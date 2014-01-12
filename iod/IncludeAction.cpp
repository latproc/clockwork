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
: list_machine_name(name), entry(val) {
}

IncludeActionTemplate::~IncludeActionTemplate() {
}
                                           
Action *IncludeActionTemplate::factory(MachineInstance *mi) {
	return new IncludeAction(mi, this);
}

IncludeAction::IncludeAction(MachineInstance *m, const IncludeActionTemplate *dat)
    : Action(m), list_machine_name(dat->list_machine_name), entry(dat->entry), list_machine(0), entry_machine(0) {
}

IncludeAction::IncludeAction() : list_machine(0), entry_machine(0) {
}

std::ostream &IncludeAction::operator<<(std::ostream &out) const {
	return out << "Include Action " << list_machine_name << " " << entry << "\n";
}

Action::Status IncludeAction::run() {
	owner->start(this);
    if (!list_machine)
        list_machine = owner->lookup(list_machine_name);

	if (list_machine) {
        if (list_machine->_type == "REFERENCE") {
            if (list_machine->locals.size()) {
                // remove old item
                MachineInstance *old = list_machine->locals.at(0).machine;
                if (old) {
                    std::set<Transmitter*>::iterator found = old->listens.find(list_machine);
                    if (found != old->listens.end()) old->listens.erase(found);
                    std::set<MachineInstance*>::iterator dep_found = list_machine->depends.find(old);
                    if (dep_found != list_machine->depends.end()) list_machine->depends.erase(dep_found);
                }
                list_machine->locals.clear();
            }
            if (entry.kind == Value::t_symbol) {
                std::string real_name = entry.sValue;
                //entry.sValue = "ITEM";
                list_machine->addLocal(entry, owner->lookup(real_name));
                Parameter &p = list_machine->locals.at(0);
                p.real_name = real_name;
                p.val.sValue = "ITEM";
                p.val.cached_machine = p.machine;
                if (p.machine)
                    status = Complete;
                else
                    status = Failed;
            }
            else
                status = Failed;
            owner->stop(this);
            return status;
        }
        else {
            bool found = false;
            for (int i=0; i<list_machine->parameters.size(); ++i) {
                if (list_machine->parameters[i].val == entry
                        || list_machine->parameters[i].real_name == entry.asString())
                    found = true;
            }
            if (!found) {
                if (entry.kind == Value::t_symbol)
                    list_machine->addParameter(entry, owner->lookup(entry));
                else
                    list_machine->addParameter(entry);
            }
        }
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

