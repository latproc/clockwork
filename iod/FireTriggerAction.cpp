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

#include "FireTriggerAction.h"
#include "MachineInstance.h"
#include "Logger.h"
#include "DebugExtra.h"

FireTriggerAction::FireTriggerAction(MachineInstance *m, Trigger *t) 
	: Action(m), trigger(t){ if (m) t->setOwner(m); }

FireTriggerAction::~FireTriggerAction() {
}

Action::Status FireTriggerAction::run() { 
	owner->start(this);
	if (!trigger->enabled()) {
		DBG_SCHEDULER << owner->getName() << " trigger fire after it was disabled; deleting it\n";
		//delete trigger;
        trigger = 0;
		status = Complete;
		owner->stop(this);
		return status;
	}
	DBG_SCHEDULER << owner->getName() << " triggered " << trigger->getName() << "\n";
	trigger->fire(); 
	status = Complete;
	owner->stop(this);
	return status;
}

Action::Status FireTriggerAction::checkComplete() {
	return Complete;
}

std::ostream& FireTriggerAction::operator<<(std::ostream &out) const {
	out << owner->getName() << " FireTrigger Action " << ((trigger) ? trigger->getName() : "(no trigger)") 
		<< " status: " << status
        << " owner: " << owner->getName()
		<< " owner state: " << owner->getCurrent().getName() << "\n";
	return out;
}
