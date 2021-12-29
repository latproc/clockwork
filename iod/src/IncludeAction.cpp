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

#include "IncludeAction.h"
#include "Logger.h"
#include "MachineInstance.h"
#include "MessageLog.h"
#include <sstream>

static void debugParameterChange(MachineInstance *dest_machine) {
    const char *delim = "";
    char buf[1010];
    snprintf(buf, 1000, "[");
    size_t n = 1;
    for (unsigned int i = 0; i < dest_machine->parameters.size(); ++i) {
        snprintf(buf + n, 1000 - n, "%s%s", delim,
                 dest_machine->parameters[i].val.asString().c_str());
        n += strlen(delim) + dest_machine->parameters[i].val.asString().length();
        delim = ",";
        if (n >= 999) {
            break;
        }
    }
    snprintf(buf + n, 1000 - n, "]");
    dest_machine->setValue("DEBUG", Value(buf, Value::t_string));
}

IncludeActionTemplate::IncludeActionTemplate(const std::string &name, Value val, Value pos,
                                             bool insert_before, bool expand_items)
    : list_machine_name(name), entry(val), position(pos), before(insert_before),
      expand(expand_items) {}

IncludeActionTemplate::~IncludeActionTemplate() {}

Action *IncludeActionTemplate::factory(MachineInstance *mi) {
    return new IncludeAction(mi, this, position, before, expand);
}

IncludeAction::IncludeAction(MachineInstance *m, const IncludeActionTemplate *dat, Value pos,
                             bool insert_before, bool expand_list)
    : Action(m), list_machine_name(dat->list_machine_name), entry(dat->entry), list_machine(0),
      entry_machine(0), position(pos), before(insert_before), expand(expand_list) {}

IncludeAction::IncludeAction()
    : list_machine(0), entry_machine(0), position(-1), before(false), expand(false) {}

std::ostream &IncludeAction::operator<<(std::ostream &out) const {
    return out << "Include Action "
               << " " << entry << ((before) ? " before " : " after ");
    if (position < 0) {
        out << "last";
    }
    else {
        out << " item " << position << "of ";
    }
    out << list_machine_name;
}

Action::Status IncludeAction::run() {
    owner->start(this);
    list_machine = owner->lookup(list_machine_name);

    if (list_machine) {
        if (list_machine->_type == "REFERENCE") {
            list_machine->localised_names.erase("ITEM");
            owner->localised_names.erase("ITEM");
            MachineInstance *old = 0;
            if (list_machine->locals.size()) {
                old = list_machine->locals.at(0).machine;
                if (old) {
                    list_machine->localised_names.erase(old->getName());
                    list_machine->removeLocal(0);
                }
            }
            /*
                This gets complicated. Clockwork doesn't have a way for values to to refer
                to machines although internally this happens all the time.
                To enable statements like:
                     x:= TAKE FIRST FROM mylist;
                     ASSIGN x TO test;
                we need to be able to use 'x' as if it is the name of a machine whereas it is the
                name of a property that contains the name of a machine.
                Thus, below we provide for a second level of indirection and the specific sequence
                above works but the ASSIGN statement is the only one that currently will be able to handle x properly
            */
            if (entry.kind == Value::t_symbol) {
                std::string real_name;
                MachineInstance *new_assignment = 0;
                real_name = entry.sValue;
                new_assignment = owner->lookup(real_name);
                bool done = false;
                if (new_assignment) {
                    list_machine->addLocal(entry, new_assignment);
                    done = true;
                }
                else {
                    Value ref = owner->getValue(real_name);
                    if (ref.kind == Value::t_symbol) {
                        real_name = ref.sValue;
                        new_assignment = owner->lookup(ref);
                        list_machine->addLocal(ref, new_assignment);
                        list_machine->localised_names["ITEM"] = new_assignment;
                        done = true;
                    }
                }
                if (!done) {
                    char buf[400];
                    snprintf(buf, 400,
                             "%s failed to lookup machine: %s for assignment. "
                             "Treating it as a string",
                             owner->fullName().c_str(), entry.asString().c_str());
                    MessageLog::instance()->add(buf);
                    list_machine->addLocal(entry, new_assignment);
                }
                else {
                    new_assignment->addDependancy(owner);
                    owner->listenTo(new_assignment);
                }
                Parameter &p = list_machine->locals.at(0); //TBD take more care of this index
                p.real_name = real_name;
                p.val = Value("ITEM");
                p.val.cached_machine = p.machine;
                if (old && new_assignment && old != new_assignment) {
                    list_machine->setNeedsCheck();
                    list_machine->notifyDependents();
                }
                if (p.machine) {
                    status = Complete;
                }
                else {
                    result_str = "assignment failed";
                    status = Failed;
                }
            }
            else {
                status = Failed;
            }
            owner->stop(this);
            return status;
        }
        else {
            entry_machine = entry.kind == Value::t_symbol ? owner->lookup(entry.sValue) : nullptr;
            Value to_insert = entry;
            if (entry.kind == Value::t_symbol && !entry_machine) {
                const Value &v = owner->getValue(entry);
                if (v != SymbolTable::Null) {
                    to_insert = v;
                }
            }
            bool found = false;
            long pos = -1; // indicates a search to insert before a given item
            if (position.kind == Value::t_string || position.kind == Value::t_symbol) {
                const Value &pos_v = owner->getValue(position.asString());
                if (pos_v == SymbolTable::Null || !pos_v.asInteger(pos)) {
                    char *err_msg = (char *)malloc(100);
                    snprintf(err_msg, 100, "%s Cannot find an integer value for %s",
                             owner->getName().c_str(), position.asString().c_str());
                    MessageLog::instance()->add(err_msg);
                    error_str = err_msg;
                    status = Failed;
                    return status;
                }
            }
            else if (!position.asInteger(pos)) {
                char *err_msg = (char *)malloc(100);
                snprintf(err_msg, 100, "%s Unexpected value %s when inserting item",
                         owner->getName().c_str(), position.asString().c_str());
                MessageLog::instance()->add(err_msg);
                error_str = err_msg;
                status = Failed;
                return status;
            }
            if (to_insert.kind == Value::t_symbol) {
                for (unsigned int i = 0; i < list_machine->parameters.size(); ++i) {
                    if (list_machine->parameters[i].val == to_insert ||
                        list_machine->parameters[i].real_name == to_insert.asString()) {
                        found = true;
                    }
                    // bug: if the item being added is a list and expand is true but the list object itself is already
                    //  on this list then it won't be expanded
                }
            }
            if (!found) {
                if (to_insert.kind == Value::t_symbol) {
                    MachineInstance *machine = entry_machine;
                    if (!machine) {
                        // entry may be a property that names a machine
                        Value v = owner->getValue(entry.sValue);
                        if (v.kind == Value::t_symbol) {
                            machine = owner->lookup(v.sValue);
                            if (machine) {
                                if (!v.cached_machine) {
                                    v.cached_machine = machine;
                                }
                                list_machine->addParameter(v, machine, pos, before);
                            }
                            else {
                                list_machine->addParameter(v, nullptr, pos, before);
                            }
                        }
                        else {
                            list_machine->addParameter(v, nullptr, pos, before);
                        }
                    }
                    else {
                        if (machine->_type == "LIST" && expand) {
                            for (unsigned int i = 0; i < machine->parameters.size(); i++) {
                                MachineInstance *item = machine->parameters[i].machine;
                                list_machine->addParameter(machine->parameters[i].val, item, pos,
                                                           before);
                                if (pos >= 0) {
                                    ++pos; // not adding at the end
                                }
                            }
                        }
                        else {
                            list_machine->addParameter(to_insert, machine, pos, before);
                        }
                    }
                }
                else {
                    list_machine->addParameter(to_insert, 0, pos, before);
                }
            }
        }
        debugParameterChange(list_machine);
        status = Complete;
    }
    else {
        char *err_msg = (char *)malloc(400);
        snprintf(err_msg, 400, "%s Cannot find a machine named %s for list insert of %s",
                 owner->getName().c_str(), list_machine_name.c_str(), entry.asString().c_str());
        MessageLog::instance()->add(err_msg);
        error_str = err_msg;
        status = Failed;
    }
    owner->stop(this);
    return status;
}

Action::Status IncludeAction::checkComplete() {
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
