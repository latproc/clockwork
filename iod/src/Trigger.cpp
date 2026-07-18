#include "Action.h"
#include "DebugExtra.h"
#include "Logger.h"
#include "MachineInstance.h"
#include "MessageLog.h"
#include "Trigger.h"
#include <boost/thread/mutex.hpp>
#include <iostream>
#include <list>

std::list<Trigger *> all_triggers;
static boost::recursive_mutex trigger_list_mutex;

class TriggerInternals {
  public:
    std::list<Action *> holders;
    uint64_t start_time;
    uint64_t last_report;
    TriggerInternals() {
        start_time = microsecs();
        last_report = 0;
    }
};

void Trigger::report(const char *msg) {
    uint64_t now = microsecs();
    if (now - _internals->last_report > 1000) {
        //      DBG_ACTIONS << name << " " << msg << "\n";
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

size_t Trigger::liveCount() {
    boost::recursive_mutex::scoped_lock scoped_lock(trigger_list_mutex);
    return all_triggers.size();
}

void Trigger::addHolder(Action *h) { _internals->holders.push_back(h); }

void Trigger::removeHolder(Action *h) { _internals->holders.remove(h); }

char *Trigger::getTriggers() {
    std::stringstream ss;
    {
        boost::recursive_mutex::scoped_lock scoped_lock(trigger_list_mutex);
        auto iter = all_triggers.begin();
        while (iter != all_triggers.end()) {
            Trigger *t = *iter++;
            ss << t->getName() << " (" << t->refs;
            if (!t->_internals->holders.empty()) {
                ss << ":";
                auto h_iter = t->_internals->holders.begin();
                while (h_iter != t->_internals->holders.end()) {
                    ss << *(*h_iter++) << " ";
                }
            }
            ss << ")\n";
        }
    }
    if (!ss.str().length()) {
        return 0;
    }
    size_t len = ss.str().length();
    char *res = new char[len + 1];
    strncpy(res, ss.str().c_str(), len);
    res[len] = 0;
    return res;
}

Trigger::Trigger(const std::string &n)
    : _internals(0), name(n), seen(false), owner(0), deleted(false), refs(1), is_active(true) {
    _internals = new TriggerInternals;
    addTrigger(this);
}

Trigger::Trigger(TriggerOwner *own, const std::string &n)
    : _internals(0), name(n), seen(false), owner(own), deleted(false), refs(1), is_active(true) {
    _internals = new TriggerInternals;
    addTrigger(this);
}

Trigger::~Trigger() {
    removeTrigger(this);
    delete _internals;
}

Trigger *Trigger::retain() {
    ++refs;
    if (refs == 0) {
        NB_MSG << "Trigger reference count is 0 after increment\n";
    }
    return this;
}
Trigger *Trigger::release() {
    assert(refs > 0);
    if (--refs == 0) {
        delete this;
    }
    return 0;
}
std::ostream &Trigger::operator<<(std::ostream &out) const {
    out << "Trigger " << name << " fired: " << seen << " active: " << is_active;
    return out;
}

std::ostream &operator<<(std::ostream &out, const Trigger &t) { return t.operator<<(out); }

void Trigger::setOwner(TriggerOwner *new_owner) { owner = new_owner; }
bool Trigger::enabled() const { return is_active; }
bool Trigger::fired() const { return seen; }
void Trigger::fire() {
    if (seen) {
        return;
    }
    seen = true;
    if (owner) {
        owner->triggerFired(this);
    }
}
void Trigger::disable() { is_active = false; }
const std::string &Trigger::getName() const { return name; }
bool Trigger::matches(const std::string &event) { return is_active && event == name; }
