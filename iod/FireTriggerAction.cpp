
#include "FireTriggerAction.h"
#include "MachineInstance.h"
#include "Logger.h"
#include "DebugExtra.h"

FireTriggerAction::FireTriggerAction(MachineInstance *m, Trigger *t) 
	: Action(m), trigger(t){ if (m) t->setOwner(m); }

Action::Status FireTriggerAction::run() { 
	owner->start(this);
	if (!trigger->enabled()) {
		DBG_SCHEDULER << owner->getName() << " trigger fire after it was disabled; deleting it\n";
		delete trigger;
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
	out << owner->getName() << " FireTrigger Action " << trigger->getName() 
		<< " status: " << status
        << " owner: " << owner->getName()
		<< " owner state: " << owner->getCurrent().getName() << "\n";
	return out;
}
