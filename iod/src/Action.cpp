
#include "Action.h"
#include "AbortAction.h"
#include "DebugExtra.h"
#include "Logger.h"
#include "MachineInstance.h"
#include "MessageLog.h"
#include <boost/thread/mutex.hpp>
#include <iostream>

void ActionTemplate::toC(std::ostream &out, std::ostream &vars) const { operator<<(out); }

Action::Action(MachineInstance *m)
    : refs(1), owner(m), error_str(""), result_str(""), status(New), saved_status(Running),
      blocked(0), trigger(0), started_(false), timeout_msg(0), error_msg(0) {}

Action::Action(MachineInstance *m, Trigger *t)
    : refs(1), owner(m), error_str(""), result_str(""), status(New), saved_status(Running),
      blocked(0), trigger(t), started_(false), timeout_msg(0), error_msg(0) {}

Action::Status Action::getStatus() { return status; }
Action *Action::blocker() { return blocked; }
bool Action::isBlocked() { return blocked != 0; }
void Action::setBlocker(Action *a) { blocked = a; }

bool Action::can_continue() const { return !trigger || trigger->fired(); }

/* setTrigger does not do a retain on the trigger. It is expected to be called from a factory that returns a new trigger */
void Action::setTrigger(Trigger *t) {
    if (!trigger && !t) {
        return;
    }
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

void Action::disableTrigger() {
    if (trigger) {
        trigger->disable();
    }
}

bool Action::started() const { return started_; }
void Action::start() {
    assert(!started_);
    started_ = true;
}
void Action::stop() { started_ = false; }
bool Action::aborted() const { return aborted_; }
void Action::reset() {
    status = New;
    error_str = "";
    result_str = "";
    saved_status = Running;
    blocked = 0;
    started_ = false;
    cleanupTrigger();
}

const char *actionStatusName(const Action::Status &state) {
    switch (state) {
    case Action::New:
        return "New";
    case Action::Running:
        return "Running";
    case Action::Failed:
        return "Failed";
    case Action::Complete:
        return "Complete";
    case Action::Suspended:
        return "Suspended";
    case Action::NeedsRetry:
        return "NeedsRetry";
    default:
        return "Unknown";
    }
}

std::ostream &operator<<(std::ostream &out, const Action::Status &state) {

    return out << actionStatusName(state);
}

Action::~Action() {
    //  DBG_ACTIONS << owner->fullName() << " removing action " << *this << "\n";
    if (trigger) {
        size_t len = trigger->getName().length();
        if (len > 5 && trigger->getName().substr(len - 5) == "_done" && !trigger->fired()) {
            std::stringstream ss;
            ss << "Trigger " << trigger->getName() << " on " << owner->getName()
               << " has not fired\n";
            MessageLog::instance()->add(ss.str().c_str());
        }
        cleanupTrigger();
    }
}

bool Action::debug() { return owner && owner->debug(); }

void Action::release() {
    --refs;
    if (refs < 0) {
        NB_MSG << "detected potential double delete of " << *this << "\n";
    }
    if (refs == 0 && trigger) {
        trigger->removeHolder(this);
        trigger->release();
        trigger = 0;
    }
    if (refs == 0) {
        delete this;
    }
}

void Action::setError(const std::string &err) {
    char *buf = strdup(err.c_str());
    error_str = buf;
}

bool Action::complete() {
    if (status == Running || status == Suspended) {
        status = checkComplete();
    }
    return (status == Complete || status == Failed);
}
bool Action::running() {
    if (status == New) {
        status = run();
    }
    if (status == Running || status == Suspended) {
        status = checkComplete();
    }
    return (status == Running || status == New);
}

void Action::suspend() {
    if (status == Suspended) {
        return;
    }
    if (status != Running) {
        DBG_M_ACTIONS << owner->getName() << " suspend called when action is in state " << status
                      << "\n";
    }
    saved_status = status;
    status = Suspended;
}

void Action::resume() {
    //assert(status == Suspended);
    status = saved_status;
    DBG_M_ACTIONS << "resumed: (status: " << status << ") " << *this << "\n";
    if (status == Suspended) {
        status = Running;
    }
    owner->setNeedsCheck();
}

Action::Status Action::operator()() {
    reset();
    start_time = microsecs();
    status = Running; // important because run() checks the current state
    status = run();
    if (status == Failed) {
        if (error_msg) {
            AbortActionTemplate aat(true, Value{error_msg->get()});
            auto *aa = (AbortAction *)aat.factory(owner);
            owner->enqueueAction(aa);
        }
        else if (timeout_msg) {
            AbortActionTemplate aat(true, Value{timeout_msg->get()});
            auto *aa = (AbortAction *)aat.factory(owner);
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
    if (!shared_ss) {
        shared_ss = new std::stringstream;
    }
    shared_ss->clear();
    shared_ss->str("");
    *shared_ss << *this;
    snprintf(buf, buffer_size, "%s", shared_ss->str().c_str());
}

void Action::abort() { aborted_ = true; }
