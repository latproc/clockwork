/*
 Copyright (C) 2014 Martin Leadbeater, Michael O'Connor
 
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

#include <string>
#include <string.h>
#include "cJSON.h"
#include "MessageEncoding.h"
#include "value.h"
#include "symboltable.h"
#include <assert.h>
#include <math.h>

std::string MessageEncoding::valueType(const Value &v) {
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

void MessageEncoding::addValueToJSONObject(cJSON *obj, const char *name, const Value &val) {
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

char *MessageEncoding::encodeCommand(std::string cmd, std::list<Value> *params) {
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

char *MessageEncoding::encodeCommand(std::string cmd, Value p1, Value p2, Value p3) {
    std::list<Value>params;
    params.push_back(p1);
    params.push_back(p2);
    params.push_back(p3);
    return encodeCommand(cmd, &params);
}

char *MessageEncoding::encodeState(const std::string &machine, const std::string &state) {
    cJSON *msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "command", "STATE");
    cJSON *cjParams = cJSON_CreateArray();
    cJSON_AddItemToArray(cjParams, cJSON_CreateString(machine.c_str()));
    cJSON_AddItemToArray(cjParams, cJSON_CreateString(state.c_str()));
    cJSON_AddItemToObject(msg, "params", cjParams);
    char *res = cJSON_PrintUnformatted(msg);
    cJSON_Delete(msg);
    return res;
}

char *MessageEncoding::encodeError(const char *error) {
    cJSON *msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "error", error);
    char *res = cJSON_PrintUnformatted(msg);
    cJSON_Delete(msg);
    return res;
}

Value MessageEncoding::valueFromJSONObject(cJSON *obj, cJSON *cjType) {
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


bool MessageEncoding::getCommand(const char *msg, std::string &cmd, std::list<Value> **params)
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

bool MessageEncoding::getState(const char *msg, std::string &cmd, std::list<Value> **params)
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

