
#include <iostream>
#include "Logger.h"
#include "DebugExtra.h"
#include "Action.h"
#include "MachineInstance.h"
#include <boost/thread/mutex.hpp>
#include <list>

std::list<Trigger*> all_triggers;
static boost::recursive_mutex trigger_list_mutex;

uint64_t nowMicrosecs();

class TriggerInternals {
public:
	std::list<Action*>holders;
	uint64_t start_time;
	uint64_t last_report;
	TriggerInternals() {
		start_time = nowMicrosecs();
		last_report = 0;
	}
};

void Trigger::report(const char *msg) {
	uint64_t now = nowMicrosecs();
	if (now - _internals->last_report > 1000000) {
		NB_MSG << name << " " << msg << "\n";
		_internals->last_report = now;
	}
}

uint64_t Trigger::startTime() { return _internals->start_time; }


void addTrigger(Trigger *t) {
	boost::recursive_mutex::scoped_lock scoped_lock(trigger_list_mutex);
	all_triggers.push_back(t);
if (t->getName() == "OCR_StatusLED.off") {
int x = 1;
}
}

void removeTrigger(Trigger *t) {
	boost::recursive_mutex::scoped_lock scoped_lock(trigger_list_mutex);
	all_triggers.remove(t);
}

void Trigger::addHolder(Action *h) {
	_internals->holders.push_back(h);
}

void Trigger::removeHolder(Action *h) {
	_internals->holders.remove(h);
}

char *Trigger::getTriggers() {
	std::stringstream ss;
	{
		boost::recursive_mutex::scoped_lock scoped_lock(trigger_list_mutex);
		std::list<Trigger*>::iterator iter = all_triggers.begin();
		while (iter != all_triggers.end()) {
			Trigger *t = *iter++;
			ss << t->getName() << " (" << t->refs;
			if (t->_internals->holders.size()) {
				ss << ":";
				std::list<Action*>::iterator h_iter = t->_internals->holders.begin();
				while (h_iter != t->_internals->holders.end()) {
					ss << *(*h_iter++) << " ";
				}
			}
			ss <<	")\n";
		}
	}
	if (!ss.str().length()) return 0;
	size_t len = ss.str().length();
	char *res = new char[len+1];
	strncpy(res, ss.str().c_str(), len);
	res[len] = 0;
	return res;
}

Trigger::Trigger() : _internals(0), name("inactive"), seen(false), is_active(false), owner(0), deleted(false), refs(1) { 
	_internals = new TriggerInternals;
	addTrigger(this); 
}

Trigger::Trigger(const std::string &n) : _internals(0), name(n), seen(false), is_active(true), owner(0), deleted(false), refs(1) { 
	_internals = new TriggerInternals;
	addTrigger(this); }

Trigger::Trigger(const Trigger &o) : _internals(0), name(o.name), seen(o.seen), is_active(o.is_active), owner(o.owner), deleted(false), refs(1) { 
	_internals = new TriggerInternals;
	addTrigger(this); 
}

Trigger &Trigger::operator=(const Trigger &o) { 
		name = o.name;
		seen = o.seen;
		is_active = o.is_active;
        owner = o.owner;
        // note: refs does not change
		return *this;
}

Trigger::~Trigger() {
	//NB_MSG << "Removing trigger " << name << "\n";
	removeTrigger(this);
}

Trigger* Trigger::retain() {
	int holders = _internals->holders.size();
	if (getName().compare(0, strlen("OCR_StatusLED"), "OCR_StatusLED")  == 0) { 
		int x = 1; 
	}
	++refs;
	if (refs == 0) { NB_MSG << "Trigger reference count is 0 after increment\n"; }
	return this;
}
Trigger *Trigger::release() {
	int holders = _internals->holders.size();
	if (getName().compare(0, strlen("OCR_StatusLED"), "OCR_StatusLED")  == 0) { 
		int x = 1; 
	}
	assert(refs>0);
	if (--refs == 0)
		delete this;
	return 0;
}

/* setTrigger does not do a retain on the trigger. It is expected to be called from a factory that returns a new trigger */
void Action::setTrigger(Trigger *t) { 
	if (!trigger && !t) return;
	if (trigger && trigger == t) {
		NB_MSG << "Attempt to set trigger " << t->getName() << " when it is already set";
		return; 
	}
	if (trigger && t) {
		NB_MSG << "Attempt to set trigger " 
			<< t->getName() << " when another trigger " 
			<< trigger->getName() << " is already set";
		trigger->removeHolder(this);
		trigger->release(); 
		trigger = 0;
	}
	if (t) {
		t->addHolder(this);
	}
}

Trigger *Action::getTrigger() const { return trigger; }

void Action::disableTrigger() { if (trigger) trigger->disable(); }


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

Action::~Action(){
	//NB_MSG << "Removing action " << *this << "\n";
	if (trigger) {
		if (trigger->getName().rfind("_done") != std::string::npos && !trigger->fired()) {
			NB_MSG << "Trigger " << trigger->getName() << " on " << owner->getName() << " has not fired\n";
		}
		trigger->removeHolder(this);
		trigger->release();
		trigger = 0;
	}
}


bool Action::debug() {
	return owner && owner->debug();
}

void Action::release() {
	--refs;
	if (refs < 0) {
		NB_MSG << "detected potential double delete of " << *this << "\n";
	}
	if (trigger) {
		trigger->removeHolder(this);
		trigger->release();
		trigger = 0;
	}
	if (refs == 0)
		delete this;
}

bool Action::complete() {
	if(status == Running || status == Suspended) status = checkComplete();
	return (status == Complete || status == Failed);
}
bool Action::running() {
	if (status == New) status = run();
	if(status == Running || status == Suspended) status = checkComplete();
	return (status == Running || status == New);
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

Action::Status Action::operator()() {
	if (status == New) {
		start_time = microsecs();
		status = Running;
	}
	status = run();
	return status;
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
