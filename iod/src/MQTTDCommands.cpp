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

#include "MQTTDCommands.h"
#include "DebugExtra.h"
#include "Logger.h"
#include "MQTTInterface.h"
#include "Statistic.h"
#include "Statistics.h"
#include "cJSON.h"
#include "options.h"
#include <fstream>
#include <list>

extern Statistics *statistics;

std::map<std::string, std::string> message_handlers;

extern bool program_done;

bool MQTTDCommandGetStatus::run(std::vector<std::string> &params) {
    bool done = false;
    if (params.size() == 3) {
        MQTTModule *machine = MQTTInterface::findModule(params[1].c_str());
        if (machine) {
            if (machine->publishes(params[2])) {
                done = true;
                result_str = machine->getStateString(params[2]);
            }
        }
        else {
            error_str = "Not Found";
        }
    }
    return done;
}

bool MQTTDCommandSetStatus::run(std::vector<std::string> &params) {
    if (params.size() == 4) {
        std::string ds = params[1];
        MQTTModule *device = MQTTInterface::instance()->findModule(ds);
        if (device) {
            if (device->publishes(params[2])) {
                device->publish(params[2], params[3]);
                result_str = device->getStateString(params[2]);
                return true;
            }
        }
        //  Send reply back to client
        const char *msg_text = "Not found: ";
        size_t len = strlen(msg_text) + ds.length();
        char *text = (char *)malloc(len + 1);
        snprintf(text, len + 1, "%s%s", msg_text, ds.c_str());
        error_str = text;
        return false;
    }
    error_str = "Usage: SET device topic value";
    return false;
}

bool MQTTDCommandDescribe::run(std::vector<std::string> &params) {
    std::cout << "received iod command Describe " << params[1] << "\n";
    bool use_json = false;
    if (params.size() == 3 && params[2] != "JSON") {
        error_str = "Usage: DESCRIBE machine [JSON]";
        return false;
    }
    if (params.size() == 2 || params.size() == 3) {
        MQTTModule *m = MQTTInterface::instance()->findModule(params[1]);
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
            delete res;
        }
        else {
            result_str = ss.str();
        }
        return true;
    }
    error_str = "Failed to find machine";
    return false;
}

bool MQTTDCommandProperty::run(std::vector<std::string> &params) {
    if (params.size() == 4) {
        MQTTModule *m = MQTTInterface::instance()->findModule(params[1]);
        if (m) {
            m->publish(params[2], params[3].c_str());

            result_str = "OK";
            return true;
        }
        else {
            error_str = "Unknown device";
            return false;
        }
    }
    else {
        std::stringstream ss;
        ss << "Unrecognised parameters in ";
        std::ostream_iterator<std::string> out(ss, " ");
        std::copy(params.begin(), params.end(), out);
        ss << ".  Usage: PROPERTY property_name value";
        error_str = ss.str();
        return false;
    }
}

bool MQTTDCommandList::run(std::vector<std::string> &params) {
    std::ostringstream ss;
    std::list<MQTTModule *>::const_iterator iter = MQTTInterface::modules.begin();
    while (iter != MQTTInterface::modules.end()) {
        MQTTModule *m = *iter++;
        ss << (m->name) << "\n";
    }
    result_str = ss.str();
    return true;
}

std::set<std::string> MQTTDCommandListJSON::no_display;

cJSON *printMachineInstanceToJSON(MQTTModule *m, std::string prefix = "") {
    cJSON *node = cJSON_CreateObject();
    std::string name_str = m->name;
    if (prefix.length() != 0) {
        std::stringstream ss;
        ss << prefix << '-' << m->name << std::flush;
        name_str = ss.str();
    }
    cJSON_AddStringToObject(node, "name", name_str.c_str());
    SymbolTableConstIterator st_iter = m->topics.begin();
    while (st_iter != m->topics.end()) {
        std::pair<std::string, Value> item(*st_iter++);
        if (item.second.kind == Value::t_integer) {
            cJSON_AddNumberToObject(node, item.first.c_str(), item.second.iValue);
        }
        else {
            cJSON_AddStringToObject(node, item.first.c_str(), item.second.asString().c_str());
        }
        //std::cout << item.first << ": " <<  item.second.asString() << "\n";
    }
    return node;
}

bool MQTTDCommandListJSON::run(std::vector<std::string> &params) {
    cJSON *root = cJSON_CreateArray();
    Value tab("");
    std::list<MQTTModule *>::const_iterator iter = MQTTInterface::modules.begin();
    while (iter != MQTTInterface::modules.end()) {
        MQTTModule *m = *iter++;
        cJSON_AddItemToArray(root, printMachineInstanceToJSON(m));
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

bool MQTTDCommandQuit::run(std::vector<std::string> &params) {
    program_done = true;
    std::stringstream ss;
    ss << "quitting ";
    std::ostream_iterator<std::string> oi(ss, " ");
    ss << std::flush;
    result_str = ss.str();
    return true;
}

bool MQTTDCommandHelp::run(std::vector<std::string> &params) {
    std::stringstream ss;
    ss << "Commands: \n"
       << "DEBUG machine on|off\n"
       << "DEBUG debug_group on|off\n"
       << "DESCRIBE machine_name [JSON]\n"
       << "GET machine_name\n"
       << "LIST JSON\n"
       << "LIST\n"
       << "PROPERTY machine_name property new_value\n"
       << "QUIT\n"
       << "RESUME machine_name\n"
       << "SEND command\n"
       << "SET machine_name TO state_name\n"
       << "SLAVES\n";
    std::string s = ss.str();
    result_str = s;
    return true;
}

bool MQTTDCommandDebugShow::run(std::vector<std::string> &params) {
    std::stringstream ss;
    ss << "Debug status: \n" << *LogState::instance() << "\n" << std::flush;
    std::string s = ss.str();
    result_str = s;
    return true;
}

bool MQTTDCommandUnknown::run(std::vector<std::string> &params) {
    std::stringstream ss;
    std::ostream_iterator<std::string> oi(ss, "");
    std::copy(params.begin(), params.end(), oi);

    ss << ": Unknown command";
    error_str = ss.str();
    return false;
}

static void sendMessage(zmq::socket_t &socket, const char *message) {
    const char *msg = (message) ? message : "";
    size_t len = strlen(msg);
    zmq::message_t reply(len);
    memcpy((void *)reply.data(), msg, len);
    socket.send(reply);
}
