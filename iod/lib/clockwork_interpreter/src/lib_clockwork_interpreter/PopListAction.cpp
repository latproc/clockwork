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

#include <algorithm>
#include <numeric>

#include "MachineInstance.h"
#include "PopListAction.h"
// #include "Logger.h"

PopBackActionTemplate::PopBackActionTemplate(Value list_name)
    : list_machine_name(list_name.asString()) {}

PopBackActionTemplate::~PopBackActionTemplate() {}

Action *PopBackActionTemplate::factory(MachineInstance *mi) {
    return new PopBackAction(mi, this);
}

PopBackAction::PopBackAction(MachineInstance *m,
                             const PopBackActionTemplate *dat)
    : Action(m), list_machine_name(dat->list_machine_name), list_machine(0) {}

PopBackAction::PopBackAction() : list_machine(0) {}

std::ostream &PopBackAction::operator<<(std::ostream &out) const {
    return out << "Pop Back Action " << list_machine->getName();
}

Action::Status PopBackAction::run() {
    owner->start(this);
    if (!list_machine) {
        list_machine = owner->lookup(list_machine_name);
    }
    if (list_machine && list_machine->_type == "LIST") {

        if (list_machine->parameters.size()) {
            unsigned int i = list_machine->parameters.size();
            Value res;
            if (list_machine->parameters[i].val.kind == Value::t_symbol) {
                MachineInstance *mi = list_machine->parameters[i].machine;
                if (mi == 0) {
                    mi = owner->lookup(list_machine->parameters[i].val.sValue);
                    list_machine->parameters[i].machine = mi;
                }
                if (mi == 0) {
                    status = Failed;
                    owner->stop(this);
                    return status;
                } else {
                    res =
                        list_machine->parameters[i].machine->getValue("VALUE");
                }
            } else {
            }
        } else {
            status = Failed;
            owner->stop(this);
            return status;
        }
        status = Complete;
    } else {
        status = Failed;
    }
    owner->stop(this);
    return status;
}

Action::Status PopBackAction::checkComplete() {
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
