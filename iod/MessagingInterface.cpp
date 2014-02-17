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

#include "MessagingInterface.h"
#include <iostream>
#include <exception>
#include <math.h>
#include <zmq.hpp>
#include "Logger.h"
#include "DebugExtra.h"
#include "cJSON.h"
#ifdef DYNAMIC_VALUES
#include "dynamic_value.h"
#endif
#include "symboltable.h"
#include "options.h"

MessagingInterface *MessagingInterface::current = 0;
std::map<std::string, MessagingInterface *>MessagingInterface::interfaces;

MessagingInterface *MessagingInterface::getCurrent() {
    if (MessagingInterface::current == 0) {
        MessagingInterface::current = new MessagingInterface(1, publisher_port());
        usleep(200000); // give current subscribers time to notice us
    }
    return MessagingInterface::current;
}

MessagingInterface *MessagingInterface::create(std::string host, int port) {
    std::stringstream ss;
    ss << host << ":" << port;
    std::string id = ss.str();
    if (interfaces.count(id) == 0) {
        MessagingInterface *res = new MessagingInterface(host, port);
        interfaces[id] = res;
        return res;
    }
    else
        return interfaces[id];
}

MessagingInterface::MessagingInterface(int num_threads, int port) : Receiver("messaging_interface") {
    context = new zmq::context_t(num_threads);
    socket = new zmq::socket_t(*context, ZMQ_PUB);
    is_publisher = true;
    std::stringstream ss;
    ss << "tcp://*:" << port;
    url = ss.str();
    socket->bind(url.c_str());
}

MessagingInterface::MessagingInterface(std::string host, int port) :Receiver("messaging_interface") {
    if (host == "*") {
        context = new zmq::context_t(1);
        socket = new zmq::socket_t(*context, ZMQ_PUB);
        is_publisher = true;
        std::stringstream ss;
        ss << "tcp://*:" << port;
        url = ss.str();
        socket->bind(url.c_str());
    }
    else {
        context = new zmq::context_t(1);
        std::stringstream ss;
        ss << "tcp://" << host << ":" << port;
        url = ss.str();
        connect();
    }
}

void MessagingInterface::connect() {
    socket = new zmq::socket_t(*context, ZMQ_REQ);
    is_publisher = false;
    socket->connect(url.c_str());
    int linger = 0;
    socket->setsockopt (ZMQ_LINGER, &linger, sizeof (linger));
}

MessagingInterface::~MessagingInterface() {
    if (MessagingInterface::current == this) MessagingInterface::current = 0;
    delete socket;
    delete context;
}

bool MessagingInterface::receives(const Message&, Transmitter *t) {
    return true;
}
void MessagingInterface::handle(const Message&msg, Transmitter *from, bool needs_receipt ) {
    send(msg);
}


char *MessagingInterface::send(const char *txt) {
    if (!is_publisher){
        DBG_MESSAGING << "sending message " << txt << " on " << url << "\n";
    }
    else {
        DBG_MESSAGING << "sending message " << txt << "\n";
    }
    size_t len = strlen(txt);
	try {
	    zmq::message_t msg(len);
	    strncpy ((char *) msg.data(), txt, len);
	    socket->send(msg);
        if (!is_publisher) {
            bool expect_reply = true;
            while (expect_reply) {
                zmq::pollitem_t items[] = { { *socket, 0, ZMQ_POLLIN, 0 } };
                zmq::poll( &items[0], 1, 5000000);
                if (items[0].revents & ZMQ_POLLIN) {
                    zmq::message_t reply;
                    if (socket->recv(&reply)) {
                        len = reply.size();
                        char *data = (char *)malloc(len+1);
                        memcpy(data, reply.data(), len);
                        data[len] = 0;
                        std::cout << url << ": " << data << "\n";
                        return data;
                    }
                }
                else
                    expect_reply = false;
                    std::cerr << "abandoning message " << txt << "\n";
                    delete socket;
                    connect();
                }
        }
	}
	catch (std::exception e) {
        if (zmq_errno())
            std::cerr << "Exception when sending " << url << ": " << zmq_strerror(zmq_errno()) << "\n";
        else
            std::cerr << "Exception when sending " << url << ": " << e.what() << "\n";
	}
    return 0;
}

char *MessagingInterface::send(const Message&msg) {
    char *text = encodeCommand(msg.getText(), msg.getParams());
    char *res = send(text);
    free(text);
    return res;
}

static std::string valueType(const Value &v) {
    switch (v.kind) {
        case Value::t_symbol:
            return "NAME";
            break;
        case Value::t_string: return "STRING";
        case Value::t_integer: return "INTEGER";
        case Value::t_bool: return "BOOL";
#ifdef DYNAMIC_VALUES
        case Value::t_dynamic: {
            DynamicValue *dv = v.dynamicValue();
            Value v = dv->operator()();
            return valueType(v);
        }
#endif
        case Value::t_empty: return "";
        default:
            break;
    }
    return "";
}

static void addValueToJSONObject(cJSON *obj, const char *name, const Value &val) {
    switch (val.kind) {
        case Value::t_symbol:
        case Value::t_string:
            cJSON_AddStringToObject(obj, name, val.sValue.c_str());
            break;
        case Value::t_integer:
            cJSON_AddNumberToObject(obj, name, val.iValue);
            break;
        case Value::t_bool:
            if (val.bValue)
                cJSON_AddTrueToObject(obj, name);
            else
                cJSON_AddFalseToObject(obj, name);
            break;
#ifdef DYNAMIC_VALUES
        case Value::t_dynamic: {
            DynamicValue *dv = val.dynamicValue();
            if (dv) {
                Value v = dv->operator()();
                addValueToJSONObject(obj, name, v);
            }
            else {
                cJSON_AddNullToObject(obj, name);
            }
            break;
        }
#endif
        default:
            break;
    }
}

char *MessagingInterface::encodeCommand(std::string cmd, std::list<Value> *params) {
    cJSON *msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "command", cmd.c_str());
    cJSON *cjParams = cJSON_CreateArray();
    for (std::list<Value>::const_iterator iter = params->begin(); iter != params->end(); ) {
        const Value &val = *iter++;
        if (val != SymbolTable::Null) {
            cJSON *cjItem = cJSON_CreateObject();
            cJSON_AddStringToObject(cjItem, "type", valueType(val).c_str());
            addValueToJSONObject(cjItem, "value", val);
            cJSON_AddItemToArray(cjParams, cjItem);
        }
    }
    cJSON_AddItemToObject(msg, "params", cjParams);
    char *res = cJSON_PrintUnformatted(msg);
    cJSON_Delete(msg);
    return res;
}

char *MessagingInterface::encodeCommand(std::string cmd, Value p1, Value p2, Value p3) {
    std::list<Value>params;
    params.push_back(p1);
    params.push_back(p2);
    params.push_back(p3);
    return encodeCommand(cmd, &params);
}


char *MessagingInterface::sendCommand(std::string cmd, std::list<Value> *params) {
    char *request = encodeCommand(cmd, params);
    char *response = send(request);
    free(request);
    return response;
}

/* TBD, this may need to be optimised, one approach would be to use a 
 templating system; a property message looks like this:
 
 { "command":    "PROPERTY", "params":   
    [
         { "type":  "NAME", "value":    $1 },
         { "type":   "NAME", "value":   $2 },
         { "type":  TYPEOF($3), "value":  $3 }
     ] }
 
 */

char *MessagingInterface::sendCommand(std::string cmd, Value p1, Value p2, Value p3)
{
    std::list<Value>params;
    params.push_back(p1);
    params.push_back(p2);
    params.push_back(p3);
    return sendCommand(cmd, &params);
}

char *MessagingInterface::sendState(std::string cmd, std::string name, std::string state_name)
{
    cJSON *msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "command", cmd.c_str());
    cJSON *cjParams = cJSON_CreateArray();
    cJSON_AddStringToObject(cjParams, "name", name.c_str());
    cJSON_AddStringToObject(cjParams, "state", state_name.c_str());
    cJSON_AddItemToObject(msg, "params", cjParams);
    char *request = cJSON_PrintUnformatted(msg);
    cJSON_Delete(msg);
    char *response = send(request);
    free (request);
    return response;
}

Value MessagingInterface::valueFromJSONObject(cJSON *obj, cJSON *cjType) {
    if (!obj) return SymbolTable::Null;
    const char *type = 0;
    if (cjType) {
        assert(cjType->type == cJSON_String);
        type = cjType->valuestring;
    }
    else
        type = "STRING";
    Value res;
    if (obj->type == cJSON_String) {
        if (strcmp(type, "STRING") == 0)
            res = Value(obj->valuestring, Value::t_string);
        else
            res =  obj->valuestring;
    }
    else if (obj->type == cJSON_NULL) {
        res = SymbolTable::Null;
    }
    else if (obj->type == cJSON_True) {
        res = true;
    }
    else if (obj->type == cJSON_False) {
        res = false;
    }
    else if (obj->type == cJSON_Number) {
        if (obj->valueNumber.kind == cJSON_Number_int_t)
            res = obj->valueNumber.val._int;
        else
            res = (long)floor(obj->valueNumber.val._double + 0.5);
    }
    else
        res = SymbolTable::Null;
    return res;
}

bool MessagingInterface::getCommand(const char *msg, std::string &cmd, std::list<Value> **params)
{
    cJSON *obj = cJSON_Parse(msg);
    if (!obj) return false;
    {
        cJSON *command = cJSON_GetObjectItem(obj, "command");
        if (!command) goto failed_getCommand;
        if (command->type != cJSON_String) goto failed_getCommand;
        cmd = command->valuestring;
        if (cmd == "STATE") {
            cJSON *cjParams = cJSON_GetObjectItem(obj, "params");
            int num_params = cJSON_GetArraySize(cjParams);
            if (num_params) {
                *params = new std::list<Value>;
                for (int i=0; i<num_params; ++i) {
                    cJSON *item = cJSON_GetArrayItem(cjParams, i);
                    Value item_val = valueFromJSONObject(item, 0);
                    if (item_val != SymbolTable::Null) (*params)->push_back(item_val);
                }
            }
            else
                *params = NULL;
        }
        else {
            cJSON *cjParams = cJSON_GetObjectItem(obj, "params");
            int num_params = cJSON_GetArraySize(cjParams);
            if (num_params) {
                *params = new std::list<Value>;
                for (int i=0; i<num_params; ++i) {
                    cJSON *item = cJSON_GetArrayItem(cjParams, i);
                    cJSON *type = cJSON_GetObjectItem(item, "type");
                    cJSON *value = cJSON_GetObjectItem(item, "value");
                    Value item_val = valueFromJSONObject(value, type);
                    if (item_val != SymbolTable::Null) (*params)->push_back(item_val);
                }
            }
            else
                *params = NULL;
        }
        cJSON_Delete(obj);
        return true;
    }
failed_getCommand:
    cJSON_Delete(obj);
    return false;
}

bool MessagingInterface::getState(const char *msg, std::string &cmd, std::list<Value> **params)
{
    cJSON *obj = cJSON_Parse(msg);
    if (!obj) return false;
    {
        cJSON *command = cJSON_GetObjectItem(obj, "command");
        if (!command) goto failed_getCommand;
        if (command->type != cJSON_String) goto failed_getCommand;
        cmd = command->valuestring;
        cJSON *cjParams = cJSON_GetObjectItem(obj, "params");
        int num_params = cJSON_GetArraySize(cjParams);
        if (num_params) {
            *params = new std::list<Value>;
            for (int i=0; i<num_params; ++i) {
                cJSON *item = cJSON_GetArrayItem(cjParams, i);
                Value item_val = valueFromJSONObject(item, 0);
                if (item_val != SymbolTable::Null) (*params)->push_back(item_val);
            }
        }
        else
            *params = NULL;
        cJSON_Delete(obj);
        return true;
    }
failed_getCommand:
    cJSON_Delete(obj);
    return false;
}

