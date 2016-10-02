
#include <iostream>
#include "Logger.h"
#include "DebugExtra.h"
#include "Action.h"
#include "MachineInstance.h"
#include <boost/thread/mutex.hpp>
#include <list>
#include "MessageLog.h"
#include "AbortAction.h"

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
	if (now - _internals->last_report > 1000) {
//		DBG_ACTIONS << name << " " << msg << "\n";
		_internals->last_report = now;
	}
}

uint64_t Trigger::startTime() { return _internals->start_time; }


void addTrigger(Trigger *t) {
	boost::recursive_mutex::scoped_lock scoped_lock(trigger_list_mutex);
	all_triggers.push_back(t);
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

Trigger::Trigger(const std::string &n) : _internals(0), name(n), seen(false), owner(0),
		deleted(false), refs(1), is_active(true) {
	_internals = new TriggerInternals;
	addTrigger(this); }


Trigger::~Trigger() {
	removeTrigger(this);
	delete _internals;
}

Trigger* Trigger::retain() {
	++refs;
	if (refs == 0) { NB_MSG << "Trigger reference count is 0 after increment\n"; }
	return this;
}
Trigger *Trigger::release() {
	assert(refs>0);
	if (--refs == 0)
		delete this;
	return 0;
}

void Trigger::setOwner(TriggerOwner *new_owner) { owner = new_owner; }
bool Trigger::enabled() const { return is_active; }
bool Trigger::fired() const { return seen; }
void Trigger::fire() {
	if (seen) return;
	seen = true;
	if (owner) owner->triggerFired(this);
}
void Trigger::disable() {
	is_active = false;
}
const std::string& Trigger::getName() const { return name; }
bool Trigger::matches(const std::string &event) {
	return is_active && event == name;
}


Action::Action(MachineInstance *m)
: refs(1), owner(m), error_str(""), result_str(""), status(New),
saved_status(Running), blocked(0), trigger(0), started_(false), timeout_msg(0), error_msg(0) {
}

Action::Action(MachineInstance *m, Trigger *t)
: refs(1), owner(m), error_str(""), result_str(""), status(New),
saved_status(Running), blocked(0), trigger(t), started_(false), timeout_msg(0), error_msg(0) {
}


Action::Status Action::getStatus() { return status; }
Action *Action::blocker() { return blocked; }
bool Action::isBlocked() { return blocked != 0; }
void Action::setBlocker(Action *a) { blocked = a; }

/* setTrigger does not do a retain on the trigger. It is expected to be called from a factory that returns a new trigger */
void Action::setTrigger(Trigger *t) { 
	if (!trigger && !t) return;
	if (trigger && trigger == t) {
		DBG_ACTIONS << "Attempt to set trigger " << t->getName() << " when it is already set";
		return; 
	}
	cleanupTrigger();
	if (t) {
		t->addHolder(this);
		t->setOwner(this);
		trigger = t;
	}
}

void Action::cleanupTrigger() {
	if (trigger) {
		trigger->removeHolder(this);
		trigger->release();
		trigger = 0;
	}
}

Trigger *Action::getTrigger() const { return trigger; }

void Action::disableTrigger() { if (trigger) trigger->disable(); }

bool Action::started() { return started_; }
void Action::start() { assert(!started_); started_ = true; }
void Action::stop() { started_ = false; }
bool Action::aborted() { return aborted_; }
void Action::reset() {
	status = New;
	error_str=""; result_str="";
	saved_status=Running; blocked=0;
	started_=false;
	cleanupTrigger();
}

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
//	DBG_ACTIONS << owner->fullName() << " removing action " << *this << "\n";
	if (trigger) {
		size_t len = trigger->getName().length();
		if (len > 5 &&
				trigger->getName().substr(len-5) == "_done"
				&& !trigger->fired()) {
			std::stringstream ss;
			ss << "Trigger "
			<< trigger->getName()
			<< " on "
			<< owner->getName() << " has not fired\n";
			MessageLog::instance()->add(ss.str().c_str());
		}
		cleanupTrigger();
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
	if (refs ==0 && trigger) {
		trigger->removeHolder(this);
		trigger->release();
		trigger = 0;
	}
	if (refs == 0)
		delete this;
}

void Action::setError(const std::string &err) {
	char *buf = strdup(err.c_str());
	error_str = buf;
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
	reset();
	start_time = microsecs();
	status = Running; // important because run() checks the current state
	status = run();
	if (status == Failed) {
		if (error_msg) {
			AbortActionTemplate aat(true, error_msg->get());
			AbortAction *aa = (AbortAction*)aat.factory(owner);
			owner->enqueueAction(aa);
		}
		else if (timeout_msg) {
			AbortActionTemplate aat(true, timeout_msg->get());
			AbortAction *aa = (AbortAction*)aat.factory(owner);
			owner->enqueueAction(aa);
		}
	}
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
	aborted_ = true;
}
