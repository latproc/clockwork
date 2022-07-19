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

#include <fstream>
#include <inttypes.h>
#include <iostream>
#include <list>
#include <sstream>
#include <stdlib.h>
#include <string>
#include <sys/param.h>
#include <unistd.h>

#include <boost/filesystem.hpp>
#include <boost/thread.hpp>
#include <boost/thread/condition.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/utility.hpp>
#include <sys/stat.h>
#ifdef __APPLE__
#include <sys/dir.h>
#endif

#include "Channel.h"
#include "Configuration.h"
#include "DebugExtra.h"
#include "Logger.h"
#include "MachineCommandAction.h"
#include "MachineInstance.h"
#include "Message.h"
#include "MessageLog.h"
#include "ModbusInterface.h"
#include "PersistentStore.h"
#include "clockwork.h"
#include "options.h"
#include "symboltable.h"

#ifndef EC_SIMULATOR
#include "ECInterface.h"
#ifdef USE_SDO
#include "SDOEntry.h"
#endif //USE_SDO
#endif

extern int yylineno;
extern int yycharno;
const char *yyfilename = 0;
extern FILE *yyin;
int yyparse();
int yylex(void);
SymbolTable globals;
static bool cw_framework_initialised = false;

std::list<std::string> error_messages;
int num_errors = 0;

ClockworkInterpreter *ClockworkInterpreter::_instance = 0;
MachineInstance *_settings = 0;

void ClockworkProcessManager::SetTime(uint64_t t) {
    ClockworkInterpreter *cw = ClockworkInterpreter::instance();
    if (t >= cw->now()) {
        cw->current_time = t;
    }
}

ClockworkInterpreter::ClockworkInterpreter() : cycle_delay(0), default_poll_delay(0) {
    current_time = microsecs();
}

ClockworkInterpreter *ClockworkInterpreter::instance() {
    if (!_instance) {
        _instance = new ClockworkInterpreter;
    }
    return _instance;
}

void ClockworkInterpreter::setup(MachineInstance *new_settings) {
    _settings = new_settings;
    cycle_delay = &ClockworkInterpreter::instance()->settings()->getValue("CYCLE_DELAY");
    default_poll_delay =
        ClockworkInterpreter::instance()->settings()->getMutableValue("POLLING_DELAY");

    MachineInstance::polling_delay = default_poll_delay;
}

MachineInstance *ClockworkInterpreter::settings() { return _settings; }

void usage(int argc, char const *argv[]) {
    std::cerr
        << "Usage: " << argv[0] << " [-v] [-t] [-l logfilename] [-i persistent_store]\n"
        << "[-c debug_config_file] [-m modbus_mapping] [-g graph_output] [-s maxlogfilesize]\n"
        << "[-mp modbus_port] [-ps persistent_store_port] "
        << "[-cp command/iosh port] [--name device_name] [--stats | --nostats] enable/disable statistics\n"
        << "[--fix-invalid true [-e ethernet_interface]"
        << "\n";
}

static void listDirectory(const std::string pathToCheck, std::list<std::string> &file_list) {
    boost::filesystem::path dir(pathToCheck.c_str());
    try {
        for (boost::filesystem::directory_iterator iter =
                 boost::filesystem::directory_iterator(dir);
             iter != boost::filesystem::directory_iterator(); iter++) {
            boost::filesystem::directory_entry file = *iter;
            char *path_str = strdup(file.path().native().c_str());
            struct stat file_stat;
            int err = stat(path_str, &file_stat);
            if (err == -1) {
                std::cerr << "Error: " << strerror(errno) << " checking file type for " << path_str
                          << "\n";
            }
            else if (file_stat.st_mode & S_IFDIR) {
                listDirectory(path_str, file_list);
            }
            else if (boost::filesystem::exists(file.path()) &&
                     (file.path().extension() == ".lpc" || file.path().extension() == ".cw")) {
                file_list.push_back(file.path().native());
            }
            free(path_str);
        }
    }
    catch (const boost::filesystem::filesystem_error &ex) {
        std::cerr << ex.what() << '\n';
    }
}

int load_preset_modbus_mappings() {
    // load preset modbus mappings
    std::ifstream modbus_mappings_file(modbus_map());
    if (!modbus_mappings_file) {
        DBG_INITIALISATION << "No preset modbus mapping file found\n";
    }
    else {
        char buf[200];
        int lineno = 0;
        int errors = 0;

        int max_disc = 0;
        int max_coil = 0;
        int max_input = 0;
        int max_holding = 0;
        bool generate_length = false; // backward compatibility for old mappings files
        while (modbus_mappings_file.getline(buf, 200, '\n')) {
            ++lineno;
            std::stringstream line(buf);
            std::string group_addr, group, addr, name, type;
            int length = 1;
            line >> group_addr;
            size_t pos = group_addr.find(':');
            if (pos != std::string::npos) {
                group = group_addr;
                line >> name >> type;
                if (!(line >> length)) {
                    generate_length = true;
                }
                group.erase(pos);
                addr = group_addr;
                addr = group_addr.substr(pos + 1);
                std::string lookup_name(name);
                if (group == "0") {
                    size_t pos = lookup_name.rfind("cmd_");
                    if (pos != std::string::npos) {
                        lookup_name = lookup_name.replace(pos, 4, "");
                        name = lookup_name;
                    }
                }
                if (ModbusAddress::preset_modbus_mapping.count(lookup_name) == 0) {
                    int group_num, addr_num;
                    char *end = 0;
                    group_num = (int)strtol(group.c_str(), &end, 10);
                    if (*end) {
                        std::cerr << "error loading modbus mappings from " << modbus_map()
                                  << " at line " << lineno << " " << *end << "not expected\n";
                        ++errors;
                        continue;
                    }
                    addr_num = (int)strtol(addr.c_str(), &end, 10);
                    if (*end) {
                        std::cerr << "error loading modbus mappings from " << modbus_map()
                                  << " at line " << lineno << " " << *end << "not expected\n";
                        ++errors;
                        continue;
                    }
                    if (generate_length) {
                        if (type == "Signed_int_32") {
                            length = 2;
                        }
                        else if (type == "Floating_PT_32") {
                            length = 1;
                        }
                    }
                    if (group_num == 0) {
                        if (addr_num >= max_coil) {
                            max_coil = addr_num + 1;
                        }
                    }
                    else if (group_num == 1) {
                        if (addr_num >= max_disc) {
                            max_disc = addr_num + 1;
                        }
                    }
                    else if (group_num == 3) {
                        if (addr_num >= max_input) {
                            max_input = addr_num + 1;
                        }
                    }
                    else if (group_num == 4) {
                        if (addr_num >= max_holding) {
                            max_holding = addr_num + 1;
                        }
                    }
                    ModbusExport::Type exportType(ModbusExport::discrete);
                    if (type == "Signed_int_16") {
                        if (group_num == 3) {
                            exportType = ModbusExport::reg;
                        }
                        else {
                            exportType = ModbusExport::rw_reg;
                        }
                    }
                    if (type == "Signed_int_32") {
                        if (group_num == 3) {
                            exportType = ModbusExport::reg32;
                        }
                        else {
                            exportType = ModbusExport::rw_reg32;
                        }
                    }
                    if (type == "Floating_PT_32") {
                        exportType = ModbusExport::float32;
                    }
                    DBG_MODBUS << "Loaded modbus mapping " << group_num << " " << addr << " "
                               << length << "\n";
                    ModbusAddressDetails details(group_num, addr_num, exportType, length);
                    ModbusAddress::preset_modbus_mapping[name] = details;
                }
            }
            else {
                std::cerr << "error loading modbus mappings from " << modbus_map() << " at line "
                          << lineno << " missing ':' in " << group_addr << "\n";
                ++errors;
            }
        }
        if (errors) {
            std::cerr << errors << " errors. aborting.\n";
            return 1;
        }
        if (max_disc >= ModbusAddress::nextDiscrete()) {
            ModbusAddress::setNextDiscrete(max_disc + 1);
        }
        if (max_coil >= ModbusAddress::nextCoil()) {
            ModbusAddress::setNextCoil(max_coil + 1);
        }
        if (max_input >= ModbusAddress::nextInputRegister()) {
            ModbusAddress::setNextInputRegister(max_input + 1);
        }
        if (max_holding >= ModbusAddress::nextHoldingRegister()) {
            ModbusAddress::setNextHoldingRegister(max_holding + 1);
        }
    }
    return 0; // no error
}

void predefine_special_machines() {
    char host_name[100];
    int err = gethostname(host_name, 100);
    if (err == -1) {
        strcpy(host_name, "localhost");
    }
    MachineClass *settings_class = new MachineClass("SYSTEMSETTINGS");
    settings_class->addState("ready");
    settings_class->default_state = State("ready");
    settings_class->initial_state = State("ready");
    settings_class->disableAutomaticStateChanges();
    settings_class->setProperty("INFO", "Clockwork host");
    settings_class->setProperty("HOST", host_name);
    settings_class->setProperty("VERSION", "0.9");
    settings_class->setProperty("CYCLE_DELAY", 2000);
    settings_class->setProperty("POLLING_DELAY", 2000);

    MachineClass *cw_class = new MachineClass("CLOCKWORK");
    cw_class->addState("ready");
    cw_class->default_state = State("ready");
    cw_class->initial_state = State("ready");
    cw_class->disableAutomaticStateChanges();
    cw_class->setProperty("PROTOCOL", "CLOCKWORK");
    cw_class->setProperty("HOST", "localhost");
    cw_class->setProperty("PORT", 5600);

    MachineClass *point_class = new MachineClass("POINT");
    point_class->parameters.push_back(Parameter("module"));
    point_class->parameters.push_back(Parameter("offset"));
    point_class->addState("on", true);
    point_class->addState("off", true);
    point_class->default_state = State("off");
    point_class->initial_state = State("off");
    point_class->disableAutomaticStateChanges();

    point_class = new MachineClass("STATUS_FLAG");
    point_class->parameters.push_back(Parameter("module"));
    point_class->parameters.push_back(Parameter("offset"));
    point_class->parameters.push_back(Parameter("entry"));
    point_class->addState("on");
    point_class->addState("off");
    point_class->default_state = State("off");
    point_class->initial_state = State("off");
    point_class->disableAutomaticStateChanges();

    MachineClass *ain_class = new MachineClass("ANALOGINPUT");
    ain_class->parameters.push_back(Parameter("module"));
    ain_class->parameters.push_back(Parameter("offset"));
    ain_class->parameters.push_back(Parameter("filter_settings"));
    ain_class->addState("stable");
    ain_class->addState("unstable");
    ain_class->default_state = State("stable");
    ain_class->initial_state = State("stable");
    ain_class->disableAutomaticStateChanges();
    ain_class->setProperty("IOTIME", Value(0));
    ain_class->setProperty("VALUE", Value(0));
    ain_class->setProperty("Position", Value(0));
    ain_class->setProperty("Velocity", Value(0.0));
    ain_class->setProperty("Acceleration", Value(0.0));

    MachineClass *cnt_class = new MachineClass("COUNTER");
    cnt_class->parameters.push_back(Parameter("module"));
    cnt_class->parameters.push_back(Parameter("offset"));
    cnt_class->addState("stable");
    cnt_class->addState("unstable");
    cnt_class->addState("off");
    cnt_class->default_state = State("off");
    cnt_class->initial_state = State("off");
    cnt_class->disableAutomaticStateChanges();
    cnt_class->setProperty("IOTIME", Value(0));
    cnt_class->setProperty("VALUE", Value(0));
    cnt_class->setProperty("Position", Value(0));
    cnt_class->setProperty("Velocity", Value(0));

    MachineClass *re_class = new MachineClass("RATEESTIMATOR");
    re_class->parameters.push_back(Parameter("position_input"));
    re_class->parameters.push_back(Parameter("settings"));
    re_class->addState("stable");
    re_class->addState("unstable");
    re_class->addState("off");
    re_class->default_state = State("off");
    re_class->initial_state = State("off");
    re_class->disableAutomaticStateChanges();
    re_class->setProperty("VALUE", Value(0));
    re_class->setProperty("position", Value(0));

    MachineClass *cr_class = new MachineClass("COUNTERRATE");
    cr_class->parameters.push_back(Parameter("position_output"));
    cr_class->parameters.push_back(Parameter("module"));
    cr_class->parameters.push_back(Parameter("offset"));
    cr_class->addState("stable");
    cr_class->addState("unstable");
    cr_class->addState("off");
    cr_class->default_state = State("off");
    cr_class->initial_state = State("off");
    cr_class->disableAutomaticStateChanges();
    cr_class->setProperty("VALUE", Value(0));
    cr_class->setProperty("position", Value(0));

    MachineClass *aout_class = new MachineClass("ANALOGOUTPUT");
    aout_class->parameters.push_back(Parameter("module"));
    aout_class->parameters.push_back(Parameter("offset"));
    aout_class->addState("stable");
    aout_class->addState("unstable");
    aout_class->default_state = State("stable");
    aout_class->initial_state = State("stable");
    aout_class->setProperty("VALUE", Value(0));

#if 0
    MachineClass *pid_class = new MachineClass("SPEEDCONTROLLER");
    pid_class->parameters.push_back(Parameter("module"));
    pid_class->parameters.push_back(Parameter("offset"));
    pid_class->parameters.push_back(Parameter("settings"));
    pid_class->parameters.push_back(Parameter("position"));
    pid_class->parameters.push_back(Parameter("speed"));
    pid_class->addState("stable");
    pid_class->addState("unstable");
    pid_class->default_state = State("stable");
    pid_class->initial_state = State("stable");
    pid_class->setProperty("VALUE", Value(0));
#endif

    MachineClass *list_class = new MachineClass("LIST");
    list_class->addState("empty");
    list_class->addState("nonempty");
    list_class->default_state = State("empty");
    list_class->initial_state = State("empty");
    //list_class->disableAutomaticStateChanges();
    list_class->setProperty("VALUE", Value(0));

    MachineClass *ref_class = new MachineClass("REFERENCE");
    ref_class->addState("ASSIGNED");
    ref_class->addState("EMPTY");
    ref_class->default_state = State("EMPTY");
    ref_class->initial_state = State("EMPTY");
    //ref_class->disableAutomaticStateChanges();

    MachineClass *module_class = new MachineClass("MODULE");
    module_class->disableAutomaticStateChanges();
    module_class->addState("PREOP");
    module_class->addState("BOOT");
    module_class->addState("SAFEOP");
    module_class->addState("OP");
#ifdef EC_SIMULATOR
    module_class->transitions.push_back(Transition(State("INIT"), State("OP"), Message("turnOn")));
    module_class->transitions.push_back(
        Transition(State("INIT"), State("PREOP"), Message("powerUp")));
    module_class->transitions.push_back(Transition(State("PREOP"), State("OP"), Message("turnOn")));
    module_class->transitions.push_back(
        Transition(State("OP"), State("PREOP"), Message("turnOff")));
#endif

    MachineClass *publisher_class = new MachineClass("MQTTPUBLISHER");
    publisher_class->parameters.push_back(Parameter("broker"));
    publisher_class->parameters.push_back(Parameter("topic"));
    publisher_class->parameters.push_back(Parameter("message"));

    MachineClass *subscriber_class = new MachineClass("MQTTSUBSCRIBER");
    subscriber_class->parameters.push_back(Parameter("broker"));
    subscriber_class->parameters.push_back(Parameter("topic"));
    subscriber_class->setOption("message", 0); //TODO: exported code cannot handle string messages

    MachineClass *broker_class = new MachineClass("MQTTBROKER");
    broker_class->parameters.push_back(Parameter("host"));
    broker_class->parameters.push_back(Parameter("port"));
    broker_class->disableAutomaticStateChanges();

    MachineClass *cond = new MachineClass("CONDITION");
    cond->addState("true");
    cond->addState("false");
    cond->default_state = State("false");

    MachineClass *flag = new MachineClass("FLAG");
    flag->addState("on");
    flag->addState("off");
    flag->default_state = State("off");
    flag->initial_state = State("off");
    flag->disableAutomaticStateChanges();
    flag->transitions.push_back(Transition(State("off"), State("on"), Message("turnOn")));
    flag->transitions.push_back(Transition(State("on"), State("off"), Message("turnOff")));

    MachineClass *mc_variable = new MachineClass("VARIABLE");
    mc_variable->addState("ready");
    mc_variable->initial_state = State("ready");
    mc_variable->disableAutomaticStateChanges();
    mc_variable->parameters.push_back(Parameter("VAL_PARAM1"));
    mc_variable->setOption("VALUE", "VAL_PARAM1");

    MachineClass *mc_constant = new MachineClass("CONSTANT");
    mc_constant->addState("ready");
    mc_constant->initial_state = State("ready");
    mc_constant->disableAutomaticStateChanges();
    mc_constant->parameters.push_back(Parameter("VAL_PARAM1"));
    mc_constant->setOption("VALUE", "VAL_PARAM1");

#ifndef EC_SIMULATOR
    MachineClass *mcwc = new MachineClass("ETHERCAT_WORKINGCOUNTER");
    mcwc->addState("ZERO");
    mcwc->addState("INCOMPLETE");
    mcwc->addState("COMPLETE");
    mcwc->initial_state = State("ZERO");
    mcwc->disableAutomaticStateChanges();
    mcwc->setProperty("VALUE", Value(0));

    MachineClass *mcls = new MachineClass("ETHERCAT_LINKSTATUS");
    mcls->addState("DOWN");
    mcls->addState("UP");
    mcls->initial_state = State("DOWN");
    mcls->disableAutomaticStateChanges();

    MachineClass *mcec = new MachineClass("ETHERCAT_BUS");
    {
        State *init = mcec->findMutableState("INIT");
        if (!init) {
            init = new State("INIT");
            mcec->states.push_back(init);
        }
        init->setEnterFunction(ECInterface::setup);
    }
    mcec->addState("DISCONNECTED");
    mcec->addState("CONNECTED");
    mcec->addState("CONFIG");
    mcec->addState("ACTIVE");
    mcec->addState("ERROR");
    mcec->initial_state = State("INIT");
    mcec->setProperty("tolerance", Value(10));
    mcec->disableAutomaticStateChanges();
    MachineCommandTemplate *mc = new MachineCommandTemplate("activate", "");
    mcec->receives.insert(std::make_pair(Message("activate"), mc));
    mc = new MachineCommandTemplate("deactivate", "");
    mcec->receives.insert(std::make_pair(Message("deactivate"), mc));
    //  mcec->transitions.push_back(Transition(State("CONFIG"), State("ACTIVE"), Message("activate")));
    //  mcec->transitions.push_back(Transition(State("ACTIVE"), State("CONFIG"), Message("deactivate")));

    MachineInstance *miwc =
        MachineInstanceFactory::create("ETHERCAT_WC", "ETHERCAT_WORKINGCOUNTER");
    miwc->setProperties(mcwc->getProperties());
    miwc->setStateMachine(mcwc);
    miwc->setDefinitionLocation("Internal", 0);
    machines["ETHERCAT_WC"] = miwc;

    MachineInstance *mils = MachineInstanceFactory::create("ETHERCAT_LS", "ETHERCAT_LINKSTATUS");
    mils->setProperties(mcls->getProperties());
    mils->setStateMachine(mcls);
    mils->setDefinitionLocation("Internal", 0);
    machines["ETHERCAT_LS"] = mils;

    MachineInstance *miec = MachineInstanceFactory::create("ETHERCAT", "ETHERCAT_BUS");
    miec->setProperties(mcec->getProperties());
    miec->setStateMachine(mcec);
    miec->addLocal("counter", miwc);
    miec->addLocal("link", mils);
    miec->setDefinitionLocation("Internal", 0);
    machines["ETHERCAT"] = miec;
#endif

#ifdef USE_SDO
    MachineClass *mc_sdo = new MachineClass("SDOENTRY");
    mc_sdo->addState("ready");
    mc_sdo->initial_state = State("ready");
    mc_sdo->disableAutomaticStateChanges();
    mc_sdo->parameters.push_back(Parameter("MODULE"));
    mc_sdo->parameters.push_back(Parameter("INDEX"));
    mc_sdo->parameters.push_back(Parameter("SUBINDEX"));
    mc_sdo->parameters.push_back(Parameter("SIZE"));
    mc_sdo->parameters.push_back(Parameter("OFFSET"));
    mc_sdo->setOption("VALUE", 0);
#endif //USE_SDO

    MachineClass *mc_external = new MachineClass("EXTERNAL");
    mc_external->setOption("HOST", "localhost");
    mc_external->setOption("PORT", 5600);
    mc_external->setOption("PROTOCOL", "ZMQ");

    MachineInstance *settings = MachineInstanceFactory::create("SYSTEM", "SYSTEMSETTINGS");
    machines["SYSTEM"] = settings;
    settings->setProperties(settings_class->getProperties());
    settings->setStateMachine(settings_class);
    settings->setDefinitionLocation("Internal", 0);

    MachineInstance *channels = MachineInstanceFactory::create("CHANNELS", "LIST");
    machines["CHANNELS"] = channels;
    channels->setProperties(list_class->getProperties());
    channels->setStateMachine(list_class);

    ClockworkInterpreter::instance()->setup(settings);
    settings->setValue("NAME", Value(device_name(), Value::t_string));
}

void semantic_analysis() {

    std::map<std::string, MachineInstance *> machine_instances;

    std::map<std::string, MachineInstance *>::const_iterator iter = machines.begin();
    while (iter != machines.end()) {
        MachineInstance *m = (*iter).second;
        if (!m) {
            std::stringstream ss;
            ss << "## - Warning: machine table entry " << (*iter).first << " has no machine";
            ++num_errors;
            error_messages.push_back(ss.str());
        }
        else {
            machine_instances[m->fullName()] = m;
        }
        iter++;
    }

    // display machine classes and build a map of names to classes
    // also move the DEFAULT stable state to the end
    BOOST_FOREACH (MachineClass *mc, MachineClass::all_machine_classes) {
        if (mc->stable_states.size()) {
            size_t n = mc->stable_states.size();
            size_t i;
            // find the default state and move it to the end of the stable state tests
            for (i = 0; i < n - 1; ++i) {
                if (mc->stable_states[i].condition.predicate &&
                    mc->stable_states[i].condition.predicate->priority == 1) {
                    break;
                }
            }
            if (i < n - 1) {
                StableState tmp = mc->stable_states[i];
                while (i < n - 1) {
                    mc->stable_states[i] = mc->stable_states[i + 1];
                    ++i;
                }
                mc->stable_states[n - 1] = tmp;
            }
            mc->collectTimerPredicates();
        }
        machine_classes[mc->name] = mc;
    }
    // setup references for each global
    BOOST_FOREACH (MachineClass *mc, MachineClass::all_machine_classes) {
        if (!mc->global_references.empty()) {
            std::pair<std::string, MachineInstance *> node;
            BOOST_FOREACH (node, mc->global_references) {
                if (machines.count(node.first) == 0) {
                    std::stringstream ss;
                    ss << "## - Warning: Cannot find a global machine or variable named "
                       << node.first;
                    error_messages.push_back(ss.str());
                    ++num_errors;
                }
                else {
                    mc->global_references[node.first] = machines[node.first];
                }
            }
        }
    }

    // display all machine instances and link classes to their instances
    DBG_PARSER << "******* Definitions\n";
    std::list<MachineInstance *>::iterator m_iter = MachineInstance::begin();
    while (m_iter != MachineInstance::end()) {
        MachineInstance *m = *m_iter++;
        std::map<std::string, MachineClass *>::iterator c_iter = machine_classes.find(m->_type);
        if (c_iter == machine_classes.end()) {
            std::stringstream ss;
            ss << "## - Error: class " << m->_type << " not found for machine " << m->getName();
            error_messages.push_back(ss.str());
            ++num_errors;
        }
        else if ((*c_iter).second) {
            MachineClass *machine_class = (*c_iter).second;
            m->setStateMachine(machine_class);
            // make sure that analogue machines have a value property
            if (machine_class->name == "ANALOGOUTPUT" || machine_class->name == "ANALOGINPUT" ||
                machine_class->name == "COUNTER" || machine_class->name == "COUNTERRATE" ||
                machine_class->name == "RATEESTIMATOR") {
                m->properties.add("VALUE", 0, SymbolTable::NO_REPLACE);
            }
            if (machine_class->name == "COUNTERRATE" || machine_class->name == "RATEESTIMATOR") {
                m->properties.add("position", 0, SymbolTable::NO_REPLACE);
                if (machine_class->name == "COUNTERRATE") {
                    MachineInstance *pos = m->lookup(m->parameters[0]);
                    if (pos) {
                        pos->setValue("VALUE", 0);
                    }
                }
            }
        }
        else {
            std::stringstream ss;
            ss << "Error: no state machine defined for instance " << (*c_iter).first;
            error_messages.push_back(ss.str());
            ++num_errors;
        }
    }

    // stitch the definitions together

    // check the parameter count of the instantiated machine vs the machine class and raise an error if necessary
    m_iter = MachineInstance::begin();
    while (m_iter != MachineInstance::end()) {
        MachineInstance *mi = *m_iter++;
        DBG_PARSER << "Machine " << mi->getName() << " (" << mi->_type << ") has "
                   << mi->parameters.size() << " parameters\n";
        if (!mi->getStateMachine()) {
            continue;
        }

        size_t num_sm_params = mi->getStateMachine()->parameters.size();
        if (mi->getStateMachine() && mi->parameters.size() != num_sm_params) {
            // the POINT class is special; it can have either 2 or 3 parameters
            const std::string &sm_name = mi->getStateMachine()->name;
            if (sm_name == "LIST") {
                DBG_PARSER << "List has " << num_sm_params << " parameters\n";
            }
            else if (((sm_name == "POINT" || sm_name == "INPUTBIT" || sm_name == "OUTPUTBIT" ||
                       sm_name == "INPUTREGISTER" || sm_name == "OUTPUTREGISTER") &&
                      num_sm_params >= 2 && num_sm_params <= 3) ||
                     (sm_name == "COUNTERRATE" && (num_sm_params == 3 || num_sm_params == 1))
#ifdef USE_SDO
                     || (sm_name == "SDOENTRY" && (num_sm_params == 4 || num_sm_params == 5))
#endif //USE_SDO
            ) {
            }
            else {
                std::stringstream ss;
                ss << "## - Error: Machine " << sm_name << " requires " << num_sm_params
                   << " parameters but instance " << mi->getName() << " has "
                   << mi->parameters.size() << std::flush;
                std::string s = ss.str();
                ++num_errors;
                error_messages.push_back(s);
            }
        }

        // for each of the machine instance's symbol parameters,
        //  find a reference to the machine instance corresponding to the given name
        //  and raise a warning if necessary. ( should this warning be an error?)
        for (unsigned int i = 0; i < mi->parameters.size(); i++) {
            Value p_i = mi->parameters[i].val;
            if (p_i.kind == Value::t_symbol) {
                DBG_PARSER << "  parameter " << i << " " << p_i.sValue << " ("
                           << mi->parameters[i].real_name << ")\n";
                MachineInstance *found =
                    mi->lookup(mi->parameters[i]); // uses the real_name field and caches the result
                if (found) {
                    // special check of parameter types for points
                    if (mi->_type == "POINT" && i == 0 && found->_type != "MODULE" &&
                        found->_type != "MQTTBROKER") {
                        std::stringstream ss;
                        ss << "Error: in the definition of " << mi->getName() << ", " << p_i.sValue
                           << " has type " << found->_type << " but should be MODULE or MQTTBROKER";
                        std::string s = ss.str();
                        ++num_errors;
                        error_messages.push_back(s);
                    }
                    else {
                        DBG_PARSER << " linked machine " << found->getName() << " as parameter "
                                   << i << "\n";
                        mi->parameters[i].machine = found;
                        found->addDependancy(mi);
                        mi->listenTo(found);
                    }
                }
                else {
                    const Value &prop = mi->getValue(mi->parameters[i].real_name);
                    if (prop != SymbolTable::Null) {
                        mi->parameters[i].val = prop;
                        mi->parameters[i].machine = 0;
                    }
                    else {
                        std::stringstream ss;
                        ss << "Error: no instance " << p_i.sValue << " ("
                           << mi->parameters[i].real_name << ")"
                           << " found for " << mi->getName();
                        error_messages.push_back(ss.str());
                        MessageLog::instance()->add(ss.str().c_str());
                        ++num_errors;
                    }
                }
            }
            else {
                DBG_PARSER << "  parameter " << i << " " << p_i << "\n";
            }
        }
        // make sure that local machines have correct links to their
        // parameters
        DBG_PARSER << "fixing parameter references for locals in " << mi->getName() << "\n";
        for (unsigned int i = 0; i < mi->locals.size(); ++i) {
            DBG_PARSER << "   " << i << ": " << mi->locals[i].val << "\n";

            MachineInstance *m = mi->locals[i].machine;
            // fixup real names of parameters that are passed as parameters to our locals
            for (unsigned int j = 0; j < m->parameters.size(); ++j) {
                Parameter &p = m->parameters[j];
                if (p.val.kind == Value::t_symbol) {
                    for (unsigned int k = 0; k < mi->parameters.size(); ++k) {
                        Value p_k = mi->parameters[k].val;
                        if (p_k.kind == Value::t_symbol && p.val == p_k) {
                            p.real_name = mi->parameters[k].real_name;
                            break;
                        }
                    }
                    // no matching name in the parameter list, is a local machine being passed?
                    for (unsigned int k = 0; k < mi->locals.size(); ++k) {
                        if (p.val == mi->locals[k].machine->getName()) {
                            p.real_name = mi->getName() + ".";
                            p.real_name += p.val.sValue;
                            m->parameters[j].machine = mi->locals[k].machine;
                            break;
                        }
                    }
                }
            }
            for (unsigned int j = 0; j < m->parameters.size(); ++j) {
                Parameter &p = m->parameters[j];
                if (p.val.kind == Value::t_symbol) {
                    DBG_PARSER << "      " << j << ": " << p.val << "\n";
                    if (p.real_name.length() == 0) {
                        p.machine = mi->lookup(p.val);
                        if (p.machine) {
                            p.real_name = p.machine->getName();
                        }
                    }
                    else {
                        p.machine = mi->lookup(p);
                    }
                    if (p.machine) {
                        p.machine->addDependancy(m);
                        m->listenTo(p.machine);
                        DBG_PARSER << " linked parameter " << j << " of local " << m->getName()
                                   << " (" << m->parameters[j].val << ") to "
                                   << p.machine->getName() << "\n";
                    }
                    else {
                        std::stringstream ss;
                        ss << "## - Error: local " << m->getName() << " for machine "
                           << mi->getName() << " sends an unknown parameter " << p.val
                           << " at position: " << j;
                        error_messages.push_back(ss.str());
                        ++num_errors;
                    }
                }
            }
        }
    }

    // setup triggered actions for the stable states for each machine
    std::list<MachineInstance *>::iterator am_iter = MachineInstance::begin();
    am_iter = MachineInstance::begin();
    while (am_iter != MachineInstance::end()) {
        MachineInstance *mi = *am_iter++;
        if (mi->getStateMachine()) {
            BOOST_FOREACH (StableState &ss, mi->stable_states) {
                /*  ss.condition(mi); // evaluate the conditions to prep caches and dependancies
                    for (unsigned int i=0; i<mi->locals.size(); ++i) {
                    MachineInstance *local = mi->locals[i].machine;
                    if (local){
                        BOOST_FOREACH(StableState &lmss, local->stable_states) {
                            lmss.condition(local);
                            if (lmss.condition.predicate && lmss.condition.predicate->error()) {
                                //++num_errors; // dont' abort at this point
                                error_messages.push_back(lmss.condition.predicate->errorString());
                            }
                        }
                    }
                    }*/
                if (ss.condition.predicate->usesTimer(ss.timer_val)) {
                    ss.uses_timer = true;
                }
                bool subcond_uses_timer = false;
                if (ss.subcondition_handlers) {
                    BOOST_FOREACH (ConditionHandler &ch, *ss.subcondition_handlers) {
                        if (ch.condition.predicate &&
                            ch.condition.predicate->usesTimer(ch.timer_val)) {
                            ch.uses_timer = true;
                        }
                        else {
                            ch.uses_timer = false;
                        }
                    }
                }
                if (subcond_uses_timer || ss.uses_timer) {
                    mi->uses_timer = true;
                    continue;
                }
            }
        }
    }

    // make sure that references to globals are configured with dependencies
    am_iter = MachineInstance::begin();
    while (am_iter != MachineInstance::end()) {
        MachineInstance *mi = *am_iter++;
        if (mi->getStateMachine()) {
            std::pair<std::string, MachineInstance *> node;
            BOOST_FOREACH (node, mi->getStateMachine()->global_references) {
                if (node.second) {
                    node.second->addDependancy(mi);
                    mi->listenTo(node.second);
                }
                else {
                    char buf[100];
                    snprintf(buf, 100, "machine %s cannot find a global machine called: %s\n",
                             mi->getName().c_str(), node.first.c_str());
                    MessageLog::instance()->add(buf);
                    DBG_INITIALISATION << "Warning: " << node.first
                                       << " has no machine when settingup globals\n";
                }
            }
        }
        else {
            char buf[100];
            snprintf(buf, 100, "machine %s does not have a state machine\n", mi->getName().c_str());
            MessageLog::instance()->add(buf);
        }
    }

    // add receives_function entries for each machine to ensure real_name messages are captured
    m_iter = MachineInstance::begin();
    while (m_iter != MachineInstance::end()) {
        MachineInstance *mi = *m_iter++;

        std::list<std::pair<Message, MachineCommand *>> to_add;
        std::pair<Message, MachineCommand *> rcv;
        BOOST_FOREACH (rcv, mi->receives_functions) {
            if (rcv.first.getText().find('.') != std::string::npos) {
                to_add.push_back(rcv);
            }
        }

        BOOST_FOREACH (rcv, to_add) {
            std::string machine(rcv.first.getText());
            std::string event(machine);
            machine.erase(machine.find('.'));
            event = event.substr(event.find('.') + 1);
            MachineInstance *source = mi->lookup(machine);
            if (!source) {
                std::stringstream ss;
                ss << machine << ": Unknown machine " << machine << " when preparing statement "
                   << rcv.first.getText() << "\n";
                error_messages.push_back(ss.str());
                ++num_errors;
                DBG_INITIALISATION << ss.str() << "\n";
            }

            if (source && source->getName() != machine) {
                //DBG_MSG << "duplicating receive function for " << mi->getName() << " from " << source->getName() << " (" << machine << ")" << "\n";
                event = source->getName() + "." + event;
                mi->receives_functions.insert(std::make_pair(Message(event.c_str()), rcv.second));
            }
        }
    }

    // reorder the list of machines in reverse order of dependence
    MachineInstance::sort();
}

char *getFilePath(const char *fname, char *buf, size_t buf_size) {
    if (!buf_size) {
        return 0;
    }
    buf[0] = 0;
    if (fname[0] == '/') { /* absolute path already given */
        if (buf_size > strlen(fname)) {
            strncpy(buf, fname, buf_size);
        }
        else {
            return 0;
        }
    }
    else {
        size_t len;
        if (!getcwd(buf, buf_size) || errno != 0) {
            return 0;
        }
        len = strlen(buf);
        if (buf_size > len + strlen(fname) + 1) {
            snprintf(buf + len, buf_size - strlen(fname) - 1, "/%s", fname);
        }
        else {
            return 0; /* buffer not big enough */
        }
    }
    return buf;
}

int loadOptions(int argc, const char *argv[], std::list<std::string> &files) {
    const char *logfilename = NULL;
    long maxlogsize = 20000;

    /* check for commandline options, later we process config files in the order they are named */
    int i = 1;
    while (i < argc) {
        if (strcmp(argv[i], "-v") == 0) {
            set_verbose(1);
        }
        else if (strcmp(argv[i], "-t") == 0) {
            set_test_only(1);
        }
        else if (strcmp(argv[i], "-l") == 0 && i < argc - 1) {
            logfilename = argv[++i];
        }
        /*        else if (strcmp(argv[i], "-s") == 0 && i < argc-1)
                    maxlogsize = strtol(argv[++i], NULL, 10);
        */
        else if (strcmp(argv[i], "-i") == 0 && i < argc - 1) { // initialise from persistent store
            char buf[200];
            //if (getFilePath(argv[++i], buf, 200))
            set_persistent_store(argv[++i] /*strdup(buf)*/);
            /*  else {
                char err[100];
                if (errno == 0)
                    snprintf(err, 100, "failed to get path for %s", argv[i]);
                else
                    snprintf(err, 100, "failed to get path for %s: %s", argv[i], strerror(errno));

                MessageLog::instance()->add(err);
                }
            */
        }
        else if (strcmp(argv[i], "-e") == 0 && i < argc - 1) {
            set_ethercat_adapter(argv[++i]);
        }
        else if (strcmp(argv[i], "-c") == 0 && i < argc - 1) { // debug config file
            set_debug_config(argv[++i]);
        }
        else if (strcmp(argv[i], "-m") == 0 && i < argc - 1) { // modbus mapping file
            set_modbus_map(argv[++i]);
        }
        else if (strcmp(argv[i], "-g") == 0 && i < argc - 1) { // dependency graph file
            set_dependency_graph(argv[++i]);
        }
        else if (strcmp(argv[i], "-r") == 0 && i < argc - 1) { // dependency graph root
            set_graph_root(argv[++i]);
        }
        else if (strcmp(argv[i], "-p") == 0 && i < argc - 1) { // publisher port
            set_publisher_port((int)strtol(argv[++i], 0, 10), true);
        }
        else if (strcmp(argv[i], "-ps") == 0 && i < argc - 1) { // persistent store port
            set_persistent_store_port((int)strtol(argv[++i], 0, 10));
        }
        else if (strcmp(argv[i], "-mp") == 0 && i < argc - 1) { // modbus port
            set_modbus_port((int)strtol(argv[++i], 0, 10));
        }
        else if (strcmp(argv[i], "-cp") == 0 && i < argc - 1) { // command port
            set_command_port((int)strtol(argv[++i], 0, 10), true);
        }
        else if (strcmp(argv[i], "--name") == 0 && i < argc - 1) { // set the system name
            set_device_name(argv[++i]);
        }
        else if (strcmp(argv[i], "--stats") == 0) { // do not keep stats
            enable_statistics(true);
        }
        else if (strcmp(argv[i], "--nostats") == 0) { // do keep stats
            enable_statistics(false);
        }
        else if (strcmp(argv[i], "--export_c") == 0) { // generate c code
            set_export_to_c(true);
        }
        else if (strcmp(argv[i], "--config") == 0) { // use a config file
            Configuration conf(argv[++i]);
            int ecat_cpu = conf.asInt("ethercat_thread_cpu_affinity");
            if (ecat_cpu) {
                set_cpu_affinity("ethercat", ecat_cpu);
            }
            int proc_cpu = conf.asInt("processing_thread_cpu_affinity");
            if (proc_cpu) {
                set_cpu_affinity("processing", proc_cpu);
            }
        }
#if 0
        // disabled this code as it isn't working properly yet
        else if (strcmp(argv[i], "--fix-invalid") == 0) {
            std::string abort = argv[++i];
            bool val = fix_invalid_transitions();
            if (abort == "true") {
                val = true;
            }
            else if (abort == "false") {
                val = false;
            }
            set_fix_invalid_transitions(val);
        }
#endif
        else if (*(argv[i]) == '-' && strlen(argv[i]) > 1) {
            usage(argc, argv);
            return 2;
        }
        else if (*(argv[i]) == '-') {
            files.push_back(argv[i]);
        }
        else {
            struct stat file_stat;
            int err = stat(argv[i], &file_stat);
            if (err == -1) {
                std::cerr << "Error: " << strerror(errno) << " checking file type for " << argv[i]
                          << "\n";
            }
            else if (file_stat.st_mode & S_IFDIR) {
                listDirectory(argv[i], files);
            }
            else {
                files.push_back(argv[i]);
            }
        }
        i++;
    }

    // redirect output to the logfile if given TBD use Logger::setOutputStream..
    if (logfilename && strcmp(logfilename, "-") != 0) {
        int ferr;
        ferr = fflush(stdout);
        if (ferr == -1) {
            perror("fflush(stdout)");
        }
        ferr = fflush(stderr);
        if (ferr == -1) {
            perror("fflush(stderr)");
        }
        stdout = freopen(logfilename, "w+", stdout);
        ferr = dup2(1, 2);
        if (ferr == -1) {
            perror("dup2");
        }
    }

    if (!modbus_map()) {
        set_modbus_map("modbus_mappings.txt");
    }
    if (!debug_config()) {
        set_debug_config("iod.conf");
    }

    DBG_PARSER << (argc - 1) << " arguments\n";
    return 0;
}

int loadConfig(std::list<std::string> &files) {
    struct timeval now_tv;
    tzset(); /* this initialises the tz info required by ctime().  */
    srandom(microsecs());

    if (!cw_framework_initialised) {
        int modbus_result = load_preset_modbus_mappings();
        if (modbus_result) {
            return modbus_result;
        }

        predefine_special_machines();
        cw_framework_initialised = true;
    }

    /* load configuration from files named on the commandline */
    int opened_file = 0;
    std::list<std::string>::iterator f_iter = files.begin();
    while (f_iter != files.end()) {
        const char *filename = (*f_iter).c_str();
        if (filename[0] != '-') {
            opened_file = 1;
            yyin = fopen(filename, "r");
            if (yyin) {
                DBG_PARSER << "Processing file: " << filename << "\n";
                yylineno = 1;
                yycharno = 1;
                yyfilename = filename;
                yyparse();
                fclose(yyin);
            }
            else {
                std::stringstream ss;
                ss << "## - Error: failed to load config: " << filename;
                error_messages.push_back(ss.str());
                ++num_errors;
            }
        }
        else if (strlen(filename) == 1) { /* '-' means stdin */
            opened_file = 1;
            DBG_PARSER << "\nProcessing stdin\n";
            yyfilename = "stdin";
            yyin = stdin;
            yylineno = 1;
            yycharno = 1;
            yyparse();
        }
        f_iter++;
    }

    if (!opened_file) {
        return 1;
    }

    if (num_errors > 0) {
        for (std::string &error : error_messages) {
            std::cerr << error << "\n";
        }
        printf("Errors detected. Aborting\n");
        return 2;
    }

    // construct machines that shadow those defined in channels
    ChannelDefinition::instantiateInterfaces();

    semantic_analysis();

    // display errors and warnings
    if (!error_messages.empty()) {
        std::cerr << "Errors detected\n";
        BOOST_FOREACH (std::string &error, error_messages) {
            std::cerr << error << "\n";
        }
        // abort if there were errors
        std::cerr << "Aborting\n";
        return 2;
    }

    DBG_PARSER << " Configuration loaded. " << MachineInstance::countAutomaticMachines()
               << " automatic machines\n";
    //MachineInstance::displayAutomaticMachines();
    return 0;
}

void enable_all_machines() {
    // enable all machines

    bool only_startup = machine_classes.count("STARTUP") > 0;
    auto m_iter = MachineInstance::begin();
    while (m_iter != MachineInstance::end()) {
        MachineInstance *m = *m_iter++;
        Value enable = m->getValue("startup_enabled");
        if (enable == SymbolTable::Null || enable == true) {
            if (!only_startup || (only_startup && m->_type == "STARTUP"))
                if (!m->isShadow()) {
                    m->enable();
                }
        }
        else {
            DBG_INITIALISATION << m->getName() << " is disabled at startup\n";
        }
    }
}

void disable_all_machines() {
    auto m_iter = MachineInstance::begin();
    while (m_iter != MachineInstance::end()) {
        MachineInstance *m = *m_iter++;
        if (!m->isShadow()) {
            m->disable();
        }
    }
}

void load_properties_file(const std::string &filename, bool only_persistent) {
    PersistentStore store(filename);
    store.load();

    // enable all persistent variables and set their value to the
    // value in the map.
    auto m_iter = MachineInstance::begin();
    while (m_iter != MachineInstance::end()) {
        MachineInstance *m = *m_iter++;
        if (m && (!only_persistent ||
                  (only_persistent && (m->_type == "CONSTANT" || m->isPersistent())))) {
            std::string name(m->fullName());
            std::map<std::string, std::map<std::string, Value>>::iterator found =
                store.init_values.find(name);
            if (found != store.init_values.end()) {
                std::map<std::string, Value> &list((*found).second);
                PersistentStore::PropertyPair node;
                BOOST_FOREACH (node, list) {
                    long v;
                    double d;
                    DBG_INITIALISATION << name << " initialising " << node.first << " to "
                                       << node.second << " " << node.second.kind << "\n";
                    if (node.second.kind == Value::t_bool || node.second.kind == Value::t_integer ||
                        node.second.kind == Value::t_float) {
                        m->setValue(node.first, node.second);
                    }
                    else if (node.second.asFloat(d)) {
                        m->setValue(node.first, d);
                    }
                    else if (node.second.asInteger(v)) {
                        m->setValue(node.first, v);
                    }
                    else {
                        m->setValue(node.first, node.second);
                    }
                }
            }
        }
    }
}

void initialise_machines() {

    std::list<MachineInstance *>::iterator m_iter;

    if (persistent_store()) {
        load_properties_file(persistent_store(), true);
    }

    // prepare the list of machines that will be processed at idle time
    m_iter = MachineInstance::begin();
    int num_passive = 0;
    int num_active = 0;
    while (m_iter != MachineInstance::end()) {
        MachineInstance *mi = *m_iter++;
        mi->markActive();
    }

    enable_all_machines();

    // let channels start processing messages
    Channel::startChannels();
}
