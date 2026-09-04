/*
    Copyright (C) 2012 Martin Leadbeater, Michael O'Connor

    This file is part of Latproc

    Latproc is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License
    as published by the Free Software Foundation; either version 2
    of the License, or (at your option) any later version.

    Latproc is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Latproc; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#include "ControlSystemMachine.h"
#include "ECInterface.h"
#include "IOComponent.h"
#include "boost/filesystem/operations.hpp"
#include "boost/filesystem/path.hpp"
#include <iostream>
#include <sstream>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zmq.hpp>

#include "cJSON.h"
#include <boost/thread/condition.hpp>
#include <boost/thread/mutex.hpp>
#include <fstream>
#include <list>
#include <map>
#include <utility>
#ifndef EC_SIMULATOR
#ifdef USE_SDO
#include "SDOEntry.h"
#endif //USE_SDO
#endif
#ifdef __linux__
#endif

#define __MAIN__
#include "Channel.h"
#include "ClientInterface.h"
#include "DebugExtra.h"
#include "Dispatcher.h"
#include "EtherCATSetup.h"
#include "IODCommand.h"
#include "IODCommands.h"
#include "Logger.h"
#include "MQTTInterface.h"
#include "MachineInstance.h"
#include "MessageLog.h"
#include "MessagingInterface.h"
#include "ModbusInterface.h"
#include "PredicateAction.h"
#include "ProcessingThread.h"
#include "Scheduler.h"
#include "Statistic.h"
#include "Statistics.h"
#include "StallTrace.h"
#include "clockwork.h"
#include "ecat_thread.h"
#include "ethercat_xml_parser.h"
#include "options.h"
#include "symboltable.h"
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include "ThreadSafeQueue.h"
#include <thread>

bool program_done = false;
bool machine_is_ready = false;

// svc -d / daemontools send SIGTERM. Set program_done so threads leave cleanly
// and ecat can deactivate (reduces SM-watchdog spam vs SIGKILL / svc -k).
// If processing is wedged it never sees program_done — _exit after 2s of a
// frozen heartbeat so supervise can restart (1G2C-122 2026-08/09 wedges).
static void iod_request_shutdown(int /*sig*/) {
    program_done = true;
}

static void iod_shutdown_watch() {
#ifdef __linux__
    pthread_setname_np(pthread_self(), "iod shutdown wd");
#endif
    while (!program_done) {
        usleep(100000);
    }
    const uint64_t seq0 = StallTrace::heartbeatSeq();
    for (int i = 0; i < 20; ++i) {
        usleep(100000);
        if (!program_done) {
            return;
        }
        if (StallTrace::heartbeatSeq() != seq0) {
            return;
        }
    }
    if (program_done) {
        std::cerr << "iod: SIGTERM: processing heartbeat frozen 2s; _exit\n";
        _exit(1);
    }
}

void usage(int argc, char *argv[]);
void displaySymbolTable();

Statistics *statistics = NULL;
std::list<Statistic *> Statistic::stats;

boost::mutex thread_protection_mutex;
static boost::mutex io_mutex;
static boost::mutex model_mutex;
boost::condition_variable_any io_updated;
boost::condition_variable_any model_updated;
boost::mutex ecat_mutex;
boost::condition_variable_any ecat_polltime;

void load_debug_config() {
    if (debug_config()) {
        std::ifstream program_config(debug_config());
        if (program_config) {
            std::string debug_flag;
            while (program_config >> debug_flag) {
                if (debug_flag[0] == '#') {
                    continue;
                }
                int dbg = LogState::instance()->lookup(debug_flag);
                if (dbg) {
                    LogState::instance()->insert(dbg);
                }
                else if (machines.count(debug_flag)) {
                    MachineInstance *mi = machines[debug_flag];
                    if (mi) {
                        mi->setDebug(true);
                    }
                }
                else {
                    std::cerr << "Warning: unrecognised DEBUG Flag " << debug_flag << "\n";
                }
            }
        }
    }
}

std::list<DeviceInfo *> collected_configurations;
std::map<unsigned int, DeviceInfo *> slave_configuration;
class ClockworkDeviceConfigurator : public DeviceConfigurator {
  public:
    bool configure(DeviceInfo *dev) {
        DBG_INITIALISATION << "collected configuration for device " << std::hex << " 0x"
                           << dev->product_code << " " << std::hex << " 0x" << dev->revision_no
                           << std::dec << "\n";
        std::list<DeviceInfo *>::iterator iter = collected_configurations.begin();
        while (iter != collected_configurations.end()) {
            const DeviceInfo *di = *iter++;
            if (*dev == *di) {
                DBG_INITIALISATION << " using item already found\n";
                return true;
            }
        }
        collected_configurations.push_back(dev);
        return true;
    }
};

bool setupEtherCatThread() {
    // Ensure the master is opened before module/config work (state-machine
    // ENTER may not have run yet when the processing thread activates hardware).
    if (!ECInterface::instance()->initialised) {
        ECInterface::instance()->init();
    }
    if (!ECInterface::instance()->initialised) {
        std::cerr << "Cannot setup EtherCAT: master open failed\n";
        return false;
    }
#ifndef EC_SIMULATOR
    {
        //determine which EtherCAT modules are to use configurations loaded from
        // XML files
        // build a list of modules with xml configs and a list of xml file references
        //   collecting product code and revision numbers if specified
        // where product codes and revision numbers are not specified in the config
        //   the module in the bus position will be used
        // search the bus to complete the product_code/release_no details if necessary
        // search the xml files for matching modules
        ClockworkDeviceConfigurator configurator;
        EtherCATXMLParser parser(configurator);

        // Populate the list of ECModules from the bus. This guarantees that
        // the positions in the list match the position of the module on the bus.
        collectEtherCatModules();
        std::vector<ec_slave_info_t> slaves = ECInterface::instance()->listSlaves();
        DBG_INITIALISATION << "found " << slaves.size() << " slaves on the bus\n";

        // Search for XML configurations in all modules and replace previous
        // modules.
        std::list<MachineInstance *>::iterator iter = MachineInstance::begin();
        std::set<std::string> xml_files;
        while (iter != MachineInstance::end()) {
            const int error_buf_size = 100;
            char error_buf[error_buf_size];
            MachineInstance *m = *iter++;
            if (m->_type == "MODULE") {
                const Value &position = m->getValue("position");
                if (position >= slaves.size()) {
                    std::stringstream ss;
                    ss << "No slave at position " << position;
                    MessageLog::instance()->add(ss.str());
                    DBG_INITIALISATION << ss.str() << "\n";
                    continue;
                }
                const Value &xml_filename = m->getValue("config_file");
                const Value &selected_sm = m->getValue("alternate_sync_manager");
                const Value &product_code = m->getValue("ProductCode");
                const Value &revision_no = m->getValue("RevisionNo");
                if (xml_filename != SymbolTable::Null) {
                    DBG_INITIALISATION << "using xml configuration file " << xml_filename << " for "
                                       << m->getName() << " at position " << position << "\n";
                    xml_files.insert(xml_filename.sValue);
                    Value pc(product_code);
                    Value rn(revision_no);
                    Value sm(selected_sm);
                    if (product_code == SymbolTable::Null) {
                        // find product code of the device at this position
                        const ec_slave_info_t &slave(slaves[position.iValue]);
                        pc = (long)slave.product_code;
                        DBG_INITIALISATION << "setting product code for position "
                                           << position.iValue << " to " << std::hex << "0x" << pc
                                           << std::dec << " from bus slave at position "
                                           << position.iValue << "\n";
                    }
                    else {
                        DBG_INITIALISATION << "using config file product code " << std::hex << "0x"
                                           << pc << std::dec << ":" << std::hex << "0x" << rn
                                           << std::dec << " for position " << position.iValue
                                           << "\n";
                    }
                    if (revision_no == SymbolTable::Null) {
                        rn = (long)slaves[position.iValue].revision_number;
                        // find product code of the device at this position
                        const ec_slave_info_t &slave(slaves[position.iValue]);
                        DBG_INITIALISATION << "setting product code for position "
                                           << position.iValue << " to " << std::hex << "0x" << rn
                                           << std::dec << "\n";
                    }
                    if (sm == SymbolTable::Null) {
                        sm = Value("", Value::t_string);
                    }

                    // Due to limitations of the parser, we reset and reparse for every module.
                    // TODO: Identify modules to be configured and load them all from a single
                    //       pass through the XML.
                    parser.xml_configured.clear();
                    DeviceInfo *dev = new DeviceInfo(pc.iValue, rn.iValue, sm.sValue.c_str());
                    parser.xml_configured.push_back(dev);
                    parser.init();
                    if (!parser.loadDeviceConfigurationXML(xml_filename.sValue.c_str())) {
                        std::stringstream ss;
                        ss << "Warning: failed to load module configuration from " << xml_filename
                           << "\n";
                        MessageLog::instance()->add(ss.str());
                        NB_MSG << ss.str() << "\n";
                    }
                    else {
                        DBG_INITIALISATION << "checking for " << *dev << "\n";
                        DeviceInfo *di = nullptr;
                        assert(collected_configurations.size() <= 1);
                        if (collected_configurations.size() == 1) {
                            di = *collected_configurations.begin();
                        }
                        if (di) {
                            slave_configuration[position.iValue] = di;
                            const ec_slave_info_t &slave(slaves[position.iValue]);
                            ECModule *module = new ECModule();

                            module->name = slave.name;
                            module->alias = slave.alias;
                            module->position = slave.position;
                            module->vendor_id = slave.vendor_id;
                            module->product_code = slave.product_code;
                            module->revision_no = slave.revision_number;
                            module->syncs = di->config.c_syncs;
                            module->pdos = 0;
                            module->pdo_entries = di->config.c_entries;
                            module->sync_count = di->config.num_syncs;
                            module->entry_details = di->config.c_entry_details;
                            module->num_entries = di->config.num_entries;
                            auto res = ECInterface::instance()->addModule(module, true);
                            module->link_to_machine(m);
                            if (res) {
                                DBG_INITIALISATION << "iod: Added module " << module->name
                                        << " at position " << module->position
                                        << " num entries: " << module->num_entries
                                        << "\n";
                            }
                            else {
                                std::cerr << "addModule: " << module->name << " " << res.error() << "\n";
                                delete module; // module may be already registered
                            }
                            delete di;
                            collected_configurations.clear();
                        }
                        else {
                            DBG_INITIALISATION << "error: found " << collected_configurations.size()
                                               << " for slave at position " << position
                                               << " when searching xml file for device " << std::hex
                                               << pc.iValue << "/" << rn.iValue << std::dec << ":"
                                               << sm.sValue << "\n";
                        }
                    }
                }
            }
        }
#if 0
        // having collected a number of xml files we load the manual configurations
        std::set<std::string>::iterator fi(xml_files.begin());
        while (fi != xml_files.end()) {
            const std::string &fname = *fi++;
            parser.init();
            DBG_INITIALISATION << "attempting to load devices from " << fname << "\n";
            if (!parser.loadDeviceConfigurationXML(fname.c_str())) {
                std::cerr << "Warning: failed to load module configuration from " << fname << "\n";
            }
        }
        DBG_INITIALISATION << "Collected " << collected_configurations.size() << " configurations\n\n";
#endif

        ECInterface::instance()->configureModules();
        ECInterface::instance()->registerModules();
        char *slave_config = collectSlaveConfig(false);
        if (slave_config) {
            free(slave_config);
        }
    }
#endif
    generateIOComponentModules(slave_configuration);
#ifndef EC_SIMULATOR
#ifdef USE_SDO
    // prepare all SDO entries
    SDOEntry::resolveSDOModules();
#endif //USE_SDO
#endif
    IOComponent::setupIOMap();
    initialiseOutputs();
    return true;
}

class IODHardwareActivation : public HardwareActivation {
  public:
    IODHardwareActivation() : setup_done(false) {}
    bool initialiseHardware() {
        assert(!setup_done);
        if (setupEtherCatThread()) {
#ifdef USE_SDO
            ECInterface::instance()->beginModulePreparation();
#endif
            DBG_INITIALISATION << "EtherCAT thread setup ok\n";
            setup_done = true;
            return true;
        }
        else {
            std::cerr << "Warning: ECInterface failed to setup the EtherCAT thread\n";
            return false;
        }
    }
    void operator()(void) {
        DBG_INITIALISATION << "----------- Initialising machines ------------\n";
        initialise_machines();
    }

  private:
    bool setup_done;
};

int main(int argc, char const *argv[]) {
    char *pn = strdup(argv[0]);
    program_name = strdup(basename(pn));
    free(pn);
    std::string thread_name("iod_main");
#ifdef __APPLE__
    pthread_setname_np(thread_name.c_str());
#else
    pthread_setname_np(pthread_self(), thread_name.c_str());
#endif

    {
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = iod_request_shutdown;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGTERM, &sa, nullptr);
        sigaction(SIGINT, &sa, nullptr);
    }
    std::thread(iod_shutdown_watch).detach();

    boost::condition_variable_any processing_condition;
    boost::shared_mutex processing_queue_mutex;
    SharedThreadSafeQueue<Package*> processing_queue(processing_condition, processing_queue_mutex);

    boost::condition_variable_any mqtt_source_condition;
    boost::shared_mutex mqt_source_queue_mutex;
    SharedThreadSafeQueue<MQTTInterface::MQTTReceivedMessage*> mqtt_source_queue(mqtt_source_condition, mqt_source_queue_mutex);

    zmq::context_t *context = new zmq::context_t;
    MessagingInterface::setContext(context);
    Logger::instance();
    Dispatcher::create(processing_queue);
    MessageLog::setMaxMemory(10000);
    Scheduler::instance();

    ControlSystemMachine machine;
    IODCommandThread *stateMonitor = IODCommandThread::instance();
    IODHardwareActivation iod_activation;
    ProcessingThread &processMonitor(
        ProcessingThread::create(&machine, iod_activation, *stateMonitor, processing_queue, mqtt_source_queue));

    Logger::instance()->setLevel(Logger::Debug);
    //LogState::instance()->insert(DebugExtra::instance()->DEBUG_PARSER);

    std::list<std::string> source_files;
    int load_result = loadOptions(argc, argv, source_files);
    if (load_result) {
        Dispatcher::instance()->stop();
        Scheduler::shutdown();
        return load_result;
    }
    if (help_only()) {
        Dispatcher::instance()->stop();
        Scheduler::shutdown();
        return 0;
    }
    load_debug_config();

#if 0
    //LogState::instance()->insert(DebugExtra::instance()->DEBUG_PREDICATES);
    //LogState::instance()->insert(DebugExtra::instance()->DEBUG_INITIALISATION);
    //LogState::instance()->insert(DebugExtra::instance()->DEBUG_MESSAGING);
    //LogState::instance()->insert(DebugExtra::instance()->DEBUG_ACTIONS);
    //DBG_INITIALISATION << DebugExtra::instance()->DEBUG_PREDICATES << "\n";
    //assert (!LogState::instance()->includes(DebugExtra::instance()->DEBUG_PREDICATES));
    //LogState::instance()->insert(DebugExtra::instance()->DEBUG_SCHEDULER);
    //LogState::instance()->insert(DebugExtra::instance()->DEBUG_PROPERTIES);
    //LogState::instance()->insert(DebugExtra::instance()->DEBUG_MESSAGING);
    //LogState::instance()->insert(DebugExtra::instance()->DEBUG_STATECHANGES);
    //LogState::instance()->insert(DebugExtra::instance()->DEBUG_AUTOSTATES);
    //LogState::instance()->insert(DebugExtra::instance()->DEBUG_MODBUS);
#endif

    IODCommandListJSON::no_display.insert("tab");
    IODCommandListJSON::no_display.insert("type");
    IODCommandListJSON::no_display.insert("name");
    IODCommandListJSON::no_display.insert("image");
    IODCommandListJSON::no_display.insert("class");
    IODCommandListJSON::no_display.insert("state");
    IODCommandListJSON::no_display.insert("export");
    IODCommandListJSON::no_display.insert("startup_enabled");
    IODCommandListJSON::no_display.insert("NAME");
    IODCommandListJSON::no_display.insert("STATE");
    IODCommandListJSON::no_display.insert("PERSISTENT");
    IODCommandListJSON::no_display.insert("POLLING_DELAY");
    IODCommandListJSON::no_display.insert("TRACEABLE");
    IODCommandListJSON::no_display.insert("default");

    load_debug_config();
    statistics = new Statistics;
    load_result = loadConfig(source_files);
    if (load_result) {
        return load_result;
    }
    if (dependency_graph()) {
        DBG_INITIALISATION << "writing dependency graph to " << dependency_graph() << "\n";
        std::ofstream graph(dependency_graph());
        if (graph) {
            graph << "digraph G {\n";
            std::list<MachineInstance *>::iterator m_iter;
            m_iter = MachineInstance::begin();
            while (m_iter != MachineInstance::end()) {
                MachineInstance *mi = *m_iter++;
                if (!mi->depends.empty()) {
                    BOOST_FOREACH (MachineInstance *dep, mi->depends) {
                        graph << mi->getName() << " -> " << dep->getName() << ";\n";
                    }
                }
            }
            graph << "}\n";
        }
        else {
            std::cerr << "not able to open " << dependency_graph() << " for write\n";
        }
    }

    if (test_only()) {
        const char *backup_file_name = "modbus_mappings.bak";
        ControlSystemMachine machine;
        rename(modbus_map(), backup_file_name);
        // export the modbus mappings and exit
        std::list<MachineInstance *>::iterator m_iter = MachineInstance::begin();
        std::ofstream out(modbus_map());
        if (!out) {
            std::cerr << "not able to open " << modbus_map() << " for write\n";
            return false;
        }
        while (m_iter != MachineInstance::end()) {
            (*m_iter)->exportModbusMapping(out);
            m_iter++;
        }
        out.close();

        return load_result;
    }

    // SYSTEM.CYCLE_DELAY = EtherCAT period (µs). POLLING_DELAY is Clockwork-only.
    // applyCyclePeriodUs locks bus period at activate on plant iod-elc
    // use set_cycle_time + FREQUENCY; ecat thread observes get_cycle_time().
#ifndef EC_SIMULATOR
    {
        const Value *cycle_delay_v = ClockworkInterpreter::instance()->cycle_delay;
        long delay = 500;
        if (cycle_delay_v && cycle_delay_v->iValue >= 100) {
            delay = cycle_delay_v->iValue;
        }
        ECInterface::instance()->applyCyclePeriodUs(static_cast<unsigned long>(delay));
    }
#endif

    MachineInstance *ethercat_status = MachineInstance::find("ETHERCAT");
    if (!ethercat_status) {
        std::cerr << "Warning: No instance of the EtherCAT control machine found\n";
    }

    if (num_errors > 0) {
        // display errors and warnings
        BOOST_FOREACH (std::string &error, error_messages) {
            std::cerr << error << "\n";
            MessageLog::instance()->add(error.c_str());
        }
        // abort if there were errors
        std::cerr << "Errors detected. Aborting\n";
        return 2;
    }

    DBG_INITIALISATION << "-------- Initialising ---------\n";

    MQTTInterface::instance()->init();
    MQTTInterface::instance()->start(mqtt_source_queue);

    DBG_INITIALISATION << "-------- Starting EtherCAT Interface ---------\n";
    EtherCATThread ethercat;
    boost::thread ecat_thread(boost::ref(ethercat));
    DBG_INITIALISATION << "-------- Starting Scheduler ---------\n";
    boost::thread scheduler_thread(boost::ref(*Scheduler::instance()));
    Scheduler::instance()->setThreadRef(scheduler_thread);

    boost::thread monitor(boost::ref(*stateMonitor));
    usleep(50000); // give time before starting the processin g thread

    // Inform the modbus interface we have started
    ModbusAddress::message("STARTUP");
    Dispatcher::start();

    processMonitor.setProcessingThreadInstance(&processMonitor);
    boost::thread process(boost::ref(processMonitor));
#ifdef __linux__
    {
        int processing_cpu = cpu_affinity("processing");
        if (processing_cpu) {
            cpu_set_t cpuset;
            CPU_ZERO(&cpuset);
            CPU_SET(processing_cpu, &cpuset);
            int rc = pthread_setaffinity_np(process.native_handle(), sizeof(cpu_set_t), &cpuset);
            if (rc != 0) {
                std::cerr << "Error calling pthread_setaffinity_np: " << rc << "\n";
            }
            else {
                DBG_INITIALISATION << "Set processing thread cpu affinity to " << processing_cpu
                                   << "\n";
            }
        }
    }
#endif

    // let channels start processing messages
    Channel::startChannels();

    // do not start a thread, simply run this process directly
    //processMonitor();
    try {
        // Blocks for plant life. Returns only when processing ends (SIGTERM,
        // SHUTDOWN LPC/cmd, QUIT). Do NOT arm a teardown watchdog before this —
        // that kills a healthy iod every N seconds (looks like crash/restart loop).
        process.join();
        std::cerr << "iod: processing thread exited; starting teardown\n";
        // Teardown-only watchdog: if MQTT/etc hang after join, force exit so
        // supervise restarts a full process (no half-dead :5555-gone zombie).
        {
            std::thread([]() {
                sleep(8);
                std::cerr << "iod: shutdown watchdog — teardown still running after 8s; "
                             "forcing _exit(1) so supervise can restart a full process\n";
                _exit(1);
            }).detach();
        }
        stateMonitor->stop();
        MQTTInterface::instance()->stop();
        Dispatcher::instance()->stop();
        processMonitor.stop();
        ethercat.stop();
        usleep(200);
        // Avoid tearing down condition variables while worker threads may still
        // hold them (MessagingInterface::abort() can throw ZMQ EFSM here).
        _exit(0);
    }
    catch (const zmq::error_t &ex) {
        std::cerr << "Error on exit: " << ex.what() << "\n";
        _exit(0);
    }
    _exit(0);
}
