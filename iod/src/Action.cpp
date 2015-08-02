
#include <iostream>
#include "Logger.h"
#include "DebugExtra.h"
#include "Action.h"
#include "MachineInstance.h"

const char *actionStatusName(const Action::Status &state) {
	switch(state) {
		case Action::New: return "New";
		case Action::Running: return "Running";
		case Action::Failed: return "Failed";
		case Action::Complete: return "Complete";
		case Action::Suspended: return "Suspended";
		case Action::NeedsRetry: return "NeedsRetry";
		default: return "Unknown";
	}
}

std::ostream &operator<<(std::ostream &out, const Action::Status &state) {

	return out << actionStatusName(state);
}


bool Action::debug() {
	return owner && owner->debug();
}

void Action::release() {
	--refs;
	if (refs < 0) {
		NB_MSG << "detected potential double delete of " << *this << "\n";
	}
	if (refs == 0)
		delete this;
}


void Action::suspend() {
	if (status == Suspended) return;
	if (status != Running) {
		DBG_M_ACTIONS << owner->getName() << " suspend called when action is in state " << status << "\n";
	}
	saved_status = status;
	status = Suspended;
}

void Action::resume() {
	//assert(status == Suspended);
	status = saved_status;
	DBG_M_ACTIONS << "resumed: (status: " << status << ") " << *this << "\n";
	if (status == Suspended) status = Running;
	owner->setNeedsCheck();
}

void Action::recover() {
	DBG_M_ACTIONS << "Action failed to remove itself after completing: " << *this << "\n";
	assert(status == Complete || status == Failed);
	owner->stop(this);
}
static std::stringstream *shared_ss = 0;
void Action::toString(char *buf, int buffer_size) {
	if (!shared_ss) shared_ss = new std::stringstream;
	shared_ss->clear();
	shared_ss->str("");
	*shared_ss << *this;
	snprintf(buf, buffer_size, "%s", shared_ss->str().c_str());
}

void Action::abort() {
	owner->stop(this);
}