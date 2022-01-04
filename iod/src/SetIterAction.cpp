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

#include "SetIterAction.h"
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
    dest_machine->setValue("DEBUG", buf);
}

// list_machine is the source of data
// entry is the target property or in the case of an iterator, the iterator name
// pos is the source position in the list

SetIterActionTemplate::SetIterActionTemplate(const std::string &name, Value val, Value pos,
                                             bool insert_before, bool expand_items)
    : list_machine_name(name), entry(val), position(pos), before(insert_before),
      expand(expand_items) {
    int x = 1;
}

SetIterActionTemplate::~SetIterActionTemplate() {}

Action *SetIterActionTemplate::factory(MachineInstance *mi) {
    return new SetIterAction(mi, this, position, before, expand);
}

SetIterAction::SetIterAction(MachineInstance *m, const SetIterActionTemplate *dat, Value pos,
                             bool insert_before, bool expand_list)
    : Action(m), list_machine_name(dat->list_machine_name), entry(dat->entry), list_machine(0),
      entry_machine(0), position(pos), before(insert_before), expand(expand_list) {}

SetIterAction::SetIterAction()
    : list_machine(0), entry_machine(0), position(-1), before(false), expand(false) {}

std::ostream &SetIterAction::operator<<(std::ostream &out) const {
    out << "SET " << entry << " TO ";
    if (position < 0) {
        out << "LAST";
    }
    else {
        out << " ITEM " << position;
    }
    out << "OF " << list_machine_name;
    return out;
}

Action::Status SetIterAction::run() {
    owner->start(this);
    list_machine = owner->lookup(list_machine_name);
    MachineInstance *iterator_machine = owner->lookup(entry);
    if (iterator_machine && iterator_machine->_type != "ITERATOR") {
        iterator_machine = 0;
    }

    if (!list_machine || !iterator_machine) {
        char *err_msg = (char *)malloc(100);
        if (!list_machine)
            snprintf(err_msg, 100, "%s must set iterator to an item on a list (check %s)",
                     owner->getName().c_str(), list_machine_name.c_str());
        else
            snprintf(err_msg, 100, "%s no iterator found (check %s)", owner->getName().c_str(),
                     entry.asString().c_str());
        MessageLog::instance()->add(err_msg);
        error_str = err_msg;
        status = Failed;
        return status;
    }
    // Postion
    long pos = -1;
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

    if (list_machine->parameters.size() == 0) {
        char *err_msg = (char *)malloc(100);
        snprintf(err_msg, 100, "%s: cannot set iterator; list %s has no items",
                 owner->getName().c_str(), list_machine_name.c_str());
        MessageLog::instance()->add(err_msg);
        error_str = err_msg;
        status = Failed;
        return status;
    }
    if (pos < 0) {
        pos = list_machine->parameters.size() - 1;
    }

    // check and update the iterator parameter to refer to the list
    if (iterator_machine->parameters.size() &&
        iterator_machine->parameters[0].machine != list_machine) {
        Value v(entry);
        v.cached_machine = list_machine;
        iterator_machine->removeParameter(0);
        iterator_machine->addParameter(v, list_machine, 0, before);
    }
    else if (iterator_machine->parameters.size() == 0) {
        Value v(entry);
        v.cached_machine = list_machine;
        iterator_machine->addParameter(v, list_machine, 0, before);
    }
    iterator_machine->localised_names.erase("ITEM");
    owner->localised_names.erase("ITEM");
    MachineInstance *old = 0;
    if (iterator_machine->locals.size()) {
        old = iterator_machine->locals.at(0).machine;
        if (old) {
            iterator_machine->localised_names.erase(old->getName());
            iterator_machine->removeLocal(0);
        }
    }

    Parameter p = list_machine->parameters[pos];
    iterator_machine->addLocal("ITEM", p.machine);
    iterator_machine->setValue("position", pos);

    status = Complete;
    owner->stop(this);
    return status;
}

Action::Status SetIterAction::checkComplete() {
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
