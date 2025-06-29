//
//  export_code.cpp
//  Latproc
//
//  Created by Martin Leadbeater on 29/6/2025.
//
#include "daemon.h"
#include "IOComponent.h"
#include <iostream>
#include <stdio.h>
#include <unistd.h>
#include <zmq.hpp>

#include <fstream>
#include <boost/thread/condition.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/thread.hpp>
#include <list>
#include <map>
#include <signal.h>
#include <sys/stat.h>
#include <utility>

#ifndef EC_SIMULATOR
#include "tool/MasterDevice.h"
#endif
#ifdef SOEM_ETHERCAT
#include "EtherCATthread.h"
#endif

#include "DebugExtra.h"
#include "Dispatcher.h"
#include "MQTTInterface.h"
#include "MessagingInterface.h"
#include "ModbusInterface.h"
#include "PredicateAction.h"
#include "ProcessingThread.h"
#include "Scheduler.h"
#include "Statistic.h"
#include "Statistics.h"
#include "clockwork.h"
#include "symboltable.h"

namespace {

std::string base_name(const std::string &src) {
    std::string base(src);
    size_t pos = base.rfind('.');
    if (pos != std::string::npos) {
        base = base.substr(pos + 1);
    }
    return base;
}

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


}

bool generate_c() {
    const char *export_path = "/tmp/cw_export";
    std::list<MachineClass *>::iterator iter = MachineClass::all_machine_classes.begin();
    if (mkdir(export_path, 0770) == -1 && errno != EEXIST) {
        std::cerr << "failed to create export directory /tmp/cw_export.. aborting\n";
        return false;
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
    ignore.insert("DIGITALVALUE");
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
            auto iter = mc->getOptions().begin();
            while (iter != mc->getOptions().end()) {
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
            auto iter = mc->getOptions().begin();
            while (iter != mc->getOptions().end()) {
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
            std::map<std::string, size_t> &messages = ExportState::all_messages();
            std::map<std::string, size_t>::const_iterator iter = messages.begin();
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
            std::map<std::string, size_t> &messages = ExportState::all_messages();
            std::map<std::string, size_t>::const_iterator iter = messages.begin();
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
            std::map<std::string, size_t> &symbols = ExportState::all_symbols();
            std::map<std::string, size_t>::const_iterator iter = symbols.begin();
            while (iter != symbols.end()) {
                const std::pair<std::string, int> &item = *iter++;
                msg_h << "#define " << item.first << " " << item.second << "\n";
            }
        }

        msg_h << "\n#endif\n";
    }

    return true;
}
