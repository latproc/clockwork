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

#include "ClearListAction.h"
#include "Logger.h"
#include "MachineInstance.h"

ClearListActionTemplate::ClearListActionTemplate(Value destination)
    : dest_name(destination.asString()) {}

ClearListActionTemplate::~ClearListActionTemplate() {}

Action *ClearListActionTemplate::factory(MachineInstance *mi) {
    return new ClearListAction(mi, this);
}

ClearListAction::ClearListAction(MachineInstance *m, const ClearListActionTemplate *dat)
    : Action(m), dest(dat->dest_name), dest_machine(0) {}

ClearListAction::ClearListAction() : dest_machine(0) {}

std::ostream &ClearListAction::operator<<(std::ostream &out) const {
    return out << "Clear List or Reference Action " << dest << "\n";
}

Action::Status ClearListAction::run() {
    owner->start(this);
    dest_machine = owner->lookup(dest);
    if (dest_machine && dest_machine->_type == "LIST") {
#if 1
        // TBD needs further testing
        for (unsigned int i = 0; i < dest_machine->parameters.size(); ++i) {
            Parameter &p = dest_machine->parameters[i];
            if (p.machine) {
                dest_machine->stopListening(p.machine);
                p.machine->removeDependancy(dest_machine);
            }
        }
#endif
        dest_machine->parameters.clear();
        dest_machine->setNeedsCheck();
        status = Complete;
    }
    else if (dest_machine && dest_machine->_type == "REFERENCE") {
        dest_machine->removeLocal(0);
        dest_machine->localised_names.erase("ITEM");
        status = Complete;
    }
    else {
        status = Failed;
    }
    owner->stop(this);
    return status;
}

Action::Status ClearListAction::checkComplete() {
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
