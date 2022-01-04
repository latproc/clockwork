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

#include <unistd.h>
//#include "ECInterface.h"
#include "ControlSystemMachine.h"
#include "IOComponent.h"
#include <sstream>
#include <stdio.h>
#include <zmq.hpp>

#include "boost/filesystem/operations.hpp"
#include "boost/filesystem/path.hpp"
#include <boost/thread.hpp>
#include <boost/thread/condition.hpp>
#include <boost/thread/mutex.hpp>
#include <fstream>
#include <list>
#include <map>
#include <signal.h>
#include <sys/stat.h>
#include <utility>

#include "cJSON.h"
#ifndef EC_SIMULATOR
#include "tool/MasterDevice.h"
#endif

#define __MAIN__
#include "Channel.h"
#include "ClientInterface.h"
#include "DebugExtra.h"
#include "Dispatcher.h"
#include "IODCommand.h"
#include "IODCommands.h"
#include "Logger.h"
#include "MQTTInterface.h"
#include "MessageLog.h"
#include "MessagingInterface.h"
#include "ModbusInterface.h"
#include "PredicateAction.h"
#include "ProcessingThread.h"
#include "Scheduler.h"
#include "Statistic.h"
#include "Statistics.h"
#include "clockwork.h"
#include "cwlang.h"
#include "symboltable.h"
#include <libgen.h>
#include <stdio.h>

bool program_done = false;
bool machine_is_ready = false;

void usage(int argc, char *argv[]);
void displaySymbolTable();

Statistics *statistics = NULL;
std::list<Statistic *> Statistic::stats;

//static boost::mutex io_mutex;
boost::condition_variable_any ecat_polltime;

typedef std::list<std::string> StringList;
std::map<std::string, StringList> wiring;

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

static void finish(int sig) {
    struct sigaction sa;
    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, 0);
    sigaction(SIGINT, &sa, 0);
    program_done = true;
    exit(0);
}

bool setup_signals() {
    struct sigaction sa;
    sa.sa_handler = finish;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGTERM, &sa, 0) || sigaction(SIGINT, &sa, 0)) {
        return false;
    }
    sa.sa_handler = SIG_IGN;
    if (sigaction(SIGPIPE, &sa, 0)) {
        return false;
    }
    return true;
}

class IODHardwareActivation : public HardwareActivation {
  public:
    void operator()(void) {
        NB_MSG << "----------- Initialising machines ------------\n";
        initialise_machines();
    }
};

void write_pin_definitions(std::ostream &out) {
    out << "#define cw_OLIMEX_GATEWAY32_LED 33\n#define cw_OLIMEX_GATEWAY32_BUT1 34\n";
    out << "#define cw_OLIMEX_GATEWAY32_GPIO16 16\n#define cw_OLIMEX_GATEWAY32_GPIO17 17\n";
    out << "#define cw_OLIMEX_GATEWAY32_GPIO32 32\n#define cw_OLIMEX_GATEWAY32_GPIO11 11\n";
}

void exportPropertyInitialisation(const MachineInstance *m, const std::string name,
                                  std::ostream &setup) {
    SymbolTableConstIterator iter = m->properties.begin();
    while (iter != m->properties.end()) {
        const std::pair<std::string, Value> &item = *iter++;
        if (item.first != "NAME") {
            setup << "\t" << name << "->" << item.first << " = " << item.second << ";\n";
        }
    }
}

void collect_connected_machines(std::set<MachineInstance *> &included_machines,
                                MachineInstance *mi) {
    if (mi) {
        included_machines.insert(mi);
        for (int i = 0; i < mi->parameters.size(); ++i) {
            if (mi->parameters[i].machine) {
                collect_connected_machines(included_machines, mi->parameters[i].machine);
            }
        }
    }
}

static std::string base_name(const std::string &src) {
    std::string base(src);
    size_t pos = base.rfind('.');
    if (pos != std::string::npos) {
        base = base.substr(pos + 1);
    }
    return base;
}

int main(int argc, char const *argv[]) {
    char *pn = strdup(argv[0]);
    program_name = strdup(basename(pn));
    free(pn);

    std::string thread_name("cw_main");
#ifdef __APPLE__
    pthread_setname_np(thread_name.c_str());
#else
    pthread_setname_np(pthread_self(), thread_name.c_str());
#endif

    zmq::context_t *context = new zmq::context_t;
    MessagingInterface::setContext(context);
    Logger::instance();
    Dispatcher::instance();
    MessageLog::setMaxMemory(10000);
    Scheduler::instance();
    ControlSystemMachine machine;

    set_debug_config("iod.conf");
    Logger::instance()->setLevel(Logger::Debug);
    //LogState::instance()->insert(DebugExtra::instance()->DEBUG_PARSER);

    std::list<std::string> source_files;
    int load_result = loadOptions(argc, argv, source_files);
    if (load_result) {
        return load_result;
    }

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

    statistics = new Statistics;
    load_result = loadConfig(source_files);
    if (load_result) {
        Dispatcher::instance()->stop();
        Scheduler::instance()->stop();
        return load_result;
    }
    load_debug_config();

    if (dependency_graph()) {
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
                for (int i = 0; i < mi->parameters.size(); ++i) {
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
    if (test_only()) {
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

        return load_result;
    }
    if (export_to_c()) {
        const char *export_path = "/tmp/cw_export";
        std::list<MachineClass *>::iterator iter = MachineClass::all_machine_classes.begin();
        if (mkdir(export_path, 0770) == -1 && errno != EEXIST) {
            std::cerr << "failed to create export directory /tmp/cw_export.. aborting\n";
            return 1;
        }
        ExportState::instance()->set_prefix(
            "v->l_"); // see implementation of toC in actiontemplates and predicates
        // setup global ids for all state names
        ExportState::add_state("INIT");
        ExportState::add_state("off");
        ExportState::add_state("on");
        ExportState::add_state("unstable");
        ExportState::add_state("stable");
        while (iter != MachineClass::all_machine_classes.end()) {
            MachineClass *mc = *iter++;
            std::set<std::string>::iterator iter = mc->state_names.begin();
            while (iter != mc->state_names.end()) {
                ExportState::add_state(*iter++);
            }
        }
        // setup standard message ids
        ExportState::add_message("turnOff", -100);
        ExportState::add_message("turnOn", -101);
        ExportState::add_message("property_change", -102);
        ExportState::add_message("black", -103);
        ExportState::add_message("red", -104);
        ExportState::add_message("green", -105);
        ExportState::add_message("blue", -106);
        ExportState::add_message("white", -107);

        ExportState::add_symbol("sym_VALUE", 1);
        ExportState::add_symbol("sym_broker", 2);
        ExportState::add_symbol("sym_message", 3);
        ExportState::add_symbol("sym_channel", 4);
        ExportState::add_symbol("sym_max_output", 5);
        ExportState::add_symbol("sym_num_pixels", 6);
        ExportState::add_symbol("sym_pin", 7);
        ExportState::add_symbol("sym_out", 8);
        ExportState::add_symbol("sym_in", 9);
        ExportState::add_symbol("sym_r", 10);
        ExportState::add_symbol("sym_g", 11);
        ExportState::add_symbol("sym_b", 12);
        ExportState::add_symbol("sym_min", 13);
        ExportState::add_symbol("sym_max", 14);
        ExportState::add_symbol("sym_strip", 15);
        ExportState::add_symbol("sym_position", 16);
        ExportState::add_symbol("sym_bytesPerPixel", 17);
        ExportState::add_symbol("sym_T0H", 18);
        ExportState::add_symbol("sym_T1H", 19);
        ExportState::add_symbol("sym_T0L", 20);
        ExportState::add_symbol("sym_T1L", 21);
        ExportState::add_symbol("sym_TRS", 22);

        // the following classes will not be exported in the exported code
        std::set<std::string> ignore;
        ignore.insert("ADC");
        ignore.insert("CLOCKWORK");
        ignore.insert("CONDITION");
        ignore.insert("CONSTANT");
        ignore.insert("COUNTER");
        ignore.insert("COUNTERRATE");
        ignore.insert("DAC");
        ignore.insert("DIGITALIN");
        ignore.insert("DIGITALIO");
        ignore.insert("ETHERNET");
        ignore.insert("JTAG");
        ignore.insert("LIST");
        ignore.insert("PIN");
        ignore.insert("POINT");
        ignore.insert("RATEESTIMATOR");
        ignore.insert("REFERENCE");
        ignore.insert("SDIO");
        ignore.insert("SPI");
        ignore.insert("STATUS_FLAG");
        ignore.insert("SYSTEMSETTINGS");
        ignore.insert("TOUCH");
        ignore.insert("UART");
        ignore.insert("VARIABLE");

        // these classes are internal and can be instantiated but not exported
        std::set<std::string> internal;
        internal.insert("ANALOGINPUT");
        internal.insert("ANALOGOUTPUT");
        internal.insert("MQTTBROKER");
        internal.insert("MQTTPUBLISHER");
        internal.insert("MQTTSUBSCRIBER");
        internal.insert("BOARD");
        internal.insert("CPU");
        internal.insert("FLAG");
        internal.insert("MODULE");
        internal.insert("INPUT");
        internal.insert("OUTPUT");
        internal.insert("EXTERNAL");
        internal.insert("LEDSTRIP");
        internal.insert("DIGITALLED");

        // pin definitions
        std::stringstream pin_definitions;

        // find the board and CPU being used
        MachineInstance *cpu = 0;
        MachineInstance *board = 0;

        std::list<MachineInstance *>::iterator instances = MachineInstance::begin();
        while (instances != MachineInstance::end()) {
            MachineInstance *m = *instances++;
            MachineClass *parent = 0;
            if (m->getStateMachine() && (parent = m->getStateMachine()->parent)) {
                if (parent->name == "CPU") {
                    assert(!cpu);
                    cpu = m;
                }
                else if (parent->name == "BOARD") {
                    assert(!board);
                    board = m;
                }
            }
        }
        if (board) {
            assert(cpu);
        }

        iter = MachineClass::all_machine_classes.begin();
        while (iter != MachineClass::all_machine_classes.end()) {
            MachineClass *mc = *iter++;
            if (mc->parent && mc->parent->name == "CPU") {
                std::map<std::string, Value>::iterator iter = mc->options.begin();
                while (iter != mc->options.end()) {
                    const std::pair<std::string, Value> &item = *iter++;
                    pin_definitions << "#define cw_" << mc->name << "_" << item.first << " ";
                    size_t n = item.first.length();
                    while (n++ < 25) {
                        pin_definitions << " ";
                    }
                    pin_definitions << item.second << "\n";
                }
            }
            else if (mc->parent && mc->parent->name == "BOARD") {
                std::map<std::string, Value> pins;
                std::map<std::string, Value>::iterator iter = mc->options.begin();
                while (iter != mc->options.end()) {
                    const std::pair<std::string, Value> &item = *iter++;
                    pin_definitions << "#define cw_" << mc->name << "_" << item.first << " ";
                    size_t n = item.first.length();
                    while (n++ < 25) {
                        pin_definitions << " ";
                    }

                    if (item.second.kind == Value::t_symbol) {
                        size_t pos = item.second.sValue.find('.');
                        if (pos == std::string::npos) {
                            pins[item.first] = pins[item.second.sValue];
                            pin_definitions << pins[item.second.sValue];
                        }
                        else {
                            std::string pin = item.second.sValue.substr(pos + 1);
                            const Value &val = cpu->getValue(pin);
                            pin_definitions << val << "\n";
                        }
                    }
                    else {
                        pin_definitions << item.second << "\n";
                    }
                }
            }
            if (ignore.count(mc->name) || internal.count(mc->name)) {
                continue;
            }
            if (mc->name == "CPU" || mc->name == "BOARD") {
                continue;
            }
            if (mc->parent && (mc->parent->name == "CPU" || mc->parent->name == "BOARD")) {
                continue;
            }
            char basename[80];
            snprintf(basename, 80, "%s/cw_%s", export_path, mc->name.c_str());
            const std::string fname(basename);
            mc->cExport(fname);
        }

        // machine instantiations

        {
            std::map<std::string, std::string> rt_names;
            rt_names["INPUT"] = "PointInput";
            rt_names["OUTPUT"] = "PointOutput";
            rt_names["ANALOGINPUT"] = "ANALOGINPUT";
            rt_names["ANALOGOUTPUT"] = "ANALOGOUTPUT";

            std::string setup_file(export_path);
            setup_file += "/cw_setup.inc";

            // while exporting machine instantiations, we also look for special things, for example,
            // LEDSTRIPs and DIGITALLEDs.  These require extra initialisation to link them together.
            std::list<MachineInstance *> led_strips;
            std::list<MachineInstance *> digital_leds;

            std::ofstream setup(setup_file);
            setup << "#include <iointerface.h>\n#include \"driver/gpio.h\"\n\n";
            setup << pin_definitions.str();
            std::list<MachineInstance *>::iterator instances = MachineInstance::begin();
            while (instances != MachineInstance::end()) {
                MachineInstance *m = *instances++;
                MachineClass *mc = m->getStateMachine();
                if (mc && mc->name == "LEDSTRIP") {
                    led_strips.push_back(m);
                }
                else if (mc && mc->name == "DIGITALLED") {
                    digital_leds.push_back(m);
                }
                if (m->getName() == "lolibot") {
                    int x = 1;
                }
                if (mc && mc->parent &&
                    (mc->parent->name == "CPU" || mc->parent->name == "BOARD")) {
                    continue;
                }

                if (m->_type == "INPUT" || m->_type == "OUTPUT") {
                    const Value &level = m->getValue("level");
                    std::string item_name = std::string("cw_inst_") + m->getName();
                    std::string local_pin_name(base_name(m->parameters[1].real_name));
                    std::string pin_name =
                        std::string("cw_") + m->parameters[0].machine->_type + "_" + local_pin_name;
                    setup << "struct " << rt_names[m->_type] << " *" << item_name << " = create_cw_"
                          << rt_names[m->_type] << "(\"" << m->getName() << "\", " << pin_name;
                    if (m->_type == "OUTPUT") {
                        setup << ", " << level.iValue;
                    }
                    setup << ");\n";
                    setup << "gpio_pad_select_gpio(" << pin_name << ");\n";
                    setup << "gpio_set_direction(" << pin_name << ", GPIO_MODE_" << m->_type
                          << ");\n";
                    if (m->_type == "OUTPUT") {
                        setup << "gpio_set_level(" << pin_name << ", " << level.iValue << ");\n";
                    }

                    setup << "{\n\tstruct MachineBase *m = cw_" << rt_names[m->_type]
                          << "_To_MachineBase(" << item_name << ");\n";
                    exportPropertyInitialisation(m, item_name, setup);
                    setup << "\tif (m->init) m->init();\n";
                    setup << "\tstruct IOItem *item_" << item_name << " = IOItem_create(m, cw_"
                          << rt_names[m->_type] << "_getAddress(" << item_name << "), " << pin_name
                          << ");\n";
                    setup << "\tRTIOInterface_add(interface, item_" << item_name << ");\n}\n";
                }
                else if (m->_type == "ANALOGINPUT") {
                    std::string item_name = std::string("cw_inst_") + m->getName();
                    std::string local_pin_name(base_name(m->parameters[1].real_name));
                    std::string pin_name =
                        std::string("cw_") + m->parameters[0].machine->_type + "_" + local_pin_name;
                    setup << "struct cw_" << rt_names[m->_type] << " *" << item_name
                          << " = create_cw_" << rt_names[m->_type] << "(\"" << m->getName()
                          << "\", " << pin_name << ", 0, 0, " << local_pin_name << " , 0);\n";

                    setup << "{\n\tstruct MachineBase *m = cw_" << rt_names[m->_type]
                          << "_To_MachineBase(" << item_name << ");\n";
                    exportPropertyInitialisation(m, item_name, setup);
                    setup << "\tif (m->init) m->init();\n";
                    setup << "\tstruct IOItem *item_" << item_name << " = IOItem_create(m, cw_"
                          << rt_names[m->_type] << "_getAddress(" << item_name << "), " << pin_name
                          << ");\n";
                    setup << "\tRTIOInterface_add(interface, item_" << item_name << ");\n}\n";
                }
                else if (m->_type == "ANALOGOUTPUT") {
                    const Value &channel = m->getValue("channel");
                    std::string item_name = std::string("cw_inst_") + m->getName();
                    std::string local_pin_name(base_name(m->parameters[1].real_name));
                    std::string pin_name =
                        std::string("cw_") + m->parameters[0].machine->_type + "_" + local_pin_name;
                    setup << "struct cw_" << rt_names[m->_type] << " *" << item_name
                          << " = create_cw_" << rt_names[m->_type] << "(\"" << m->getName()
                          << "\", " << pin_name << ", 0, 0, LEDC_CHANNEL_" << channel.iValue
                          << ");\n";

                    setup << "{\n\tstruct MachineBase *m = cw_" << rt_names[m->_type]
                          << "_To_MachineBase(" << item_name << ");\n";
                    exportPropertyInitialisation(m, item_name, setup);
                    setup << "\tif (m->init) m->init();\n";
                    setup << "\tstruct IOItem *item_" << item_name << " = IOItem_create(m, cw_"
                          << rt_names[m->_type] << "_getAddress(" << item_name << "), " << pin_name
                          << ");\n";
                    setup << "\tRTIOInterface_add(interface, item_" << item_name << ");\n}\n";
                }
                else if (!ignore.count(m->_type)) {
                    std::string item_name = std::string("cw_inst_") + m->getName();
                    setup << "struct cw_" << m->_type << " *" << item_name << " = create_cw_"
                          << m->_type << "(\"" << m->getName() << "\"";
                    for (size_t i = 0; i < m->parameters.size(); ++i) {
                        if (m->parameters[i].machine) {
                            setup << ", (MachineBase*)cw_inst_"
                                  << m->parameters[i].machine->getName();
                        }
                        else {
                            setup << ", " << m->parameters[i].val;
                        }
                    }
                    setup << ");\n";

                    setup << "{\n\tstruct MachineBase *m = cw_" << m->_type << "_To_MachineBase("
                          << item_name << ");\n";
                    exportPropertyInitialisation(m, item_name, setup);
                    setup << "\tif (m->init) m->init();\n";
                    setup << "}\n";
                }
            }
            if (led_strips.size() && digital_leds.size()) {
                instances = digital_leds.begin();
                while (instances != digital_leds.end()) {
                    MachineInstance *m = *instances++;
                    MachineClass *mc = m->getStateMachine();
                    if (m->parameters.size() == 2 &&
                        m->parameters[0].machine->_type == "LEDSTRIP") {
                        setup << "add_led_to_strip(cw_inst_" << m->parameters[0].machine->getName()
                              << ", cw_inst_" << m->getName() << ");\n";
                    }
                }
            }
        }

        {
            std::string msg_header(export_path);
            msg_header += "/cw_message_ids.h";
            std::ofstream msg_h(msg_header);
            msg_h << "#ifndef __cw_message_ids_h__\n"
                  << "#define __cw_message_ids_h__\n\n"
                  << "\nconst char *name_from_id(int id);\n";
            // export messages
            {
                std::map<std::string, int> &messages = ExportState::all_messages();
                std::map<std::string, int>::const_iterator iter = messages.begin();
                while (iter != messages.end()) {
                    const std::pair<std::string, int> &item = *iter++;
                    if (item.second >=
                        0) { // don't export standard messages, they are define in runtime.h
                        msg_h << "#define cw_message_" << item.first << " " << item.second << "\n";
                    }
                }
            }
            msg_h << "\n#endif\n";
        }

        {
            std::string msg_support(export_path);
            msg_support += "/cw_message_ids.c";
            std::ofstream msg_c(msg_support);
            msg_c << "\nconst char *name_from_id(int id) {\n";
            {
                std::map<std::string, int> &messages = ExportState::all_messages();
                std::map<std::string, int>::const_iterator iter = messages.begin();
                while (iter != messages.end()) {
                    const std::pair<std::string, int> &item = *iter++;
                    msg_c << "\tif (id==" << item.second << ") return \"" << item.first << "\";\n";
                }
                msg_c << "\treturn \"*unknown*\";\n";
            }
            msg_c << "}\n";
        }

        // symbol ids
        {
            std::string msg_header(export_path);
            msg_header += "/cw_symbol_ids.h";
            std::ofstream msg_h(msg_header);
            msg_h << "#ifndef __cw_symbol_ids_h__\n"
                  << "#define __cw_symbol_ids_h__\n\n";
            // export messages
            {
                std::map<std::string, int> &symbols = ExportState::all_symbols();
                std::map<std::string, int>::const_iterator iter = symbols.begin();
                while (iter != symbols.end()) {
                    const std::pair<std::string, int> &item = *iter++;
                    msg_h << "#define " << item.first << " " << item.second << "\n";
                }
            }

            msg_h << "\n#endif\n";
        }

        return 0;
    }

    MQTTInterface::instance()->init();
    MQTTInterface::instance()->start();

    std::list<Output *> output_list;
    {
        {
            size_t remaining = machines.size();
            DBG_PARSER << remaining << " Machines\n";
            DBG_INITIALISATION << "Initialising MQTT\n";

            // find and process all MQTT Modules as they are required before POINTS that use them
            std::map<std::string, MachineInstance *>::const_iterator iter = machines.begin();
            while (iter != machines.end()) {
                MachineInstance *m = (*iter).second;
                iter++;
                if (m->_type == "MQTTBROKER" && m->parameters.size() == 2) {
                    MQTTModule *module = MQTTInterface::instance()->findModule(m->getName());
                    if (module) {
                        std::cerr << "MQTT Broker " << m->getName() << " is already registered\n";
                        continue;
                    }
                    module = new MQTTModule(m->getName().c_str());
                    module->host = m->parameters[0].val.asString();
                    long port;
                    if (m->parameters[1].val.asInteger(port)) {
                        module->port = (int)port;
                        MQTTInterface::instance()->addModule(module, false);
                        module->connect();
                    }
                }
            }

            iter = machines.begin();
            while (iter != machines.end()) {
                MachineInstance *m = (*iter).second;
                iter++;
                --remaining;
                if (m->_type == "MQTTBROKER" && m->parameters.size() == 2) {
                    continue;
                }
                else if (m->_type == "POINT" && m->parameters.size() > 1 &&
                         m->parameters[1].val.kind == Value::t_integer) {
                    std::string name = m->parameters[0].real_name;
                    //int bit_position = (int)m->parameters[1].val.iValue;
                    //std::cerr << "Setting up point " << m->getName() << " " << bit_position << " on module " << name << "\n";
                    MachineInstance *module_mi = MachineInstance::find(name.c_str());
                    if (!module_mi) {
                        std::cerr << "No machine called " << name << "\n";
                        continue;
                    }
                    if (!module_mi->properties.exists("position")) { // module position not given
                        std::cerr << "Machine " << name << " does not specify a position\n";
                        continue;
                    }
                    std::cerr << module_mi->properties.lookup("position").kind << "\n";
                    int module_position = (int)module_mi->properties.lookup("position").iValue;
                    if (module_position == -1) { // module position unmapped
                        std::cerr << "Machine " << name << " position not mapped\n";
                        continue;
                    }
                }
                else {
                    if (m->_type != "POINT" && m->_type != "MQTTPUBLISHER" &&
                        m->_type != "MQTTSUBSCRIBER") {
                        //DBG_MSG << "Skipping " << m->_type << " " << m->getName() << " (not a POINT)\n";
                    }
                    else {
                        std::string name = m->parameters[0].real_name;
                        //DBG_MSG << "Setting up " << m->_type << " " << m->getName() << " \n";
                        MQTTModule *module =
                            MQTTInterface::instance()->findModule(m->parameters[0].real_name);
                        if (!module) {
                            std::cerr << "No MQTT Broker called " << m->parameters[0].real_name
                                      << "\n";
                            continue;
                        }
                        std::string topic = m->parameters[1].val.asString();
                        if (m->_type != "MQTTSUBSCRIBER" && m->parameters.size() == 3) {
                            if (!module->publishes(topic)) {
                                m->properties.add("type", "Output");
                                module->publish(topic, m->parameters[2].val.asString(), m);
                            }
                        }
                        else if (m->_type != "MQTTPUBLISHER") {
                            if (!module->subscribes(topic)) {
                                m->properties.add("type", "Input");
                                module->subscribe(topic, m);
                            }
                        }
                        else {
                            std::cerr << "Error defining instance " << m->getName() << "\n";
                        }
                    }
                }
            }
            assert(remaining == 0);
        }
    }
    //MachineInstance::displayAll();
#ifdef EC_SIMULATOR
    wiring["EL2008_OUT_3"].push_back("EL1008_IN_1");
#endif

    bool sigok = setup_signals();
    assert(sigok);

    IODCommandThread *stateMonitor = IODCommandThread::instance();
    IODHardwareActivation iod_activation;
    ProcessingThread &processMonitor(
        ProcessingThread::create(&machine, iod_activation, *stateMonitor));

    zmq::socket_t sim_io(*MessagingInterface::getContext(), ZMQ_REP);
    sim_io.bind("inproc://ethercat_sync");

    DBG_INITIALISATION << "-------- Starting Scheduler ---------\n";
    boost::thread scheduler_thread(boost::ref(*Scheduler::instance()));
    Scheduler::instance()->setThreadRef(scheduler_thread);

    DBG_INITIALISATION << "-------- Starting Command Interface ---------\n";
    boost::thread monitor(boost::ref(*stateMonitor));

    // Inform the modbus interface we have started
    load_debug_config();
    ModbusAddress::message("STARTUP");
    Dispatcher::start();

    processMonitor.setProcessingThreadInstance(&processMonitor);
    boost::thread process(boost::ref(processMonitor));

    MQTTInterface::instance()->activate();

    char buf[100];
    size_t response_len;
    if (safeRecv(sim_io, buf, 100, true, response_len, 0)) {
        buf[(response_len < 100) ? response_len : 99] = 0;
        NB_MSG << "simulated io got start message: " << buf << "\n";
    }

    NB_MSG << "processing has started\n";
    uint64_t then = microsecs();
    while (!program_done) {
        MQTTInterface::instance()->collectState();

        //sim_io.send("ecat", 4);
        safeRecv(sim_io, buf, 10, false, response_len, 100);
        uint64_t now = microsecs();

        int64_t delta = now - then;
        // use the clockwork interpreter's current cycle delay
        const Value *cycle_delay_v = ClockworkInterpreter::instance()->cycle_delay;
        assert(cycle_delay_v);
        int64_t delay = cycle_delay_v->iValue;
        if (delay <= 1000) {
            delay = 1000;
        }
        if (delay - delta > 50) {
            delay = delay - delta - 50;
            //NB_MSG << "waiting: " << delay << "\n";
            struct timespec sleep_time;
            sleep_time.tv_sec = delay / 1000000;
            sleep_time.tv_nsec = (delay * 1000) % 1000000000L;
            int rc;
            struct timespec remaining;
            while ((rc = nanosleep(&sleep_time, &remaining) == -1)) {
                sleep_time = remaining;
            }
        }

        then = now;
    }
    try {
        MQTTInterface::instance()->stop();
        Dispatcher::instance()->stop();
        processMonitor.stop();
        kill(0, SIGTERM); // interrupt select() and poll()s to enable termination
        process.join();
        stateMonitor->stop();
        monitor.join();
        delete context;
    }
    catch (const zmq::error_t &) {
    }
    return 0;
}
