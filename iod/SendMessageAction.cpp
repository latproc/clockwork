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

#include "SendMessageAction.h"
#include "DebugExtra.h"
#include "Logger.h"
#include "IOComponent.h"
#include "MachineInstance.h"

Action *SendMessageActionTemplate::factory(MachineInstance *mi) { 
  return new SendMessageAction(mi, *this); 
}

std::ostream &SendMessageActionTemplate::operator<<(std::ostream &out) const {
    return out << "SendMessageActionTemplate " << message.get() << " " << target.get() << "\n";
}

Action::Status SendMessageAction::run() {
	owner->start(this);
	if (!target_machine) target_machine = owner->lookup(target.get());
	if (target_machine) {
		owner->send(new Message(message.get()), target_machine);
        if (target_machine->_type == "LIST") {
            for (unsigned int i=0; i<target_machine->parameters.size(); ++i) {
                MachineInstance *entry = target_machine->parameters[i].machine;
                if (entry) owner->send(new Message(message.get()), entry);
            }
        }
        else if (target_machine->_type == "REFERENCE" && target_machine->locals.size()) {
            for (unsigned int i=0; i<target_machine->locals.size(); ++i) {
                MachineInstance *entry = target_machine->locals[i].machine;
                if (entry) owner->send(new Message(message.get()), entry);
            }
        }
	}
	else {
		NB_MSG << *this << " Error: cannot find target machine " << target.get() << "\n"; 
	}
	status = Action::Complete;
	owner->stop(this);
	return status;
}

Action::Status SendMessageAction::checkComplete() {
	return Action::Complete;
}

std::ostream &SendMessageAction::operator<<(std::ostream &out) const {
    return out << "SendMessageAction " << message.get() << " " << target.get() << "\n";
}
		
