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

static int count_instances = 0;
static int max_count = 0;
static int last_max = 0;

FireTriggerAction::FireTriggerAction(MachineInstance *m, Trigger *t) 
: Action(m),  pending_trigger(t->retain()) {
	if (m) t->setOwner(m);
	t->addHolder(this);
	++count_instances;
	if (count_instances>max_count) max_count = count_instances;
}

FireTriggerAction::~FireTriggerAction() {
	--count_instances;
/*
	if (last_max != max_count) {
		DBG_MSG << "Max FireTriggers: " << max_count<<"\n"; last_max = max_count;
	}
*/
	DBG_M_ACTIONS << "Removing " << *this << "\n";
	cleanupTrigger();
}

Action::Status FireTriggerAction::run() { 
	owner->start(this);
	Action::setTrigger(pending_trigger->retain());
	pending_trigger = pending_trigger->release();
	if (trigger->enabled()) {
		DBG_M_ACTIONS << owner->getName() << " triggered " << trigger->getName() << "\n";
		trigger->fire(); 
	}
	else {
		//DBG_MSG << owner->getName() << " trigger " << trigger->getName() << " fired while disabled\n";
		//assert(trigger->fired());
	}
	cleanupTrigger();
	status = Complete;
	owner->stop(this);
	return status;
}

Action::Status FireTriggerAction::checkComplete() {
	assert(status == Complete);
	return status;
}

std::ostream& FireTriggerAction::operator<<(std::ostream &out) const {
	out << owner->getName() << " FireTrigger Action " << ((trigger) ? trigger->getName() : "(no trigger)")  << ","
		<< " enabled: " << ((trigger) ? trigger->enabled() : false)  << ","
		<< " status: " << status  << ","
        << " owner: " << owner->getName()  << ","
		<< " owner state: " << owner->getCurrent().getName();
	return out;
}
