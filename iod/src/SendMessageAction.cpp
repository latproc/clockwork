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
#include "Channel.h"
#include "MessageLog.h"
#include "Dispatcher.h"

SendMessageActionTemplate::SendMessageActionTemplate(Value msg, Value dest)
: message(msg), target(dest), target_machine(0) {}

SendMessageActionTemplate::SendMessageActionTemplate(Value msg, MachineInstance *dest)
: message(msg), target(dest->fullName()), target_machine(dest) {}


Action *SendMessageActionTemplate::factory(MachineInstance *mi) {
  return new SendMessageAction(mi, *this); 
}

std::ostream &SendMessageActionTemplate::operator<<(std::ostream &out) const {
	return out << "SendMessageActionTemplate "
		<< message << " "
		<< ( (target != 0) ? target : "")
		<< "\n";
}

SendMessageAction::SendMessageAction(MachineInstance *mi, SendMessageActionTemplate &eat)
: Action(mi), message(eat.message), target(eat.target), target_machine(eat.target_machine) {}

void collect_target_list (MachineInstance *target_machine, std::set<MachineInstance*> & targets) {
	if (!target_machine->enabled()) {
		return;
	}
	if (targets.count(target_machine)) { return; }
	targets.insert(target_machine);
	if (target_machine->_type == "REFERENCE") {
		if (target_machine->locals.size() > 0) {
			for (auto & entry : target_machine->locals) {
				if (entry.machine) { collect_target_list(entry.machine, targets); }
			}
		}
	}
	else {
		if (target_machine->_type == "LIST") {
			for (auto & entry : target_machine->parameters) {
				if (entry.machine) { collect_target_list(entry.machine, targets); }
			}
		}
	}
}

Action::Status SendMessageAction::run() {
	owner->start(this);
	if (target != 0) {
		target.cached_machine = 0; // clear cached value
		target_machine = owner->lookup(target);
		DBG_ACTIONS << *this << "\n";
		if (!target_machine) {
			// no target with the given name, however in the case of channels,
			// the target may be an active channel of the given type
			Channel *chn = Channel::findByType(target.asString());
			target_machine = chn;
		}
		if (target_machine) {
			std::string msg_str;
			if (message.kind == Value::t_symbol) {
				Value msg_val = owner->getValue(message);
				if (msg_val != SymbolTable::Null)
					msg_str = msg_val.asString();
				else
					msg_str = message.asString();
			}
			else {
				msg_str = message.asString();
			}

			std::set<MachineInstance*> targets;
			collect_target_list(target_machine, targets);
			for (auto receiver : targets) {
				owner->sendMessageToReceiver(Message(msg_str.c_str()), receiver);
			}
		}
		else {
			std::stringstream ss;
			ss << *this << " Error: cannot find target machine " << target;
			MessageLog::instance()->add(ss.str().c_str());
			NB_MSG << ss.str() << "\n";
			status = Action::Failed;
		}
	}
	else {
		Message *m = new Message(message.asString().c_str());
		Package *p = new Package(owner, 0, m);
		Dispatcher::instance()->deliver(p);
	}
	owner->stop(this);
	return status;
}

Action::Status SendMessageAction::checkComplete() {
	return Action::Complete;
}

std::ostream &SendMessageAction::operator<<(std::ostream &out) const {
	if (!target_machine)
		return out << owner->getName() << ": SendMessageAction " << message << " TO unknown target: " << target.asString() << "\n";
	else
		return out << owner->getName() << ": SendMessageAction " << message << " TO " << target << "\n";
}
		
