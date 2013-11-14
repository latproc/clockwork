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
#include "MachineInstance.h"
#include "Logger.h"

IncludeActionTemplate::IncludeActionTemplate(const std::string &name, Value val)
: list_machine_name(name), entry_name(val) {
}

IncludeActionTemplate::~IncludeActionTemplate() {
}
                                           
Action *IncludeActionTemplate::factory(MachineInstance *mi) {
	return new IncludeAction(mi, this);
}

IncludeAction::IncludeAction(MachineInstance *m, const IncludeActionTemplate *dat)
    : Action(m), list_machine_name(dat->list_machine_name), entry_name(dat->entry_name) {
}

IncludeAction::IncludeAction() {
}

std::ostream &IncludeAction::operator<<(std::ostream &out) const {
	return out << "Include Action " << list_machine_name << " " << entry_name << "\n";
}

Action::Status IncludeAction::run() {
	owner->start(this);
    if (!list_machine)
        list_machine = owner->lookup(list_machine_name);
	if (list_machine) {
        bool found = false;
        for (int i=0; i<list_machine->parameters.size(); ++i) {
            if (list_machine->parameters[i].val == entry_name
                    || list_machine->parameters[i].real_name == entry_name.asString())
                found = true;
        }
		if (!found)
            list_machine->addParameter(entry_name, owner->lookup(entry_name));
        status = Complete;
	}
    else
        status = Failed;
    owner->stop(this);
	return status;
}

Action::Status IncludeAction::checkComplete() {
	if (status == Complete || status == Failed) return status;
	if (this != owner->executingCommand()) {
		DBG_MSG << "checking complete on " << *this << " when it is not the top of stack \n";
	}
	else {
		status = Complete;
		owner->stop(this);
	}
	return status;
}

