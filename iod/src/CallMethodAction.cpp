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

#include "CallMethodAction.h"
#include "DebugExtra.h"
#include "Logger.h"
#include "IOComponent.h"
#include "MachineInstance.h"

// -- CallMethodAction - send a command message to a machine and wait for a response
//    the remote machine must send a reply or this action hangs forever

Action *CallMethodActionTemplate::factory(MachineInstance *mi) { 
  return new CallMethodAction(mi, *this); 
}

std::ostream &CallMethodActionTemplate::operator<<(std::ostream &out) const {
    return out << "CallMethodActionTemplate " << message.get() << " on " << target.get() << "\n";
}

Action::Status CallMethodAction::run() {
	owner->start(this);
	if (!target_machine) target_machine = owner->lookup(target.get());
	if (!target_machine) {
		std::stringstream ss;
		ss << owner->getName() << " CallMethodAction failed to find machine " << target.get();
		std::string s(ss.str());
		error_str = strdup(s.c_str());
		return Failed;
	}
	if (target_machine == owner) {
		DBG_M_MESSAGING << owner->getName() << " sending message " << message.get() << "\n";
		std::string short_name(message.get());
		if (short_name.rfind('.') != std::string::npos) {
			short_name.substr(short_name.rfind('.'));
            Message msg(short_name.c_str());
			owner->execute(msg, target_machine);
		}
		else {
			owner->execute(Message(message.get()), target_machine);
        }
		status = Action::Complete;
		owner->stop(this);
		return status;
	}
	else {
		setTrigger(owner->setupTrigger(target_machine->getName(), message.get(), "_done"));
		owner->sendMessageToReceiver(new Message(message.get()), target_machine, true);
	}
	status = Action::Running;
	return status;
}

Action::Status CallMethodAction::checkComplete() {
    if (status == Complete || status == Failed) return status;
	if ( trigger->fired()) {
		status = Action::Complete;
		owner->stop(this);
		return status;
	}
	return Action::Running;
}

std::ostream &CallMethodAction::operator<<(std::ostream &out) const {
    return out << "CallMethodAction " << message.get() << " on " << target.get()
	<< ((trigger && !trigger->fired() ) ? " Waiting for trigger to fire " : (!trigger) ? " no trigger set " : "trigger fired")
	<< "\n";
}
		
