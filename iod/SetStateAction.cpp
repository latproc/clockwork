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

#include "SetStateAction.h"
#include "DebugExtra.h"
#include "Logger.h"
#include "IOComponent.h"
#include "MachineInstance.h"

Action *SetStateActionTemplate::factory(MachineInstance *mi) 
{ 
    return new SetStateAction(mi, *this); 
}
                          
Action *MoveStateActionTemplate::factory(MachineInstance *mi) 
{ 
    return new MoveStateAction(mi, *this); 
}
                          
Action::Status SetStateAction::executeStateChange(bool use_transitions)
{
    //std::map<std::string, MachineInstance*>::iterator pos = machines.find(target.get());
    //if (pos != machines.end()) {
    owner->start(this);
    machine = owner->lookup(target.get());
    if (machine) {
		if (machine->getName() != target.get()) {
	    	DBG_M_ACTIONS << owner->getName() << " lookup for " << target.get() << " returned " << machine->getName() << "\n"; 
		}
        //machine = (*pos).second;
		if (machine->io_interface) {
            std::string txt(machine->io_interface->getStateString());
			if (txt == value.getName()) {
				result_str = "OK";
				status = Complete;
				owner->stop(this);
				return status;
			}
			
			if (value == "on") {
				machine->io_interface->turnOn();
			}
			else if (value == "off") {
				machine->io_interface->turnOff();
			}
			status = Running;
            setTrigger(owner->setupTrigger(machine->getName(), value.getName(), ""));
			return status;
		}
		else if ( (value.getName() == "INTEGER" && machine->getCurrent().getName() == "INTEGER")
	              || machine->stateExists(value)
	            ) {
			if (machine->getCurrent().getName() == value.getName()) {
				DBG_M_ACTIONS << machine->getName() << " is already " << value << " skipping " << *this << "\n";
				status = Complete;
				owner->stop(this);
				return status;
			}
			if (use_transitions) {
			    // first look through the transitions to see if a state change should be triggered
			    // by command
                if (! machine->transitions.empty()) {
					BOOST_FOREACH(Transition t, machine->transitions) {
						if ( (t.source == machine->getCurrent() || t.source.getName() == "ANY")
                                && (t.dest == value || t.dest.getName() == "ANY") ) {
                            if (!t.condition || (*t.condition)(owner)) {
                                if (t.trigger.getText() == "NOTRIGGER")  break; // no trigger command for this transition
                                DBG_M_ACTIONS << machine->_name << " has a transition from "
                                    << t.source << " to " << value << " using it\n";
                                status = machine->execute(new Message(t.trigger.getText().c_str()), machine);
                                if (status == Action::Failed) {
                                    error_str = "failed to execute transition\n";
                                }
                                else
                                    result_str = "OK";
                                if (status != Action::Running && status != Action::Suspended && owner->executingCommand() == this) {
                                    owner->stop(this);
                                }
                                return status;
                            }
                            else {
                                std::stringstream ss;
                                ss << "Transition from " << t.source << " to "
                                << value << " denied due to condition " << t.condition->last_evaluation;
                                error_str = ss.str().c_str();
                                DBG_M_ACTIONS << owner->getName() << " "  << ss.str() << "\n";
                                status = New;
                                return NeedsRetry;
                            }
						}
				    }
                }
				//DBG_M_ACTIONS << "SetStateAction didn't find a transition for " << machine->getCurrent() << " to " << value << "; manually setting\n";
			}
			status = machine->setState( value );
			if (status != Action::Running && status != Suspended) 
				owner->stop(this);
			else
				trigger = owner->setupTrigger(machine->getName(), value.getName(), "");
			return status;
	     }
	     else {
		    std::stringstream ss;
			ss << "no machine found from " << owner->getName() << " to handle " << target.get() << ".SetState(" << value.getName() << ")" << std::flush;
			error_str = strdup(ss.str().c_str());
			status = Failed;
			owner->stop(this);
			return status; 
		 }
		result_str = "OK";
		status = Running;
		setTrigger(owner->setupTrigger(machine->getName(), value.getName(), ""));
		return status;
    }
    else {
		std::stringstream ss;
		ss << owner->getName() << " failed to find machine " << target.get() << " for SetState action" << std::flush;
		std::string str = ss.str();
		error_str = strdup(str.c_str());
		status = Failed;
		owner->stop(this);
		return status;
    }
}

Action::Status SetStateAction::run() {
	return executeStateChange(true);
}

Action::Status SetStateAction::checkComplete() {
    if (status == New || status == NeedsRetry) executeStateChange(true);
	if (status == Suspended) resume();
    if (status != Running) return status;
	if (trigger && trigger->enabled()) {
		if (trigger->fired()) {
			DBG_M_MESSAGING << owner->getName() << " Set State Action " << *this << " has triggered, cleaning up\n";
			status = Complete;
			owner->stop(this);
			return status;
		}
	}
	{
	    if (machine->io_interface) {
	        IOComponent *pt = machine->io_interface;
	        if (value.getName() == pt->getStateString()) {
			    status = Complete;
				owner->stop(this);
				return status;
			}
			else {
				DBG_M_ACTIONS << machine->getName() << " still in " << pt->getStateString() << " waiting for " << value << "\n";
				return status;
			}
	    }
	    else {
			if (machine->getCurrent().getName() == value.getName()) {
				status = Complete;
				owner->stop(this);
				return status;
		    }
		}
	}

	DBG_M_ACTIONS << machine->getName() << " (" << machine->getCurrent() << ") waiting for " << value << "\n";
    return status; 
    // NOTE:  this may never finish
}
std::ostream &SetStateAction::operator<<(std::ostream &out) const {
    return out << "SetStateAction " << target.get() << " to " << value << " (status=" << status << ")" << "\n";
}

Action::Status MoveStateAction::run()
{
	return executeStateChange(false);
}

MoveStateAction::~MoveStateAction()
{
	DBG_M_ACTIONS << " deleting " << *this << "\n";
}

std::ostream &MoveStateAction::operator<<(std::ostream &out) const {
    return out << "MoveStateAction " << target.get() << " to " << value << "\n";
}

Action::Status SetIOStateAction::run() {
	owner->start(this);
	if (state.getName() == io_interface->getStateString()) {
		status = Complete;
		owner->stop(this);
		return status;
	}
    if (state.getName() == "on") {
		io_interface->turnOn();
		status = Running;
		return status;
	}
    else if (state.getName() == "off") {
		io_interface->turnOff();
		status = Running;
		return status;
	}
	else {
		status = Failed;
		owner->stop(this);
		return status;
	}
}

Action::Status SetIOStateAction::checkComplete() {
	if (status != Running && status != Suspended) return status;
	if (state.getName() == io_interface->getStateString()) {
		status = owner->setState( state );
		if (status == Complete || status == Failed) {
			owner->stop(this);
		}
		return status;
	}
	else
		return status;
	
}

std::ostream &SetIOStateAction::operator<<(std::ostream &out)const {
	return out << "SetIOState " << *io_interface;
}

