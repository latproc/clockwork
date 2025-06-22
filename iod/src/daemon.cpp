#include "daemon.h"
#include "Logger.h"
#include "MessagingInterface.h"
#include "DebugExtra.h"
#include "Dispatcher.h"
#include "MachineInstance.h"
#include "MQTTInterface.h"
#include "MessageLog.h"
#include "SharedQueueManager.h"
#include "Scheduler.h"
#include "ProcessingThread.h"
#include "IODCommands.h"

#include <pthread.h>

namespace {

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
            LogState::instance()->insert(DebugExtra::instance()->DEBUG_PARSER);
        }
    }
}

}

Daemon::Daemon(const std::string & program_name, const std::string &name, UserData *user_data)
        : thread_name(name), user_data(user_data), initialized(false),
          context(new zmq::context_t),
          notify_zmq(*context, shared_queue_notification_endpoint),
          queue_notification(*context, ZMQ_PULL) {
#ifdef __APPLE__
    pthread_setname_np(thread_name.c_str());
#else
    pthread_setname_np(pthread_self(), thread_name.c_str());
#endif

    char *pn = strdup(program_name.c_str());
    ::program_name = strdup(basename(pn));
    free(pn);
    MessagingInterface::setContext(context);
    queue_notification.bind(shared_queue_notification_endpoint);

    dbg_instance = DebugExtra::instance();
    queue_manager.create<Package*>("processing", processing_condition, processing_queue_mutex);
    queue_manager.create<MachineInstance*>("refresh", refresh_condition, refresh_queue_mutex);
    queue_manager.create<MQTTInterface::MQTTReceivedMessage*>("mqtt_source", mqtt_source_condition, mqtt_source_queue_mutex);
    queue_manager.create<ScheduledItem*>("scheduler", scheduler_condition, scheduler_queue_mutex);

    Logger::instance();
    Dispatcher::create(queue_manager.get<Package*>("processing"));
    MessageLog::setMaxMemory(10000);
    Scheduler::create(queue_manager.get<ScheduledItem*>("scheduler"));

    set_debug_config("iod.conf");
    Logger::instance()->setLevel(Logger::Debug);

}

void Daemon::stop() {
    MQTTInterface::instance()->stop();
    Dispatcher::instance()->stop();
    processMonitor->stop();
    Scheduler::instance()->stop();

    MessagingInterface::setContext(nullptr);
    process->join();
    stateMonitor->stop();
    monitor->join();
    delete Dispatcher::instance();
    queue_manager.clear();
    // kill(0, SIGTERM); // interrupt select() and poll()s
    // delete context;
    // context = nullptr;

    initialized = false;
}

void Daemon::do_not_export_internal_properties() {
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
}

void Daemon::start(HardwareActivation *activation, UserData *user_data) {
    if (!initialized) {
        initialized = true;
        DBG_INITIALISATION << "Daemon started with thread name: " << thread_name << "\n";
    MQTTInterface::instance()->init();
    MQTTInterface::instance()->start(queue_manager.get<MQTTInterface::MQTTReceivedMessage*>("mqtt_source"));

    stateMonitor = IODCommandThread::instance();
    MachineInstance::setRefreshQueue(&queue_manager.get<MachineInstance*>("refresh"));
    processMonitor = &
        ProcessingThread::create(machine, *activation, *stateMonitor, *this,
            queue_manager.get<Package*>("processing"),
            queue_manager.get<MachineInstance*>("refresh"),
            queue_manager.get<MQTTInterface::MQTTReceivedMessage*>("mqtt_source"),
            queue_manager.get<ScheduledItem*>("scheduler"));

    DBG_INITIALISATION << "-------- Starting Scheduler ---------\n";
    scheduler_thread.emplace(boost::ref(*Scheduler::instance()));
    Scheduler::instance()->setThreadRef(*scheduler_thread);

    DBG_INITIALISATION << "-------- Starting Command Interface ---------\n";
    monitor.emplace(boost::ref(*stateMonitor));

    // Inform the modbus interface we have started
    load_debug_config();
    ModbusAddress::message("STARTUP");
    Dispatcher::start();
    DBG_INITIALISATION << "started dispatcher thread\n";

    processMonitor->setProcessingThreadInstance(processMonitor);
    process.emplace(boost::ref(*processMonitor));

    MQTTInterface::instance()->activate();

#ifdef SOEM_ETHERCAT
    if (strlen(ethercat_adapter()) > 0) {
        std::cout << "Using SOEM EtherCAT on interface " << ethercat_adapter() << "\n";
        EtherCATthread::instance()->activate(ethercat_adapter());
        EtherCATthread::instance()->stop();
    }
    else {
        std::cout << "Requires ethernet interface name: -e name\n";
    }
#endif
    }
}

void collect_connected_machines(std::set<MachineInstance *> &included_machines,
                                MachineInstance *mi) {
    if (mi) {
        included_machines.insert(mi);
        for (size_t i = 0; i < mi->parameters.size(); ++i) {
            if (mi->parameters[i].machine) {
                collect_connected_machines(included_machines, mi->parameters[i].machine);
            }
        }
    }
}

void display_dependency_graph() {
        std::ofstream graph(dependency_graph());
        if (graph) {
            std::set<MachineInstance *> included_machines;
            if (graph_root()) {
                MachineInstance *mi = MachineInstance::find(graph_root());
                collect_connected_machines(included_machines, mi);
            }
            graph << "digraph G {\n\tnode [shape=record];\n";
            std::list<MachineInstance *>::iterator m_iter;
            m_iter = MachineInstance::begin();
            while (m_iter != MachineInstance::end()) {
                MachineInstance *mi = *m_iter++;
                if (graph_root() && included_machines.find(mi) == included_machines.end()) {
                    continue;
                }
                for (size_t i = 0; i < mi->parameters.size(); ++i) {
                    if (mi->parameters[i].machine) {
                        graph << mi->parameters[i].machine->getName() << " -> " << mi->getName()
                              << ";\n";
                    }
                }
            }
            graph << "}\n";
        }
        else {
            std::cerr << "not able to open " << dependency_graph() << " for write\n";
        }
}

void check_for_errors() {
}

int export_modbus_mappings(int result) {
    const char *backup_file_name = "modbus_mappings.bak";
    rename(modbus_map(), backup_file_name);
    // export the modbus mappings and exit
    std::list<MachineInstance *>::iterator m_iter = MachineInstance::begin();
    std::ofstream out(modbus_map());
    if (!out) {
        std::cerr << "not able to open " << modbus_map() << " for write\n";
        return 1;
    }
    while (m_iter != MachineInstance::end()) {
        (*m_iter)->exportModbusMapping(out);
        m_iter++;
    }
    out.close();

    return result;
}

