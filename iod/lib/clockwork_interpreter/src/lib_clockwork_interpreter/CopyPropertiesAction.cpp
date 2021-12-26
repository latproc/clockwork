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
#include "includes.hpp"
// #include "MachineInstance.h"
// // #include "Logger.h"
// // #include "MessageLog.h"

CopyPropertiesActionTemplate::CopyPropertiesActionTemplate(Value source,
                                                           Value destination)
    : source_name(source.asString()), dest_name(destination.asString()) {}

CopyPropertiesActionTemplate::CopyPropertiesActionTemplate(
    Value source, Value destination, const std::list<std::string> &properties)
    : source_name(source.asString()), dest_name(destination.asString()),
      property_list(properties) {}
CopyPropertiesActionTemplate::~CopyPropertiesActionTemplate() {}

Action *CopyPropertiesActionTemplate::factory(MachineInstance *mi) {
    return new CopyPropertiesAction(mi, this);
}

std::ostream &
CopyPropertiesActionTemplate::operator<<(std::ostream &out) const {
    if (!property_list.empty()) {
        out << "CopyProperties ";
        const char *delim = "";
        std::list<std::string>::const_iterator iter = property_list.begin();
        while (iter != property_list.end()) {
            out << delim << *iter++;
            delim = ",";
        }
    } else {
        out << "CopyProperties from";
    }
    return out << " " << source_name << " to " << dest_name;
}

CopyPropertiesAction::CopyPropertiesAction(
    MachineInstance *m, const CopyPropertiesActionTemplate *dat)
    : Action(m), source(dat->source_name), dest(dat->dest_name),
      source_machine(0), dest_machine(0), property_list(dat->property_list) {}

CopyPropertiesAction::CopyPropertiesAction()
    : source_machine(0), dest_machine(0) {}

std::ostream &CopyPropertiesAction::operator<<(std::ostream &out) const {
    if (!property_list.empty()) {
        out << "CopyProperties ";
        const char *delim = "";
        std::list<std::string>::const_iterator iter = property_list.begin();
        while (iter != property_list.end()) {
            out << delim << *iter++;
            delim = ",";
        }
    } else {
        out << "CopyProperties from";
    }
    return out << " " << source << " to " << dest;
}

Action::Status CopyPropertiesAction::run() {
    owner->start(this);
    source_machine = owner->lookup(source);
    dest_machine = owner->lookup(dest);
    if (source_machine && dest_machine) {
        size_t count = 0; // how many direct symbol updates did we do?
        if (property_list.empty()) {
            //dest_machine->properties.add(source_machine->properties);
            SymbolTableConstIterator iter = source_machine->properties.begin();
            while (iter != source_machine->properties.end()) {
                const std::string &prop = (*iter).first;
                if (prop != "STATE" && prop != "NAME" &&
                    (!dest_machine->getStateMachine() ||
                     !dest_machine->getStateMachine()->propertyIsLocal(prop))) {
                    dest_machine->setValue(prop, (*iter).second);
                    ++count;
                }
                iter++;
            }
        } else {
            std::list<std::string>::const_iterator iter = property_list.begin();
            while (iter != property_list.end()) {
                const std::string &prop = (*iter++);
                if (prop != "STATE" && prop != "NAME") {
                    const Value &val =
                        source_machine->properties.lookup(prop.c_str());
                    if (val != SymbolTable::Null) {
                        dest_machine->setValue(prop, val);
                    } else {
                        DBG_MSG << "ignoring null property "
                                << source_machine->getName() << "." << prop
                                << "during " << *this << "\n";
                    }
                }
            }
        }
        if (count) {
            dest_machine->setNeedsCheck();
            dest_machine->notifyDependents();
        }
        status = Complete;
    } else {
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
        DBG_MSG << "checking complete on " << *this
                << " when it is not the top of stack \n";
    } else {
        status = Complete;
        owner->stop(this);
    }
    return status;
}
