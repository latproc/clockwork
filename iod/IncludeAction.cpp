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
#include "MessageLog.h"

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
            MachineInstance *old = 0;
            if (list_machine->locals.size()) {
                // remove old item
                old = list_machine->locals.at(0).machine;
                if (old) {
                    old->removeDependancy(list_machine);
                    list_machine->stopListening(old);
                }
                list_machine->locals.clear();
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
                if (entry.cached_machine) {
                    real_name = entry.cached_machine->getName();
                    new_assignment = entry.cached_machine;
                    list_machine->addLocal(entry,new_assignment);
                    new_assignment->addDependancy(owner);
                    owner->listenTo(new_assignment);
                }
                else {
                    real_name = entry.sValue;
                    new_assignment = owner->lookup(real_name);
                    bool done = false;
                    if (new_assignment) {
                        list_machine->addLocal(entry,new_assignment);
                        done = true;
                    }
                    else {
                        Value ref = owner->getValue(real_name);
                        if (ref.kind == Value::t_symbol) {
                            real_name = ref.sValue;
                            new_assignment = owner->lookup(ref);
                            list_machine->addLocal(ref,new_assignment);
                            done = true;
                        }
                    }
                    if (!done) {
                        std::stringstream ss;
                        ss << owner->fullName()
                            << " failed to lookup machine: " << entry << " for assignment. Treating it as a string";
                        char *err_msg = strdup(ss.str().c_str());
                        MessageLog::instance()->add(err_msg);
                        free(err_msg);
                        list_machine->addLocal(entry,new_assignment);
                    }
                    else {
                        new_assignment->addDependancy(owner);
                        owner->listenTo(new_assignment);
                    }
                }
                Parameter &p = list_machine->locals.at(0);
                 p.real_name = real_name;
                p.val.sValue = "ITEM";
                p.val.cached_machine = p.machine;
                if (old && new_assignment && old != new_assignment) {
                    Message msg("changed");
                    list_machine->notifyDependents(msg);
                }
                if (p.machine)
                    status = Complete;
                else {
                    result_str = "assignment failed";
                    status = Failed;
                }
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
#if 0
                {
                    // TBD needs further testing
                    MachineInstance *machine = owner->lookup(entry);
                    if (!machine) {
                        Value v = owner->getValue(entry.sValue);
                        if (v.kind == Value::t_symbol) {
                            machine = owner->lookup(v.sValue);
                            if (machine) {
                                if (!v.cached_machine) v.cached_machine = machine;
                                list_machine->addParameter(v, machine);
                            }
                            else
                                list_machine->addParameter(v);
                        }
                        else
                            list_machine->addParameter(v);
                    }
                    else
                        list_machine->addParameter(entry, machine);
                }
#else
                    list_machine->addParameter(entry, owner->lookup(entry));
#endif
                else
                    list_machine->addParameter(entry);
            }
        }
        status = Complete;
	}
    else {
        std::stringstream ss;
        ss << "Cannot find a machine named " << list_machine_name << " for assignment of " << entry;
        char *err_msg = strdup(ss.str().c_str());
        MessageLog::instance()->add(err_msg);
        error_str = err_msg;
        status = Failed;
    }
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

