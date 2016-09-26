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
#include "MessageLog.h"

uint64_t nowMicrosecs();

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

	if (new_state.kind != Value::t_symbol && new_state.kind != Value::t_string) {
		std::stringstream ss;
		ss << *this << " failed. " << owner->fullName() << " " << new_state << " must be a symbol or string" << std::flush;
		char * msg = strdup(ss.str().c_str());
		MessageLog::instance()->add(msg);
		error_str = msg;
		status = Failed;
		cleanupTrigger();
		owner->stop(this);
		return status; 
	}

	machine = owner->lookup(target.get());
	if (machine) {
		if (machine->getName() != target.get()) {
			DBG_M_ACTIONS << owner->getName() << " lookup for " << target.get() << " returned " << machine->getName() << "\n"; 
		}
		// the new 'state' may be a symbol that provides the state, it may even be the name of a
		// VARIABLE or CONSTANT that contains the name. If we have a state that coincides with
		// new_state, we ignore such subtleties.
		if (!machine->hasState(new_state.sValue)) {
			State value(new_state.sValue.c_str());
			const Value &deref = owner->getValue(new_state.sValue.c_str());
			if (deref != SymbolTable::Null) {
				DBG_M_ACTIONS << *this << " dereferenced " << new_state << " to " << deref << "\n";
				if (deref.kind != Value::t_symbol && deref.kind != Value::t_string) {
					std::stringstream ss;
					ss << owner->fullName() << " state " << deref << " ("<<deref.kind<<")" << " must be a symbol or string" << std::flush;
					char *msg = strdup(ss.str().c_str());
					error_str = msg;
					MessageLog::instance()->add(msg);
					DBG_M_ACTIONS << error_str << "\n";
					status = Failed;
					cleanupTrigger();
					owner->stop(this);
					return status; 
				}
				else if (!machine->hasState(deref.sValue)){
					std::stringstream ss;
					ss << machine->fullName();
					if (machine->getStateMachine()) ss << " of class " << machine->getStateMachine()->name << " ";
					ss << " does not have a state " << deref.sValue << std::flush;
					char *msg = strdup(ss.str().c_str());
					MessageLog::instance()->add(msg);
					error_str = msg;
					DBG_M_ACTIONS << error_str << "\n";
					status = Failed;
					cleanupTrigger();
					owner->stop(this);
					return status;
				}
				value = deref.sValue.c_str();
				DBG_M_ACTIONS << owner->fullName() << " setting state of " << machine->fullName() <<" to dereferenced value " << value << "\n";
				new_state = value.getName();
			}
			else {
				char buf[150];
				snprintf(buf, 150, "Error: Machine %s asked to change to unknown state %s",
					machine->getName().c_str(), deref.sValue.c_str());
				MessageLog::instance()->add(buf);
			}
		}
		if (machine->io_interface) {
			std::string txt(machine->io_interface->getStateString());
			if (txt == value.getName()) {
				result_str = "OK";
				status = Complete;
				cleanupTrigger();
				return status;
			}

			if (value == "on") {
				machine->io_interface->turnOn();
				status = Running;
				setTrigger(owner->setupTrigger(machine->getName(), value.getName(), ""));
				return status;
			}
			else if (value == "off") {
				machine->io_interface->turnOff();
				status = Running;
				setTrigger(owner->setupTrigger(machine->getName(), value.getName(), ""));
				return status;
			}
			std::string res = "Failed to set io state to ";
			res += value.getName();
			res += " unknown state";
			result_str = res.c_str();
			status = Failed;
			owner->stop(this);
			return status;
		}
		else 
// TBD concerned about the cost of MachineInstance::stateExists 
//			if ( (value.getName() == "INTEGER" && machine->getCurrent().getName() == "INTEGER")
//				|| machine->stateExists(value)
//			) 
		{
			if (machine->getCurrent() == value) {
				DBG_M_ACTIONS << machine->getName() << " is already " << value << " skipping " << *this << "\n";
				status = Complete;
				cleanupTrigger();
				owner->stop(this);
				return status;
			}
			if (use_transitions) {
				// first look through the transitions to see if a state change should be triggered
				// by command
				if (! machine->transitions.empty()) {
					std::list<Transition>::iterator iter = machine->transitions.begin();
					while (iter != machine->transitions.end()) {
						const Transition &t = *iter++;
						if ( (t.source == machine->getCurrent() || t.source.getName() == "ANY")
								&& (t.dest == value || t.dest.getName() == "ANY") ) {
							if (!t.condition || (*t.condition)(owner)) {
								if (t.trigger.getText() == "NOTRIGGER")  break; // no trigger command for this transition
								DBG_M_ACTIONS << machine->_name << " has a transition from "
									<< t.source << " to " << value << " using it\n";
								Message ssmsg(t.trigger.getText().c_str());
								status = machine->execute(ssmsg, machine);
								if (status == Action::Failed) {
									error_str = "failed to execute transition\n";
								}
								else
									result_str = "OK";
								if (status != Action::Running && status != Action::Suspended && owner->executingCommand() == this) {
									cleanupTrigger();
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
			status = machine->setState( value, authority );
			cleanupTrigger();
			if (status == Complete || status == Failed) {
				owner->stop(this);
			}
			else {
				if (trigger) trigger->release();
				trigger = owner->setupTrigger(machine->getName(), value.getName(), "");
			}
			if (status == Action::Failed) {
				char buf[100];
				snprintf(buf, 100, "%s::setState(%s) Failed\n", machine->getName().c_str(), 
					value.getName().c_str() );
				error_str = (const char *)buf;
			}
			if (status == Complete) {
				owner->stop(this);
				State value(new_state.sValue.c_str());
				if (owner->getStateMachine()->isStableState(value)) {
					std::multimap<std::string, StableState>::iterator iter(owner->getStateMachine()->stable_state_xref.find(new_state.sValue));
					while (iter != owner->getStateMachine()->stable_state_xref.end()) {
						const std::pair<std::string, StableState> &node(*iter++);
						if (node.second.state_name != new_state.sValue) break;
						if (node.second.uses_timer) {
							DBG_SCHEDULER << owner->fullName() << "[" << owner->current_state.getName()
							<< "] scheduling condition tests for state " << node.second.state_name << "\n";
							node.second.condition.predicate->scheduleTimerEvents(owner);
						}
					}
				}

			}
			return status;
		}
#if 0
		else {
			std::stringstream ss;
			ss << "no machine found from " << owner->getName() << " to handle " << target.get() << ".SetState(" << value.getName() << ")" << std::flush;
			error_str = strdup(ss.str().c_str());
			status = Failed;
			owner->stop(this);
			return status; 
		}
#endif
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
	if (status != Complete && status != Running) return status;
	if (trigger && (trigger->enabled() || trigger->fired()) ) {
		if (trigger->fired()) {
			DBG_M_MESSAGING << owner->getName() << " Set State Action " << *this << " has triggered, cleaning up\n";
			NB_MSG << owner->getName() << " Set State Action " << *this << " has triggered, cleaning up\n";
			status = Complete;
			cleanupTrigger();
			owner->stop(this);
			return status;
		}
	}

	if (trigger && nowMicrosecs() - trigger->startTime() > 10000 ) {
		trigger->report("taking a long time");
	}

	{
		if (machine->io_interface) {
			IOComponent *pt = machine->io_interface;
			if (value.getName() == pt->getStateString()) {
				status = Complete;
				if (trigger) {
					if (trigger->enabled() && !trigger->fired()) trigger->fire();
					cleanupTrigger();
				}
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
				if (trigger) {
					if (trigger->enabled() && !trigger->fired()) trigger->fire();
					cleanupTrigger();
				}
				owner->stop(this);
				return status;
			}
			else {
				DBG_M_ACTIONS << owner->getName() << " still waiting for " << machine->getName() << " to move to state " << value << "\n";
			}
		}
	}

	DBG_M_ACTIONS << machine->getName() << " (" << machine->getCurrent() << ") waiting for " << value << "\n";
	return status; 
	// NOTE:  this may never finish
}
std::ostream &SetStateAction::operator<<(std::ostream &out) const {
	return out << "SetStateAction " << target.get() << " to " << value;
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
	return out << "MoveStateAction " << target.get() << " to " << value;
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
	if (status == Complete || status == Failed) {
		owner->stop(this);
		return status;
	}
	if (status != Running && status != Suspended) return status;
	if (state.getName() == io_interface->getStateString()) {
		status = owner->setState( state, authority );
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

