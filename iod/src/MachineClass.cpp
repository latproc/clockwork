#include "MachineClass.h"
#include "MachineCommandAction.h"
#include "MachineInstance.h"
#include "ModbusInterface.h"
#include "Parameter.h"
#include "StableState.h"
#include "State.h"
#include <fstream>
#include <list>
#include <vector>

std::list<MachineClass *> MachineClass::all_machine_classes;
std::map<std::string, MachineClass> MachineClass::machine_classes;

MachineClass::MachineClass(const char *class_name)
    : default_state("unknown"), initial_state("INIT"), name(class_name), allow_auto_states(true),
      token_id(0), plugin(0), polling_delay(0) {
    addState("INIT", true);
    token_id = Tokeniser::instance()->getTokenId(class_name);
    all_machine_classes.push_back(this);
}

void MachineClass::defaultState(State state) { default_state = state; }

void MachineClass::disableAutomaticStateChanges() { allow_auto_states = false; }

void MachineClass::enableAutomaticStateChanges() { allow_auto_states = true; }

bool MachineClass::isStableState(State &state) {
    return stable_state_xref.find(state.getName()) != stable_state_xref.end();
}

bool MachineClass::isStaticState(const char *name) { return static_state_names.count(name) != 0; }

bool MachineClass::isStaticState(const std::string &name) {
    return static_state_names.count(name) != 0;
}

void MachineClass::addState(const char *state_name, bool manual_state) {
    states.push_back(new State(state_name));
    if (manual_state) {
        static_state_names.insert(state_name);
    }
}

State *MachineClass::findMutableState(const char *seek) {
    std::list<State *>::const_iterator iter = states.begin();
    while (iter != states.end()) {
        State *s = *iter++;
        if (s->getName() == seek) {
            return s;
        }
    }
    return 0;
}

const State *MachineClass::findState(const char *seek) const {
    std::list<State *>::const_iterator iter = states.begin();
    while (iter != states.end()) {
        const State *s = *iter++;
        if (s->getName() == seek) {
            return s;
        }
    }
    return 0;
}

const State *MachineClass::findState(const State &seek) const {
    std::list<State *>::const_iterator iter = states.begin();
    while (iter != states.end()) {
        const State *s = *iter++;
        if (*s == seek) {
            return s;
        }
    }
    return 0;
}

MachineClass *MachineClass::find(const char *name) {
    int token = Tokeniser::instance()->getTokenId(name);
    std::list<MachineClass *>::iterator iter = all_machine_classes.begin();
    while (iter != all_machine_classes.end()) {
        MachineClass *mc = *iter++;
        if (mc->token_id == token) {
            return mc;
        }
    }
    return 0;
}

void MachineClass::addProperty(const char *p) {
    //NB_MSG << "Warning: ignoring OPTION " << p << " in " << name << "\n";
    property_names.insert(p);
}

void MachineClass::addPrivateProperty(const char *p) {
    //NB_MSG << "Warning: ignoring OPTION " << p << " in " << name << "\n";
    property_names.insert(p);
    local_properties.insert(p); //
}

void MachineClass::addPrivateProperty(const std::string &p) {
    //NB_MSG << "Warning: ignoring OPTION " << p << " in " << name << "\n";
    property_names.insert(p.c_str());
    local_properties.insert(p); //
}

void MachineClass::addCommand(const char *p) {
    //NB_MSG << "Warning: ignoring COMMAND/RECEIVES " << p << " in " << name << "\n";
    command_names.insert(p);
}

bool MachineClass::propertyIsLocal(const char *name) const {
    return local_properties.count(name) > 0;
}

bool MachineClass::propertyIsLocal(const std::string &name) const {
    return local_properties.count(name) > 0;
}

bool MachineClass::propertyIsLocal(const Value &name) const {
    return local_properties.count(name.asString()) > 0;
}

MachineCommandTemplate *MachineClass::findMatchingCommand(std::string cmd_name, const char *state) {
    // find the correct command to use based on the current state
    std::multimap<std::string, MachineCommandTemplate *>::iterator cmd_it = commands.find(cmd_name);
    while (cmd_it != commands.end()) {
        std::pair<std::string, MachineCommandTemplate *> curr = *cmd_it++;
        if (curr.first != cmd_name) {
            break;
        }
        MachineCommandTemplate *cmd = curr.second;
        if (!state || cmd->switch_state ||
            (!cmd->switch_state && cmd->getStateName().get() == state)) {
            return cmd;
        }
    }
    return 0;
}

void MachineClass::exportHandlers(std::ostream &ofs) {
    std::list<State *>::iterator iter = states.begin();
    while (iter != states.end()) {
        const State *s = *iter++;
        ofs << "int cw_" << name << "_enter_" << s->getName() << "(struct cw_" << name
            << " *m, ccrContParam) {";
        std::string fn_name(s->getName());
        fn_name += "_enter";
        if (name == "Ramp") {
            int x = 0;
        }
        std::multimap<Message, MachineCommandTemplate *>::iterator found =
            receives.find(Message(fn_name.c_str()));
        if (found != receives.end()) {
            const std::pair<Message, MachineCommandTemplate *> &item = *found;
            ofs << "// " << (*item.second) << "\n";
            for (unsigned int i = 0; i < (item.second)->action_templates.size(); ++i) {
                ActionTemplate *at = (item.second)->action_templates.at(i);
                if (at) {
                    ofs << "// " << (*at) << "\n";
                }
            }
        }
        ofs << "  m->machine.execute = 0;\n  return 1;\n}\n";
    }

    {
        std::multimap<Message, MachineCommandTemplate *>::iterator iter = receives.begin();
        while (iter != receives.end()) {
            const std::pair<Message, MachineCommandTemplate *> &item = *iter++;
            std::string msg(item.first.getText().substr(0, item.first.getText().find('_')));
            if (state_names.find(msg) == state_names.end()) {
                ofs << "int cw_" << name << "_" << item.first << "(struct cw_" << name
                    << " *m, ccrContParam) {"
                    << "// " << (*item.second) << "\n";
                for (unsigned int i = 0; i < (item.second)->action_templates.size(); ++i) {
                    ActionTemplate *at = (item.second)->action_templates.at(i);
                    if (at) {
                        ofs << "// " << (*at) << "\n";
                    }
                }
                ofs << "  m->machine.execute = 0;\n  return 1;\n}\n";
            }
        }
    }
}

void MachineClass::exportCommands(std::ostream &ofs) {
    std::list<State *>::iterator iter = states.begin();
    while (iter != states.end()) {
        const State *s = *iter++;
        ofs << "int " << name << "_enter_" << s->getName() << "(struct cw_" << name
            << " *m, ccrContParam) {";
        std::string fn_name(s->getName());
        std::multimap<std::string, MachineCommandTemplate *>::iterator found =
            commands.find(fn_name);
        if (found != commands.end()) {
            const std::pair<std::string, MachineCommandTemplate *> &item = *found;
            ofs << "// " << (*item.second) << "\n";
            for (unsigned int i = 0; i < (item.second)->action_templates.size(); ++i) {
                ActionTemplate *at = (item.second)->action_templates.at(i);
                if (at) {
                    ofs << "// " << (*at) << "\n";
                }
            }
        }
        ofs << "m->machine.execute = 0; return 1; }\n";
    }
}

bool MachineClass::cExport(const std::string &filename) {
    // TODO: redo this with some kind of templating system

    std::stringstream params;
    std::stringstream values;
    std::stringstream refs;
    std::stringstream setup;
    {
        std::string header(filename);
        header += ".h";
        std::ofstream ofh(header);
        ofh << "#ifndef __" << name << "_h__\n"
            << "#define __" << name << "_h__\n"
            << "\n#include \"runtime.h\"\n";

        // export statenumbers
        {
            int statenum = 0;
            std::list<State *>::iterator iter = states.begin();
            while (iter != states.end()) {
                const State *s = *iter++;
                ofh << "#define state_cw_" << name << "_" << s->getName() << " " << statenum++
                    << "\n";
            }
        }
        ofh << "struct cw_" << name << ";\n"
            << "struct IOAddress *cw_" << name << "_getAddress(struct cw_" << name << " *p);\n"
            << "struct cw_" << name << " *create_cw_" << name << "(const char *name";
        {
            std::vector<Parameter>::iterator p_iter = parameters.begin();
            while (p_iter != parameters.end()) {
                const Parameter &p = *p_iter++;
                params << ", MachineBase *" << p.val;
                values << ", " << p.val;
                refs << "MachineBase *_" << p.val << ";\n";
                setup << "m->_" << p.val << " = " << p.val << ";\n";
            }
            if (name == "ANALOGINPUT" || name == "ANALOGOUTPUT") {
                ofh << ", int pin";
            }
            ofh << params.str() << ");\n"
                << "void Init_cw_" << name << "(struct cw_" << name << " * "
                << ", const char *name";
            if (name == "ANALOGINPUT" || name == "ANALOGOUTPUT") {
                ofh << ", int pin";
            }
            ofh << params.str() << ");\n"
                << "MachineBase *cw_" << name << "_To_MachineBase(struct cw_" << name << " *);\n"
                << "#endif\n";
        }
    }
    {
        std::string source(filename);
        source += ".c";
        std::ofstream ofs(source);

        {
            ofs << "\n#include \"base_includes.h\"\n"
                << "#include \"cw_" << name << ".h\""
                << "\n//static const char* TAG = \"" << name << "\";"
                << "\n#define DEBUG_LOG 0\n"
                << "struct cw_" << name << " {\n"
                << "MachineBase machine;\n"
                << "int gpio_pin;\n"
                << "struct IOAddress addr;\n"
                << refs.str();
            SymbolTableConstIterator pnames_iter = properties.begin();
            while (pnames_iter != properties.end()) {
                const std::pair<std::string, Value> prop = *pnames_iter++;
                ofs << "Value " << prop.first << "; // " << prop.second << "\n";
            }
            std::map<std::string, Value>::iterator opts_iter = options.begin();
            while (opts_iter != options.end()) {
                const std::pair<std::string, Value> opt = *opts_iter++;
                ofs << "Value " << opt.first << "; // " << opt.second << "\n";
            }
            ofs << "};\n";
        }

        ofs << "int cw_" << name << "_check_state(struct cw_" << name << " *m);\n";

        ofs << "struct cw_" << name << " *create_cw_" << name << "(const char *name";
        if (name == "ANALOGINPUT" || name == "ANALOGOUTPUT") {
            ofs << ", int pin";
        }
        ofs << params.str() << ") {\n"
            << "struct cw_" << name << " *p = (struct cw_" << name << " *)malloc(sizeof(struct cw_"
            << name << "));\n"
            << "Init_cw_" << name << "(p, name";
        if (name == "ANALOGINPUT" || name == "ANALOGOUTPUT") {
            ofs << ", pin";
        }

        ofs << values.str() << ");\n"
            << "return p;\n"
            << "}\n";

        ofs << "void Init_cw_" << name << "(struct cw_" << name << " *m, const char *name";
        if (name == "ANALOGINPUT" || name == "ANALOGOUTPUT") {
            ofs << ", int pin";
        }
        ofs << params.str() << ") {\n"
            << "initMachineBase(&m->machine, name);\n"
            << "init_io_address(&m->addr, 0, 0, 0, 0, iot_none, IO_STABLE);\n"
            << setup.str();
        SymbolTableConstIterator pnames_iter = properties.begin();
        while (pnames_iter != properties.end()) {
            const std::pair<std::string, Value> prop = *pnames_iter++;
            ofs << "m->" << prop.first << " = " << prop.second << ";\n";
        }
        std::map<std::string, Value>::iterator opts_iter = options.begin();
        while (opts_iter != options.end()) {
            const std::pair<std::string, Value> opt = *opts_iter++;
            ofs << "m->" << opt.first << " = " << opt.second << ";\n";
        }
        ofs << "m->machine.state = 0;\n"
            << "m->machine.check_state = ( int(*)(MachineBase*) )cw_" << name << "_check_state;\n"
            << "markPending(&m->machine);\n"
            << "}\n";

        ofs << "struct IOAddress *cw_" << name << "_getAddress(struct cw_" << name << " *p) {\n"
            << "  return (p->addr.io_type == iot_none) ? 0 : &p->addr;\n"
            << "}\n";

        ofs << "MachineBase *cw_" << name << "_To_MachineBase(struct cw_" << name
            << " *p) { return &p->machine; }\n";

        // export enter functions for states
        exportHandlers(ofs);
        //exportCommands(ofs);

        ofs << "int cw_" << name << "_check_state(struct cw_" << name << " *m) {\n"
            << "  int new_state = 0; enter_func new_state_enter = 0;\n";

        for (unsigned int i = 0; i < stable_states.size(); ++i) {
            const StableState &s = stable_states.at(i);
            if (i > 0) {
                ofs << " //  else\n";
            }
            ofs << "//  if (" << *s.condition.predicate << ") {\n"
                << "//    new_state = state_cw_" << name << "_" << s.state_name << ";\n"
                << "//    new_state_enter = (enter_func)cw_" << name << "_enter_" << s.state_name
                << ";\n"
                << "//  }\n";
        }

        ofs << "  if (new_state != m->machine.state)\n"
            << "    changeMachineState(cw_" << name
            << "_To_MachineBase(m), new_state, new_state_enter);\n"
            << "  return 1;\n"
            << "}\n";
    }
    return false;
}
