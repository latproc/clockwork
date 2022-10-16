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

#include "SetListEntriesAction.h"
#include "Logger.h"
#include "MachineInstance.h"

SetListEntriesActionTemplate::SetListEntriesActionTemplate(Value source, Value destination)
    : source_name(source.asString()), dest_name(destination.asString()) {}

SetListEntriesActionTemplate::~SetListEntriesActionTemplate() {}

Action *SetListEntriesActionTemplate::factory(MachineInstance *mi) {
    return new SetListEntriesAction(mi, this);
}

SetListEntriesAction::SetListEntriesAction(MachineInstance *m,
                                           const SetListEntriesActionTemplate *dat)
    : Action(m), source(dat->source_name), dest(dat->dest_name), dest_machine(0) {}

SetListEntriesAction::SetListEntriesAction() : dest_machine(0) {}

std::ostream &SetListEntriesAction::operator<<(std::ostream &out) const {
    return out << "Set List Entries " << source << " to " << dest << "\n";
}

void SetListEntriesAction::setListEntries(unsigned long bitmap) {
    for (size_t i = dest_machine->parameters.size(); i > 0; --i) {
        MachineInstance *entry = dest_machine->parameters[i - 1].machine;
        if (entry) {
            if (bitmap % 2) {
                entry->setState("on");
            }
            else {
                entry->setState("off");
            }
        }
        bitmap /= 2;
    }
}

Action::Status SetListEntriesAction::run() {
    owner->start(this);
    dest_machine = owner->lookup(dest);
    if (dest_machine && dest_machine->_type == "LIST") {
        if (source.kind == Value::t_integer) {
            setListEntries((unsigned long)source.iValue);
        }
        else if (source.kind == Value::t_float) {
            setListEntries((unsigned long)source.fValue);
        }
        else if (source.kind == Value::t_symbol) {
            long val;
            if (owner->getValue(source).asInteger(val) ) {
                unsigned long bitmap = (unsigned long) val;
                setListEntries(bitmap);
            }
        }
        status = Complete;
    }
    else {
        status = Failed;
    }
    owner->stop(this);
    return status;
}

Action::Status SetListEntriesAction::checkComplete() {
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
