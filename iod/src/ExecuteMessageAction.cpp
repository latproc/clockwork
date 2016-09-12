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

#include "ExecuteMessageAction.h"
#include "DebugExtra.h"
#include "Logger.h"
#include "IOComponent.h"
#include "MachineInstance.h"

// ExecuteMessageAction - immediately execute a trigger and handle a message on a machine
ExecuteMessageActionTemplate::ExecuteMessageActionTemplate(CStringHolder msg, CStringHolder dest)
: message(msg), target(dest), target_machine(0) {}

ExecuteMessageActionTemplate::ExecuteMessageActionTemplate::ExecuteMessageActionTemplate(CStringHolder msg, MachineInstance *dest)
: message(msg), target(dest->fullName().c_str()), target_machine(dest) {}

Action *ExecuteMessageActionTemplate::factory(MachineInstance *mi) { 
  return new ExecuteMessageAction(mi, *this); 
}

std::ostream &ExecuteMessageActionTemplate::operator<<(std::ostream &out) const {
    return out << "ExecuteMessageActionTemplate " << message.get() << " on " << target.get();
}

ExecuteMessageAction::ExecuteMessageAction(MachineInstance *mi, ExecuteMessageActionTemplate &eat)
: Action(mi), message(eat.message), target(eat.target), target_machine(eat.target_machine) { }

Action::Status ExecuteMessageAction::run() {
	owner->start(this);
	if (!target_machine) target_machine = owner->lookup(target.get());

	if (target_machine) {
		status = target_machine->execute(Message(message.get()), owner);
		//New, Running, Complete, Failed, Suspended, NeedsRetry
		if (status == Complete || status == Failed) owner->stop(this);
	}
	else {
		status = Action::Failed;
		owner->stop(this);
	}
	return status;
}

Action::Status ExecuteMessageAction::checkComplete() {
	status = target_machine->execute(Message(message.get()), owner);
	if (status == Running || status == Suspended) owner->stop(this);
	return status;
}

std::ostream &ExecuteMessageAction::operator<<(std::ostream &out) const {
    return out << "ExecuteMessageAction " << message.get() << " on " << target.get();
}


