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

#include "IODCommands.h"
#include "Channel.h"
#include "DebugExtra.h"
#include "IOComponent.h"
#include "Logger.h"
#include "MachineInstance.h"
#include "MessageEncoding.h"
#include "MessageLog.h"
#include "MessagingInterface.h"
#include "Scheduler.h"
#include "SharedWorkSet.h"
#include "Statistic.h"
#include "Statistics.h"
#include "cJSON.h"
#include "options.h"
#include <fstream>
#include <list>
#include <stdlib.h>
#include <iostream>

#ifndef EC_SIMULATOR
#include "ECInterface.h"
#ifdef USE_SDO
#include "SDOEntry.h"
#endif //USE_SDO
#endif

extern Statistics *statistics;

std::map<std::string, std::string> message_handlers;

SequenceNumber IODCommand::sequences;

extern bool program_done;

std::ostream &IODCommand::operator<<(std::ostream &out) const {
    const char *delim = "";
    for (unsigned int i = 0; i < parameters.size(); ++i) {
        out << delim << parameters[i];
        delim = " ";
    }
    return out;
}

std::ostream &operator<<(std::ostream &out, const IODCommand &cmd) { return cmd.operator<<(out); }

bool IODCommandGetStatus::run(std::vector<Value> &params) {
    bool ok = false;
    if (params.size() == 2) {

        std::string ds = params[1].asString();
        IOComponent *device = IOComponent::lookup_device(ds);
        if (device) {
            ok = true;
            std::string res = device->getStateString();
            if (device->address.bitlen > 1) {
                char buf[10];
                snprintf(buf, 9, "(%d)", device->value());
                res += buf;
            }
            result_str = res;
        }
        else {
            MachineInstance *machine = MachineInstance::find(params[1].asString().c_str());
            if (machine) {
                ok = true;
                result_str = machine->getCurrentStateString();
            }
            else {
                error_str = "Not Found";
            }
        }
    }
    return ok;
}

bool IODCommandSetStatus::run(std::vector<Value> &params) {
    std::string ds;
    std::string state_name;
    uint64_t auth = 0;
    if (params.size() == 5 || (params.size() == 4 && params[2] == "TO")) { // SET machine TO state
        ds = params[1].asString();
        state_name = params[3].asString();
        if (params.size() == 5) {
            long res;
            if (params[4].asInteger(res)) {
                auth = res;
            }
        }
    }
    else if (params.size() == 3 || params.size() == 4) { // STATE machine state
        ds = params[1].asString();
        state_name = params[2].asString();
        if (params.size() == 4) {
            long res;
            if (params[3].asInteger(res)) {
                auth = res;
            }
        }
    }
    else {
        error_str = "Usage: SET device TO state";
        return false;
    }

    Output *device = dynamic_cast<Output *>(IOComponent::lookup_device(ds));
    if (device) {
        if (state_name == "on") {
            device->turnOn();
        }
        else if (state_name == "off") {
            device->turnOff();
        }
        result_str = device->getStateString();
        return true;
    }
    else {
        /* TBD Note: this command does not support setting a machine state to a property value */
        MachineInstance *mi = MachineInstance::find(ds.c_str());
        if (mi) {
            if (!mi->getStateMachine()) {
                char buf[150];
                snprintf(buf, 150, "Error: machine %s has no state machine", mi->getName().c_str());
                MessageLog::instance()->add(buf);
                error_str = buf;
                return false;
            }
            const State *s = mi->getStateMachine()->findState(state_name.c_str());
            if (!s) {
                if (mi->isShadow()) {
                    // shadow machines are intended to move to their initial state if the requested state is unknown
                    s = &mi->getStateMachine()->initial_state;
                }
                else {
                    char buf[150];
                    snprintf(buf, 150, "Error: machine %s has no state called '%s'",
                             mi->getName().c_str(), state_name.c_str());
                    MessageLog::instance()->add(buf);
                    error_str = buf;
                    return false;
                }
            }
            /*  it would be safer to push the requested state change onto the machine's
                action list but some machines do not poll their action list because they
                do not expect to receive events
            */
            if (mi->isShadow()) {
                mi->setState(state_name.c_str(), auth, false);
            }
            else {
                SetStateActionTemplate ssat("SELF", state_name);
                mi->enqueueAction(ssat.factory(
                    mi)); // execute this state change once all other actions are complete
            }
            result_str = "OK";
            return true;
        }
    }
    //  Send reply back to client
    const char *msg_text = "Not found: ";
    size_t len = strlen(msg_text) + ds.length();
    char *text = (char *)malloc(len + 1);
    snprintf(text, len + 1, "%s%s", msg_text, ds.c_str());
    error_str = text;
    free(text);
    return false;
}

bool IODCommandEnable::run(std::vector<Value> &params) {
    std::cout << "received iod command ENABLE " << params[1] << "\n";
    if (params.size() == 2) {
        MachineInstance *m = MachineInstance::find(params[1].asString().c_str());
        if (m) {
            m->enable();
            result_str = "OK";
            return true;
        }
        else {
            error_str = "Could not find machine";
            return false;
        }
    }
    error_str = "Failed to find machine";
    return false;
}

bool IODCommandResume::run(std::vector<Value> &params) {
    std::cout << "received iod command RESUME " << params[1] << "\n";
    if (params.size() == 2) {
        DBG_MSG << "resuming " << params[1] << "\n";
        MachineInstance *m = MachineInstance::find(params[1].asString().c_str());
        if (m && !m->enabled()) {
            m->resume();
        }
        result_str = "OK";
        return true;
    }
    else if (params.size() == 4 && params[2] == "AT") {
        DBG_MSG << "resuming " << params[1] << " at state " << params[3] << "\n";
        MachineInstance *m = MachineInstance::find(params[1].asString().c_str());
        if (m && !m->enabled()) {
            m->resume();
        }
        result_str = "OK";
        return true;
    }
    error_str = "Failed to find machine";
    return false;
}

bool IODCommandDisable::run(std::vector<Value> &params) {
    std::cout << "received iod command DISABLE " << params[1] << "\n";
    if (params.size() == 2) {
        MachineInstance *m = MachineInstance::find(params[1].asString().c_str());
        if (m) {
            m->disable();
            result_str = "OK";
            return true;
        }
        else {
            error_str = "Could not find machine";
            return false;
        }
    }
    error_str = "Failed to find machine";
    return false;
}

bool IODCommandDescribe::run(std::vector<Value> &params) {
    std::cout << "received iod command Describe " << params[1] << "\n";
    bool use_json = false;
    if (params.size() == 3 && params[2] != "JSON") {
        error_str = "Usage: DESCRIBE machine [JSON]";
        return false;
    }
    if (params.size() == 2 || params.size() == 3) {
        MachineInstance *m = MachineInstance::find(params[1].asString().c_str());
        cJSON *root;
        if (use_json) {
            root = cJSON_CreateArray();
        }
        std::stringstream ss;
        if (m) {
            m->describe(ss);
        }
        else {
            ss << "Failed to describe unknown machine " << params[1];
        }
        if (use_json) {
            std::istringstream iss(ss.str());
            char buf[500];
            while (iss.getline(buf, 500, '\n')) {
                cJSON_AddItemToArray(root, cJSON_CreateString(buf));
            }
            char *res = cJSON_Print(root);
            cJSON_Delete(root);
            result_str = res;
            free(res);
        }
        else {
            result_str = ss.str();
        }
        return true;
    }
    error_str = "Failed to find machine";
    return false;
}

bool IODCommandToggle::run(std::vector<Value> &params) {
    if (params.size() == 2) {
        DBG_MSG << "toggling " << params[1] << "\n";
        size_t pos = params[1].asString().find('-');
        std::string machine_name = params[1].asString();
        if (pos != std::string::npos) {
            machine_name.erase(pos);
        }
        MachineInstance *m = MachineInstance::find(machine_name.c_str());
        if (m) {
            if (pos != std::string::npos) {
                machine_name = params[1].asString().substr(pos + 1);
                m = m->lookup(machine_name);
                if (!m) {
                    error_str = "No such machine";
                    return false;
                }
            }
            if (!m->enabled()) {
                error_str = "Device is disabled";
                return false;
            }
            if (m->_type != "POINT") {
                Message *msg;
                if (m->getCurrent().is(ClockworkToken::on)) {
                    if (m->receives(Message("turnOff"), 0)) {
                        Message msg("turnOff");
                        m->sendMessageToReceiver(msg, m);
                    }
                    else {
                        const State *s = m->getStateMachine()->findState("off");
                        if (s) {
                            if (!m->isActive()) {
                                m->setState(*s);
                            }
                            else {
                                // execute this state change once all other actions are complete
                                SetStateActionTemplate ssat("SELF", "off");
                                m->enqueueAction(ssat.factory(m));
                            }
                        }
                    }
                }
                else if (m->getCurrent().is(ClockworkToken::off)) {
                    if (m->receives(Message("turnOn"), 0)) {
                        Message msg("turnOn");
                        m->sendMessageToReceiver(msg, m);
                    }
                    else {
                        const State *s = m->getStateMachine()->findState("on");
                        if (s) {
                            if (!m->isActive()) {
                                m->setState(*s);
                            }
                            else {
                                // execute this state change once all other actions are complete
                                SetStateActionTemplate ssat("SELF", "on");
                                m->enqueueAction(ssat.factory(m));
                            }
                        }
                    }
                }
                result_str = "OK";
                return true;
            }
        }
        else {
            error_str = "Usage: toggle device_name";
            return false;
        }

        Output *device = dynamic_cast<Output *>(IOComponent::lookup_device(params[1].asString()));
        if (device) {
            if (device->isOn()) {
                device->turnOff();
            }
            else if (device->isOff()) {
                device->turnOn();
            }
            else {
                error_str = "device is neither on nor off\n";
                return false;
            }
            result_str = "OK";
            return true;
        }
        else {
            // MQTT
            if (m->getCurrent().getName() == "on" || m->getCurrent().getName() == "off") {
                std::string msg_str = m->getCurrent().getName();
                if (msg_str == "off") {
                    msg_str = "on_enter";
                }
                else {
                    msg_str = "off_enter";
                }
                //m->mq_interface->send(new Message(msg_str.c_str()), m);
                m->execute(Message(msg_str.c_str(), Message::ENTERMSG), m->mq_interface);
                result_str = "OK";
                return true;
            }
            else {
                error_str = "Unknown message for a POINT";
                return false;
            }
        }
    }
    else {
        error_str = "Unknown device";
        return false;
    }
}

bool IODCommandGetProperty::run(std::vector<Value> &params) {
    MachineInstance *m = MachineInstance::find(params[1].asString().c_str());
    if (m) {
        if (params.size() != 3) {
            error_str = "Error: usage is GET machine property";
            return false;
        }
        const Value &v = m->getValue(params[2].asString());
        if (v == SymbolTable::Null) {
            error_str = "Error: property not found";
            return false;
        }
        else {
            if (v.kind == Value::t_dynamic && v.dynamicValue()) {
                const Value *last = v.dynamicValue()->lastResult();
                if (last) {
                    result_str = last->asString();
                }
            }
            else {
                result_str = v.asString();
            }
            return true;
        }
    }
    else {
        error_str = "Error: Unknown device";
        return false;
    }
}

bool IODCommandProperty::run(std::vector<Value> &params) {
    bool changed = false;
    //if (params.size() == 4) {
    MachineInstance *m = MachineInstance::find(params[1].asString().c_str());
    if (m) {
        if (params.size() > 5) {
            error_str = "Usage: PROPERTY machine property value";
            return false;
        }
        else if (params.size() == 4 || params.size() == 5) {
            if (m->debug()) {
                DBG_PROPERTIES << "setting property " << params[1] << "." << params[2] << " to "
                               << params[3] << "\n";
            }
            long authority = 0;
            bool use_authority = false;
            if (params.size() == 5 && params[4].asInteger(authority)) {
                use_authority = true;
            }
            if (use_authority && authority && !m->isShadow()) {
                error_str = "Refusing to change property due to authorisation conflict";
                //NB_MSG << error() << "\n";
                return false;
            }
            if (params[3].kind == Value::t_string || params[3].kind == Value::t_symbol) {
                long x;
                char *p;
                x = strtol(params[3].asString().c_str(), &p, 10);
                if (use_authority)
                    if (*p == 0) {
                        changed = m->setValue(params[2].asString(), x, authority);
                    }
                    else {
                        changed = m->setValue(params[2].asString(), params[3], authority);
                    }
                else if (*p == 0) {
                    changed = m->setValue(params[2].asString(), x);
                }
                else {
                    changed = m->setValue(params[2].asString(), params[3]);
                }
            }
            else {
                if (use_authority) {
                    changed = m->setValue(params[2].asString(), params[3], authority);
                }
                else {
                    changed = m->setValue(params[2].asString(), params[3]);
                }
            }
        }
        if (changed) {
            result_str = "OK";
        }
        else {
            error_str = "Could not set property";
        }
        return changed;
    }
    else {
        char buf[200];
        snprintf(buf, 200, "Unknown device: %s", params[1].asString().c_str());
        error_str = buf;
        return false;
    }
}

bool IODCommandList::run(std::vector<Value> &params) {
    std::ostringstream ss;
    std::list<MachineInstance *>::const_iterator iter = MachineInstance::begin();
    while (iter != MachineInstance::end()) {
        MachineInstance *m = *iter;
        ss << (m->fullName()) << " " << m->_type;
        if (m->_type == "POINT") {
            ss << " " << m->properties.lookup("tab");
        }
        ss << "\n";
        iter++;
    }
    result_str = ss.str();
    return true;
}

bool IODCommandShow::run(std::vector<Value> &params) {
    if (params.size() != 2) {
        error_str = "usage: SHOW machine";
        ;
        return false;
    }
    std::ostringstream ss;
    std::map<std::string, MachineInstance *>::iterator found = machines.find(params[1].asString());
    if (found != machines.end()) {
        MachineInstance *m = (*found).second;
        const Value &val = m->properties.lookup("VALUE");
        if (val != SymbolTable::Null) {
            ss << m->getName() << " " << m->_type << " " << val
               << ((m->enabled()) ? "" : " [DISABLED]");
        }
        else {
            ss << m->getName() << " " << m->_type << " " << m->getCurrentStateString()
               << ((m->enabled()) ? "" : " [DISABLED]");
        }
        result_str = ss.str();
        return true;
    }
    else {
        char buf[200];
        snprintf(buf, 200, "Unknown device: %s", params[1].asString().c_str());
        error_str = buf;
        return false;
    }
}

bool IODCommandFind::run(std::vector<Value> &params) {
    std::ostringstream ss;
    std::map<std::string, MachineInstance *>::const_iterator iter = machines.begin();
    while (iter != machines.end()) {
        MachineInstance *m = (*iter).second;
        const Value &val = m->properties.lookup("VALUE");
        if (params.size() == 1 || m->getName().find(params[1].asString()) != std::string::npos) {
            if (val != SymbolTable::Null) {
                ss << m->getName() << " " << m->_type << " " << val
                   << ((m->enabled()) ? "" : " [DISABLED]") << "\n";
            }
            else {
                ss << m->getName() << " " << m->_type << " " << m->getCurrentStateString()
                   << ((m->enabled()) ? "" : " [DISABLED]") << "\n";
            }
        }
        else if (params.size() == 1 ||
                 (m->getStateMachine() &&
                  m->getStateMachine()->name.find(params[1].asString()) != std::string::npos)) {
            ss << (m->getName()) << " " << m->_type << " " << m->getCurrentStateString()
               << ((m->enabled()) ? "" : " [DISABLED]") << "\n";
        }
        iter++;
    }
    result_str = ss.str();
    return true;
}

bool IODCommandBusy::run(std::vector<Value> &params) {
    std::ostringstream ss;
    if (!SharedWorkSet::instance()->empty()) {
        ss << "Busy machines: ";
        std::set<MachineInstance *>::iterator iter = SharedWorkSet::instance()->begin();
        const char *delim = "";
        while (iter != SharedWorkSet::instance()->end()) {
            MachineInstance *m = *iter++;
            ss << delim << m->getName();
            delim = ",";
        }
        ss << "\n\n";
    }
    uint64_t now = microsecs();
    std::list<MachineInstance *>::iterator iter = MachineInstance::begin();
    while (iter != MachineInstance::end()) {
        MachineInstance *m = *iter++;
        if (!m->active_actions.empty() || m->executingCommand()) {
            ss << m->getName() << " " << m->_type << ":" << m->getCurrentStateString() << " "
               << ((m->isActive()) ? "" : "[INACTIVE] ") << ((m->enabled()) ? "" : "[DISABLED] ");
            simple_deltat(ss, now - m->lastStateEvaluationTime());
            ss << "\n";
        }
    }
    result_str = ss.str();
    return true;
}

std::set<std::string> IODCommandListJSON::no_display;

cJSON *printMachineInstanceToJSON(MachineInstance *m, std::string prefix = "") {
    cJSON *node = cJSON_CreateObject();
    std::string name_str = m->getName();
    if (prefix.length() != 0) {
        std::stringstream ss;
        ss << prefix << '-' << m->getName() << std::flush;
        name_str = ss.str();
    }
    cJSON_AddStringToObject(node, "name", name_str.c_str());
    cJSON_AddStringToObject(node, "class", m->_type.c_str());
#ifndef EC_SIMULATOR
    if (m->io_interface) {
        IOComponent *ioc = m->io_interface;
        cJSON_AddNumberToObject(node, "module", ioc->address.module_position);
        ECModule *mod = ECInterface::findModule(ioc->address.module_position);
        if (mod) {
            cJSON_AddStringToObject(node, "module_name", mod->name.c_str());
        }
    }
#endif

    SymbolTableConstIterator st_iter = m->properties.begin();
    while (st_iter != m->properties.end()) {
        std::pair<std::string, Value> item(*st_iter++);
        if (item.second.kind == Value::t_integer) {
            cJSON_AddNumberToObject(node, item.first.c_str(), item.second.iValue);
        }
        else if (item.second.kind == Value::t_float) {
            cJSON_AddItemToObject(node, item.first.c_str(), cJSON_CreateDouble(item.second.fValue));
        }
        else {
            cJSON_AddStringToObject(node, item.first.c_str(), item.second.asString().c_str());
        }
    }
    Action *action;
    if ((action = m->executingCommand())) {
        std::stringstream ss;
        ss << *action;
        cJSON_AddStringToObject(node, "executing", ss.str().c_str());
    }

    if (m->enabled()) {
        cJSON_AddTrueToObject(node, "enabled");
    }
    else {
        cJSON_AddFalseToObject(node, "enabled");
    }
    if (!m->io_interface) {
        size_t len = m->getCurrent().getName().length() + 1;
        char *cs = (char *)malloc(len);
        memcpy(cs, m->getCurrentStateString(), len);
        cJSON_AddStringToObject(node, "state", cs);
        free(cs);
    }
    else {
        IOComponent *device = IOComponent::lookup_device(m->getName());
        if (device) {
            const char *state = device->getStateString();
            cJSON_AddStringToObject(node, "state", state);
        }
    }
    if (m->commands.size()) {
        std::stringstream cmds;
        std::pair<std::string, MachineCommand *> cmd;
        const char *delim = "";
        BOOST_FOREACH (cmd, m->commands) {
            cmds << delim << cmd.first;
            delim = ",";
        }
        cmds << std::flush;
        cJSON_AddStringToObject(node, "commands", cmds.str().c_str());
    }
    /*
        if (m->receives_functions.size()) {
            std::stringstream cmds;
            std::pair<Message, MachineCommand*> rcv;
            const char *delim = "";
            BOOST_FOREACH(rcv, m->receives_functions) {
                cmds << delim << rcv.first;
                delim = ",";
            }
            cmds << std::flush;
            cJSON_AddStringToObject(node, "receives", cmds.str().c_str());
        }
    */
    if (m->properties.size()) {
        std::stringstream props;
        SymbolTableConstIterator st_iter = m->properties.begin();
        int count = 0;
        const char *delim = "";
        while (st_iter != m->properties.end()) {
            std::pair<std::string, Value> prop = *st_iter++;
            if (IODCommandListJSON::no_display.count(prop.first)) {
                continue;
            }
            props << delim << prop.first;
            delim = ",";
            ++count;
        }
        if ((action = m->executingCommand())) {
            props << delim << "executing";
        }
        props << std::flush;
        if (count) {
            cJSON_AddStringToObject(node, "display", props.str().c_str());
        }
    }
    return node;
}

bool IODCommandListJSON::run(std::vector<Value> &params) {
    cJSON *root = cJSON_CreateArray();
    Value tab("");
    bool limited = false;
    if (params.size() > 2) {
        tab = params[2];
        limited = true;
    }
    std::map<std::string, MachineInstance *>::const_iterator iter = machines.begin();
    while (iter != machines.end()) {
        MachineInstance *m = (*iter++).second;
        if (!limited || (limited && m->properties.lookup("tab") == tab)) {
            cJSON_AddItemToArray(root, printMachineInstanceToJSON(m));
            for (unsigned int idx = 0; idx < m->locals.size(); ++idx) {
                const Parameter &p = m->locals[idx];
                cJSON_AddItemToArray(root, printMachineInstanceToJSON(p.machine, m->getName()));
            }
        }
    }
    char *res = cJSON_Print(root);
    cJSON_Delete(root);
    bool done;
    if (res) {
        result_str = res;
        free(res);
        done = true;
    }
    else {
        error_str = "JSON error";
        done = false;
    }
    return done;
}

/*
    send a message. The message may be in one of the forms:
        machine-object.command or
        object.command
*/
bool IODCommandSend::run(std::vector<Value> &params) {

    MachineInstance *m = 0;
    std::string machine_name;
    std::string command;

    // the ROUTE command may have been used to reroute this message:
    if (params.size() == 2) {
        command = params[1].asString();
        if (message_handlers.count(command)) {
            const std::string &name = message_handlers[command];
            m = MachineInstance::find(name.c_str());
            if (m) {
                machine_name = m->fullName();
            }
        }
    }

    // we may have found the target machine already in the
    // message routing table. if not, continue searching
    if (!m) {
        if ((params.size() != 2 && params.size() != 4) ||
            (params.size() == 4 && params[2] != "TO" && params[2] != "to")) {
            error_str = "Usage: SEND machine.command | SEND command TO machine ";
            return false;
        }

        // if the SEND machine.command syntax was used, we keep everything after the last '.'
        // if SEND command TO machine was used, we don't touch the command string
        if (params.size() == 2) {
            machine_name = params[1].asString();
            command = params[1].asString();

            size_t sep = command.rfind('.');
            if (sep != std::string::npos) {
                command.erase(0, sep + 1);
                machine_name.erase(sep);
            }
        }
        else if (params.size() == 4) {
            command = params[1].asString();
            machine_name = params[3].asString();
        }
        size_t pos = machine_name.find('-');
        if (pos != std::string::npos) {
            std::string submachine = machine_name.substr(pos + 1);
            machine_name.erase(pos);
            pos = submachine.find('.');
            if (pos != std::string::npos) {
                submachine.erase(pos);
            }
            MachineInstance *owner = MachineInstance::find(machine_name.c_str());
            if (!owner) {
                error_str = "could not find machine";
                return false;
            }
            m = owner->lookup(submachine); // find the actual device
        }
        else {
            m = MachineInstance::find(machine_name.c_str());
        }
    }
    std::stringstream ss;
    if (m) {
        m->sendMessageToReceiver(command.c_str(), m, false);
        if (m->_type == "LIST") {
            for (unsigned int i = 0; i < m->parameters.size(); ++i) {
                MachineInstance *entry = m->parameters[i].machine;
                if (entry) {
                    m->sendMessageToReceiver(command.c_str(), entry);
                }
            }
        }
        else if (m->_type == "REFERENCE" && m->locals.size()) {
            for (unsigned int i = 0; i < m->locals.size(); ++i) {
                MachineInstance *entry = m->locals[i].machine;
                if (entry) {
                    m->sendMessageToReceiver(command.c_str(), entry);
                }
            }
        }
        ss << command << " sent to " << m->getName() << std::flush;
        result_str = ss.str();
        return true;
    }
    else {
        ss << "Could not find machine " << machine_name << " for command " << command << std::flush;
    }
    error_str = ss.str();
    return false;
}

bool IODCommandData::run(std::vector<Value> &params) {
    //if (params.size() == 4) {
    MachineInstance *m = MachineInstance::find(params[1].asString().c_str());
    if (m && m->_type == "LIST") {

        for (unsigned int i = 2; i < params.size(); ++i) {
            m->addParameter(params[i]);
        }

        result_str = "OK";
        return true;
    }
    else {
        std::string err = "Unknown device: ";
        err = err + params[1].asString() + "\n";
        error_str = err.c_str();
        return false;
    }
    /*  }
        else {
        std::stringstream ss;
        ss << "Unrecognised parameters in ";
        std::ostream_iterator<std::string> out(ss, " ");
        std::copy(params.begin(), params.end(), out);
        ss << ".  Usage: PROPERTY property_name value";
        error_str = ss.str();
        return false;
        }
    */
}

bool IODCommandShowMessages::run(std::vector<Value> &params) {
    MessageLog *log = MessageLog::instance();
    // the CLEAR MESSAGES command is routed here by the client interface..
    if (params[0].asString() == "CLEAR") {
        MessageLog::instance()->purge();
        result_str = "OK";
        return true;
    }

    bool use_json = params.size() >= 2 && params[1].asString() == "JSON";
    long num = 0;
    unsigned int idx = 1;
    if (params.size() > idx && params[idx].asString() == "JSON") {
        use_json = true;
        ++idx;
    }
    if (params.size() > idx && params[idx].asInteger(num)) {
        ++idx;
    }

    cJSON *result = log->toJSON((unsigned int)num);
    if (result) {
        if (use_json) {
            char *text = cJSON_Print(result);
            result_str = text;
            free(text);
        }
        else {
            char *res = log->toString((unsigned int)num);
            result_str = res;
            free(res);
        }
        cJSON_Delete(result);
        return true;
    }
    else {
        error_str = "Failed to read the error log";
        return false;
    }
}

bool IODCommandTriggers::run(std::vector<Value> &params) {
    char *triggers = Trigger::getTriggers();
    if (triggers) {
        result_str = triggers;
        delete[] triggers;
    }
    else {
        result_str = "";
    }
    return true;
}

bool IODCommandNotice::run(std::vector<Value> &params) {
    std::stringstream msg;
    unsigned int i = 0;
    for (; i < params.size() - 1; ++i) {
        msg << params.at(i) << " ";
    }
    msg << params.at(i);
    char *msg_str = strdup(msg.str().c_str());
    MessageLog::instance()->add(msg_str);
    free(msg_str);
    result_str = "OK";
    return true;
}

bool IODCommandQuit::run(std::vector<Value> &params) {
    program_done = true;
    std::stringstream ss;
    ss << "quitting ";
    std::ostream_iterator<std::string> oi(ss, " ");
    ss << std::flush;
    result_str = ss.str();
    return true;
}

bool IODCommandHelp::run(std::vector<Value> &params) {
    std::stringstream ss;
    ss << "Commands: \n"
       << "DEBUG machine on|off\n"
       << "DEBUG debug_group on|off\n"
       << "DESCRIBE machine_name [JSON]\n"
       << "DISABLE machine_name\n"
       << "EC command\n"
       << "ENABLE machine_name\n"
       << "GET machine_name\n"
       << "LIST JSON\n"
       << "LIST\n"
       << "MASTER\n"
       << "MODBUS EXPORT\n"
       << "MODBUS group address new_value\n"
       << "MODBUS REFRESH\n"
       << "PROPERTY machine_name property new_value\n"
       << "QUIT\n"
       << "RESUME machine_name\n"
       << "SEND command\n"
       << "SET machine_name TO state_name\n"
       << "SLAVES\n"
       << "TOGGLE output_name\n"
       << "ERRORS [JSON]\n";
    std::string s = ss.str();
    result_str = s;
    return true;
}

bool IODCommandInfo::run(std::vector<Value> &params) {
    const char *device = device_name();
    if (!device) {
        device = "localhost";
    }
    const char *version = "version: 0.0";
    char *res = 0;
    if (params.size() > 1 && params[1] == "JSON") {
        cJSON *info = cJSON_CreateArray();
        cJSON_AddStringToObject(info, "NAME", device);
        cJSON_AddStringToObject(info, "VERSION", version);
        res = cJSON_Print(info);
        cJSON_Delete(info);
    }
    else {
        res = (char *)malloc(100);
        snprintf(res, 100, "DEVICE:\t%s\nVERSION\t%s\n", device, version);
    }
    if (res) {
        result_str = res;
        free(res);
        return true;
    }
    else {
        error_str = "No System Information available";
        return false;
    }
}

bool IODCommandTracing::run(std::vector<Value> &params) {
    if (params.size() != 2) {
        std::stringstream ss;
        ss << "usage: TRACING ON|OFF" << std::flush;
        std::string s = ss.str();
        error_str = s;
        return false;
    }
    if (params[1] == "ON") {
        enable_tracing(true);
    }
    else {
        enable_tracing(false);
    }
    result_str = "OK";
    return true;
}

bool IODCommandDebugShow::run(std::vector<Value> &params) {
    std::stringstream ss;
    ss << "Debug status: \n" << *LogState::instance() << "\n" << std::flush;
    std::string s = ss.str();
    result_str = s;
    return true;
}

bool IODCommandDebug::run(std::vector<Value> &params) {
    if (params.size() != 3) {
        std::stringstream ss;
        ss << "usage: DEBUG debug_group on|off\n\nDebug groups: \n"
           << *LogState::instance() << std::flush;
        std::string s = ss.str();
        error_str = s;
        return false;
    }
    int group = LogState::instance()->lookup(params[1].asString());
    if (group == 0) {
        std::map<std::string, MachineInstance *>::iterator found =
            machines.find(params[1].asString());
        if (found != machines.end()) {
            MachineInstance *mi = (*found).second;
            if (params[2] == "on" || params[2] == "ON") {
                mi->setDebug(true);
            }
            else {
                mi->setDebug(false);
            }
        }
        else {
            error_str = "Unknown debug group";
            return false;
        }
    }
    if (params[2] == "on" || params[2] == "ON") {
        LogState::instance()->insert(group);
        if (params[1] == "DEBUG_MODBUS") {
            ModbusAddress::message("DEBUG ON");
        }
    }
    else if (params[2] == "off" || params[2] == "OFF") {
        LogState::instance()->erase(group);
        if (params[1] == "DEBUG_MODBUS") {
            ModbusAddress::message("DEBUG OFF");
        }
    }
    else {
        error_str = "Please use 'on' or 'off' in all upper- or lowercase";
        return false;
    }
    result_str = "OK";
    return true;
}

bool IODCommandModbus::run(std::vector<Value> &params) {
    if (params.size() != 4) {
        error_str = "Usage: MODBUS group address value";
        std::cout << error_str << " " << params[1] << "\n";
        return false;
    }
    long group, address;
    if (!params[1].asInteger(group) || !params[2].asInteger(address)) {
        std::stringstream ss;
        ss << "malformed modbus command: " << params[0] << " " << params[1] << " " << params[2]
           << " " << params[3];
        error_str = ss.str();
        MessageLog::instance()->add(ss.str().c_str());
        return false;
    }
    DBG_MODBUS << "modbus group: " << group << " addresss " << address << " value " << params[3]
               << "\n";
    ModbusAddress found = ModbusAddress::lookup((int)group, (int)address);
    if (found.getGroup() != ModbusAddress::none) {
        if (found.getOwner()) {
            // the address found will refer to the base address, so we provide the actual offset
            assert(address == found.getAddress());
            if (params[3].kind == Value::t_integer) {
                found.getOwner()->modbusUpdated(found, (int)(address - found.getAddress()),
                                                (int)params[3].iValue);
            }
            else if (params[3].kind == Value::t_float) {
                found.getOwner()->modbusUpdated(found, (int)(address - found.getAddress()),
                                                (float)params[3].fValue);
            }
            else if (params[3].kind == Value::t_bool) {
                found.getOwner()->modbusUpdated(found, (int)(address - found.getAddress()),
                                                (params[3].bValue) ? 1 : 0);
            }
            else if (params[3].kind == Value::t_string || params[3].kind == Value::t_symbol) {
                long val;
                if (params[3].asInteger(val)) {
                    found.getOwner()->modbusUpdated(found, (int)(address - found.getAddress()),
                                                    (int)val);
                }
                else {
                    found.getOwner()->modbusUpdated(found, (int)(address - found.getAddress()),
                                                    params[3].sValue.c_str());
                }
            }
            else {
                std::stringstream ss;
                ss << "unexpected value type " << params[3].kind << " for modbus value\n";
                std::cout << ss.str() << "\n";
                error_str = ss.str();
                return false;
            }
        }
        else {
            DBG_MODBUS << "no owner for Modbus address " << found << "\n";
            error_str = "Modbus: ignoring unregistered address\n";
            std::cout << error_str << "\n";
            return false;
        }
    }
    else {
        std::stringstream ss;
        ss << "failed to find Modbus Address matching group: " << group << ", address: " << address;
        error_str = ss.str();
        std::cout << error_str << "\n";
        return false;
    }
    result_str = "OK";
    return true;
}

bool IODCommandModbusExport::run(std::vector<Value> &params) {

    const char *file_name = modbus_map();
    if (params.size() == 3) {
        file_name = params[2].asString().c_str();
    }
    const char *backup_file_name = "modbus_mappings.bak";
    if (rename(file_name, backup_file_name)) {
        std::cerr << "file rename error: " << strerror(errno) << "\n";
    }
    std::list<MachineInstance *>::iterator m_iter = MachineInstance::begin();
    std::ofstream out(file_name);
    if (!out) {
        error_str = "not able to open mapping file for write";
        return false;
    }
    while (m_iter != MachineInstance::end()) {
        (*m_iter)->exportModbusMapping(out);
        m_iter++;
    }
    out.close();
    result_str = "OK";
    return true;
}

bool IODCommandPersistentState::run(std::vector<Value> &params) {
    cJSON *result = cJSON_CreateArray();
    std::list<MachineInstance *>::iterator m_iter;
    m_iter = MachineInstance::begin();
    while (m_iter != MachineInstance::end()) {
        MachineInstance *m = *m_iter++;
        if (m && m->isPersistent()) {
            std::string fnam = m->fullName();
            SymbolTableConstIterator props_i = m->properties.begin();
            while (props_i != m->properties.end()) {
                std::pair<std::string, Value> item = *props_i++;
                cJSON *json_item = cJSON_CreateArray();
                cJSON_AddItemToArray(json_item, cJSON_CreateString(fnam.c_str()));
                cJSON_AddItemToArray(json_item, cJSON_CreateString(item.first.c_str()));
                MessageEncoding::addValueToJSONArray(json_item, item.second);
                cJSON_AddItemToArray(result, json_item);
            }
        }
    }
    char *cstr_result = cJSON_PrintUnformatted(result);
    cJSON_Delete(result);
    result_str = cstr_result;
    free(cstr_result);
    return true;
}

bool IODCommandSchedulerState::run(std::vector<Value> &params) {
    std::stringstream ss;
    ss << "Status: " << Scheduler::instance()->getStatus() << "\n";
    if (!Scheduler::instance()->empty()) {
        ss << "next: " << *(Scheduler::instance()->next());
    }
    result_str = ss.str();
    return true;
}

bool IODCommandModbusRefresh::run(std::vector<Value> &params) {
    std::list<MachineInstance *>::iterator m_iter = MachineInstance::begin();
    cJSON *result = cJSON_CreateArray();
    while (m_iter != MachineInstance::end()) {
        (*m_iter)->refreshModbus(result);
        m_iter++;
    }
    //std::string s(out.str());
    if (result) {
        char *r_str = cJSON_Print(result);
        result_str = r_str;
        free(r_str);
        cJSON_Delete(result);
        return true;
    }
    else {
        error_str = "Failed to load modbus initial data";
        return false;
    }
}

bool IODCommandPerformance::run(std::vector<Value> &params) {
    std::list<MachineInstance *>::iterator m_iter = MachineInstance::begin();
    cJSON *result = cJSON_CreateArray();
    while (m_iter != MachineInstance::end()) {
        MachineInstance *m = *m_iter++;
        if (m->stable_states_stats.getCount()) {
            cJSON *stat = cJSON_CreateArray();
            cJSON_AddItemToArray(stat, cJSON_CreateString(m->fullName().c_str()));
            cJSON_AddItemToArray(stat,
                                 cJSON_CreateString(m->stable_states_stats.getName().c_str()));
            m->stable_states_stats.reportArray(stat);
            cJSON_AddItemToArray(result, stat);
        }
        if (m->message_handling_stats.getCount()) {
            cJSON *stat = cJSON_CreateArray();
            cJSON_AddItemToArray(stat, cJSON_CreateString(m->fullName().c_str()));
            cJSON_AddItemToArray(stat,
                                 cJSON_CreateString(m->message_handling_stats.getName().c_str()));
            m->message_handling_stats.reportArray(stat);
            cJSON_AddItemToArray(result, stat);
        }
    }
    {
        cJSON *stats = cJSON_CreateArray();
        Statistic::reportAll(stats);
        cJSON_AddItemToArray(result, stats);
    }

    //std::string s(out.str());
    if (result) {
        char *r_str = cJSON_Print(result);
        result_str = r_str;
        free(r_str);
        cJSON_Delete(result);
        return true;
    }
    else {
        error_str = "Failed to load modbus initial data";
        return false;
    }
}

bool IODCommandChannels::run(std::vector<Value> &params) {
    std::map<std::string, Channel *> *channels = Channel::channels();
    if (!channels) {
        result_str = "No channels";
        return true;
    }

    std::string result;
    std::map<std::string, Channel *>::iterator iter = channels->begin();
    while (iter != channels->end()) {
        result += (*iter).first + " " +
                  (*iter).second->getCurrentStateString()
                  //          + " " + ((*iter).second->doesMonitor() ? "M" : "")
                  //          + " " + ((*iter).second->doesShare() ? "S" : "")
                  //          + " " + ((*iter).second->doesUpdate() ? "U" : "")
                  + "\n";
        iter++;
    }
    result_str = result;
    return true;
}

bool IODCommandChannelRefresh::run(std::vector<Value> &params) {
    std::map<std::string, Channel *> *channels = Channel::channels();
    if (!channels) {
        result_str = "[]";
        return true;
    }

    Channel *chn = 0;
    try {
        chn = channels->at(params[1].asString());
    }
    catch (const std::exception &ex) {
        error_str = "No such channel";
        return false;
    }

    cJSON *result = cJSON_CreateArray();

    // TBD this only supports modbus master channels at this point
    if (chn->definition()->monitorsLinked()) {
        std::set<MachineInstance *>::const_iterator iter = chn->channelMachines().begin();
        while (iter != chn->channelMachines().end()) {
            MachineInstance *m = *iter++;
            cJSON *obj = cJSON_CreateObject();
            cJSON_AddStringToObject(obj, "name", m->getName().c_str());
            cJSON_AddStringToObject(obj, "address", m->parameters[1].val.asString().c_str());
            long val;
            if (m->parameters.size() > 2 && m->parameters[2].val.asInteger(val)) {
                cJSON_AddNumberToObject(obj, "length", val);
            }
            else {
                cJSON_AddNumberToObject(obj, "length", 1);
            }
            cJSON_AddStringToObject(obj, "type", m->_type.c_str());
            const Value &format = m->getValue("format");
            if (format != SymbolTable::Null) {
                cJSON_AddStringToObject(obj, "format", format.asString().c_str());
            }
            cJSON_AddItemToArray(result, obj);
        }
    }
    char *r_str = cJSON_PrintUnformatted(result);
    result_str = r_str;
    free(r_str);
    cJSON_Delete(result);
    return true;
};

bool IODCommandChannel::run(std::vector<Value> &params) {
    if (params.size() >= 2) {
        Value ch_name = params[1];
        Channel *chn = Channel::find(ch_name.asString());
        if (chn) {
            size_t n = params.size();
            if (n == 3) {
                if (params[2] == "REMOVE") {
                    delete chn;
                    result_str = "OK";
                    return true;
                }
            }
            if (n == 5 && params[2] == "ADD" && params[3] == "MONITOR") {
                chn->addMonitor(params[4].asString().c_str());
                result_str = "OK";
                return true;
            }
            if (n == 6 && params[2] == "ADD" && params[3] == "MONITOR" && params[4] == "PATTERN") {
                chn->addMonitorPattern(params[5].asString().c_str());
                result_str = "OK";
                return true;
            }
            if (n == 7 && params[2] == "ADD" && params[3] == "MONITOR" && params[4] == "PROPERTY") {
                chn->addMonitorProperty(params[5].asString().c_str(), params[6]);
                result_str = "OK";
                return true;
            }
            if (n == 5 && params[2] == "REMOVE" && params[3] == "MONITOR") {
                chn->removeMonitor(params[4].asString().c_str());
                result_str = "OK";
                return true;
            }
            if (n == 6 && params[2] == "REMOVE" && params[3] == "MONITOR" &&
                params[4] == "PATTERN") {
                chn->removeMonitorPattern(params[5].asString().c_str());
                result_str = "OK";
                return true;
            }
            if (n == 7 && params[2] == "REMOVE" && params[3] == "MONITOR" &&
                params[5] == "PROPERTY") {
                chn->removeMonitorProperty(params[5].asString().c_str(), params[6]);
                result_str = "OK";
                return true;
            }
        }
        else {
            if (ch_name == "PERSISTENCE_CHANNEL") {
                if (!persistent_store()) {
                    char buf[100];
                    snprintf(buf, 100, "Persistence is not configured for channel %s",
                             ch_name.asString().c_str());
                    MessageLog::instance()->add(buf);
                    error_str = MessageEncoding::encodeError(buf);
                    return false;
                }
            }
            ChannelDefinition *defn = ChannelDefinition::find(ch_name.asString().c_str());
            if (!defn) {
                char buf[100];
                snprintf(buf, 100, "No such channel: %s", ch_name.asString().c_str());
                error_str = MessageEncoding::encodeError(buf);
                return false;
            }

            chn = Channel::findByType(ch_name.asString());
            if (!chn) {
                std::cout << "no channel found, creating one\n";
                long port = 0;

                //Value portval = (defn->properties.exists("port")) ? defn->properties.lookup("port") : SymbolTable::Null;
                Value portval = defn->getValue("port");
                std::cout << "default port for channel: " << portval << "\n";
                if (portval == SymbolTable::Null || !portval.asInteger(port)) {
                    port = Channel::uniquePort();
                }
                else {
                    NB_MSG << " using default channel port: " << port << " for channel " << ch_name
                           << "\n";
                }
                while (true) {
                    try {
                        std::cout << "instantiating a channel on port " << port << "\n";
                        chn = defn->instantiate(port);
                        assert(chn);
                        if (ch_name == "PERSISTENCE_CHANNEL") {
                            chn->setValue("PersistentStore",
                                          Value(persistent_store(), Value::t_string));
                        }
                        chn->start();
                        chn->enable();
                        break;
                    }
                    catch (const zmq::error_t &err) {
                        if (zmq_errno() == EADDRINUSE) {
                            NB_MSG << "address is in use\n";
                        }
                        error_str = zmq_strerror(zmq_errno());
                        std::cerr << error_str << "\n";
                        exit(1);
                    }
                }
            }
        }
        cJSON *res_json = cJSON_CreateObject();
        cJSON_AddNumberToObject(res_json, "port", chn->getPort());
        cJSON_AddStringToObject(res_json, "name", chn->getName().c_str());
        cJSON_AddNumberToObject(res_json, "authority", chn->definition()->getAuthority());
        char *res = cJSON_Print(res_json);
        result_str = res;
        free(res);
        cJSON_Delete(res_json);
        return true;
    }
    std::stringstream ss;
    ss << "usage: CHANNEL name [ REMOVE| ADD MONITOR [ ( PATTERN string | PROPERTY string string ) ]  ]";
    for (unsigned int i = 0; i < params.size() - 1; ++i) {
        ss << params[i] << " ";
    }
    ss << params[params.size() - 1];
    char *msg = strdup(ss.str().c_str());
    error_str = msg;
    free(msg);
    return false;
}

bool IODCommandUnknown::run(std::vector<Value> &params) {
    if (params.size() == 0) {
        result_str = "OK";
        return true;
    }
    std::stringstream ss;
    for (unsigned int i = 0; i < params.size() - 1; ++i) {
        ss << params[i] << " ";
    }
    ss << params[params.size() - 1];

    char *msg = strdup(ss.str().c_str());
    if (message_handlers.count(msg)) {
        const std::string &name = message_handlers[ss.str()];
        MachineInstance *m = MachineInstance::find(name.c_str());
        if (m) {
            m->sendMessageToReceiver(msg, m);
            result_str = "OK";
            return true;
        }
    }
    ss << ": Unknown command: ";
    free(msg);
    msg = strdup(ss.str().c_str());
    error_str = msg;
    free(msg);
    return false;
}

// This command is only here for testing recovery code it blocks command processing for 10 seconds

bool IODCommandFreeze::run(std::vector<Value> &params) {
    uint64_t start = microsecs();
    uint64_t now = start;
    while (now - start < 10000000) {
        usleep(100000);
    }
    result_str = "OK";
    return true;
}

extern bool program_done;
extern bool all_ok;

bool IODCommandToggleEtherCAT::run(std::vector<Value> &params) {
    uint64_t start = microsecs();
    all_ok = !all_ok;
    result_str = "OK";
    return true;
}

bool IODCommandShutdown::run(std::vector<Value> &params) {
    uint64_t start = microsecs();
    program_done = true;
    result_str = "OK";
    return true;
}

bool IODCommandSDO::run(std::vector<Value> &params) {
#ifndef EC_SIMULATOR
#ifdef USE_SDO
#if 0
    if (params.size() == 2) {
        Value entry_name = params[1];
        SDOEntry *entry = SDOEntry::find(entry_name.sValue);
        entry->setOperation(SDOEntry::READ);
        ECInterface::instance()->queueInitialisationRequest(entry);
        result_str = "OK";
        return true;
    }
    else
#endif
    if (params.size() == 3) {
        Value entry_name = params[1];
        Value new_value = params[2];
        SDOEntry *entry = SDOEntry::find(entry_name.sValue);
        long value;
        if (entry && new_value.asInteger(value)) {
#if 0
                size_t len = entry->getSize();
                if (len == 1) {
                    entry->setData((uint8_t)(value & 0xff));
                }
                else if (len == 2) {
                    entry->setData((uint16_t)(value & 0xffff));
                }
                else if (len == 4) {
                    entry->setData((uint32_t)(value & 0xffffffff));
                }
                else {
                    error_str = strdup("Unsupported size");
                    return false;
                }
                entry->setOperation(SDOEntry::WRITE);
#endif
            ECInterface::instance()->queueInitialisationRequest(entry, new_value);
            result_str = "OK";
            return true;
        }
        else {
            std::stringstream ss;
            ss << "No Entry named: " << entry_name.sValue;
            error_str = strdup(ss.str().c_str());
            return false;
        }
    }
    else {
        error_str = "usage: SDO entry new_value";
        return false;
    }
#else  //USE_SDO
    error_str = "Command disabled";
    return false;
#endif //USE_SDO
#else
    error_str = "Command disabled";
    return false;
#endif
}

/*
    void sendMessage(zmq::socket_t &socket, const char *message) {
    const char *msg = (message) ? message : "";
    size_t len = strlen(msg);
    zmq::message_t reply (len);
    memcpy ((void *) reply.data (), msg, len);
    while (true) {
        try {
            socket.send (reply);
            //std::cout << "sent: " << message << "\n";
            break;
        } catch (const zmq::error_t &e) {
            if (errno == EAGAIN) continue;
            std::cerr << "Error: " << errno << " " << zmq_strerror(errno) << "\n";
        }
    }
    }
*/
