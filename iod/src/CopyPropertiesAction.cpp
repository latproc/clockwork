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

#include "CopyPropertiesAction.h"
#include "Logger.h"
#include "MachineInstance.h"
#include "MessageLog.h"
#include "RecordClass.h"
#include "cJSON.h"

namespace {

// Convert a JSON field into a Value. Scalar nodes are copied (the source JSON
// stays owned by its holder); objects/arrays are deep-cloned. Mirrors the value
// conversion used elsewhere for JSON rows without coupling this generic action
// to the RECORD layer.
Value jsonFieldToValue(cJSON *item) {
    if (!item) {
        return Value();
    }
    if (item->type == cJSON_Object || item->type == cJSON_Array) {
        return Value(clone_json(item));
    }
    return get_value(item);
}

} // namespace

CopyPropertiesActionTemplate::CopyPropertiesActionTemplate(Value source, Value destination)
    : source_name(source.asString()), dest_name(destination.asString()) {}

CopyPropertiesActionTemplate::CopyPropertiesActionTemplate(Value source, Value destination,
                                                           const std::list<std::string> &properties)
    : source_name(source.asString()), dest_name(destination.asString()), property_list(properties) {
}
CopyPropertiesActionTemplate::~CopyPropertiesActionTemplate() {}

Action *CopyPropertiesActionTemplate::factory(MachineInstance *mi) {
    return new CopyPropertiesAction(mi, this);
}

std::ostream &CopyPropertiesActionTemplate::operator<<(std::ostream &out) const {
    if (!property_list.empty()) {
        out << "CopyProperties ";
        const char *delim = "";
        std::list<std::string>::const_iterator iter = property_list.begin();
        while (iter != property_list.end()) {
            out << delim << *iter++;
            delim = ",";
        }
    }
    else {
        out << "CopyProperties from";
    }
    return out << " " << source_name << " to " << dest_name;
}

CopyPropertiesAction::CopyPropertiesAction(MachineInstance *m,
                                           const CopyPropertiesActionTemplate *dat)
    : Action(m), source(dat->source_name), dest(dat->dest_name), source_machine(0), dest_machine(0),
      property_list(dat->property_list) {}

CopyPropertiesAction::CopyPropertiesAction() : source_machine(0), dest_machine(0) {}

std::ostream &CopyPropertiesAction::operator<<(std::ostream &out) const {
    if (!property_list.empty()) {
        out << "CopyProperties ";
        const char *delim = "";
        std::list<std::string>::const_iterator iter = property_list.begin();
        while (iter != property_list.end()) {
            out << delim << *iter++;
            delim = ",";
        }
    }
    else {
        out << "CopyProperties from";
    }
    return out << " " << source << " to " << dest;
}

Action::Status CopyPropertiesAction::run() {
    owner->start(this);
    source_machine = owner->lookup(source);
    dest_machine = owner->lookup(dest);
    MachineClass *dest_class = dest_machine ? dest_machine->getStateMachine() : nullptr;
    const bool dest_is_record = dest_class && RecordClass::isRecord(dest_class);

    // A symbol may also name an OPTION that holds a JSON object — e.g. a row
    // taken from a query-result LIST via `x := TAKE FIRST FROM rows`. Copy its
    // fields onto the destination the same way a machine's properties are copied.
    cJSON *source_json = nullptr;
    if (!source_machine) {
        const Value &src_val = owner->getValue(source.sValue);
        if (src_val.kind == Value::t_json && src_val.json &&
            src_val.json->type == cJSON_Object) {
            source_json = src_val.json;
        }
    }

    if (dest_machine && (source_machine || source_json)) {
        size_t count = 0; // how many direct symbol updates did we do?
        if (dest_is_record) {
            dest_machine->setRecordApplyMode(true);
        }
        if (source_machine) {
            if (property_list.empty()) {
                //dest_machine->properties.add(source_machine->properties);
                SymbolTableConstIterator iter = source_machine->properties.begin();
                while (iter != source_machine->properties.end()) {
                    const std::string &prop = (*iter).first;
                    if (prop != "STATE" && prop != "NAME" &&
                        (!dest_class || !dest_class->propertyIsLocal(prop)) &&
                        (!dest_is_record ||
                         dest_class->getOptions().find(prop) != dest_class->getOptions().end())) {
                        dest_machine->setValue(prop, (*iter).second);
                        ++count;
                    }
                    iter++;
                }
            }
            else {
                std::list<std::string>::const_iterator iter = property_list.begin();
                while (iter != property_list.end()) {
                    const std::string &prop = (*iter++);
                    if (prop != "STATE" && prop != "NAME") {
                        const Value &val = source_machine->properties.lookup(prop.c_str());
                        if (val != SymbolTable::Null) {
                            dest_machine->setValue(prop, val);
                        }
                        else {
                            DBG_MSG << "ignoring null property " << source_machine->getName() << "."
                                    << prop << "during " << *this << "\n";
                        }
                    }
                }
            }
        }
        else {
            // JSON-object source: project the object's keys onto the destination
            // (skip STATE/NAME and, for RECORDs, fields not declared as OPTIONS).
            if (property_list.empty()) {
                cJSON *f = source_json->child;
                while (f) {
                    const std::string prop = f->string ? f->string : "";
                    if (!prop.empty() && prop != "STATE" && prop != "NAME" &&
                        (!dest_class || !dest_class->propertyIsLocal(prop)) &&
                        (!dest_is_record ||
                         dest_class->getOptions().find(prop) != dest_class->getOptions().end())) {
                        dest_machine->setValue(prop, jsonFieldToValue(f));
                        ++count;
                    }
                    f = f->next;
                }
            }
            else {
                std::list<std::string>::const_iterator iter = property_list.begin();
                while (iter != property_list.end()) {
                    const std::string &prop = (*iter++);
                    if (prop != "STATE" && prop != "NAME") {
                        cJSON *f = cJSON_GetObjectItem(source_json, prop.c_str());
                        if (f) {
                            dest_machine->setValue(prop, jsonFieldToValue(f));
                            ++count;
                        }
                        else {
                            DBG_MSG << "ignoring absent field " << prop << " during " << *this
                                    << "\n";
                        }
                    }
                }
            }
        }
        if (dest_is_record) {
            dest_machine->setRecordApplyMode(false);
            dest_machine->setRecordSystemState("clean");
        }
        if (count) {
            dest_machine->setNeedsCheck();
            dest_machine->notifyDependents();
        }
        status = Complete;
    }
    else {
        std::stringstream ss;
        ss << "Error " << *this << std::flush;
        error_str = strdup(ss.str().c_str());
        MessageLog::instance()->add(error_str.get());
        status = Failed;
    }
    owner->stop(this);
    return status;
}

Action::Status CopyPropertiesAction::checkComplete() {
    if (status == Complete || status == Failed) {
        return status;
    }
    if (this != owner->executingCommand()) {
        DBG_MSG << "checking complete on " << *this << " when it is not the top of stack \n";
    }
    else {
        status = Complete;
        owner->stop(this);
    }
    return status;
}
