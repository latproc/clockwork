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

#ifndef cwlang_Action_h
#define cwlang_Action_h

#include "Expression.h"
#include "Message.h"
#include <boost/foreach.hpp>
#include <cstdlib>
#include <cstring>
#include <set>
#include <sys/types.h>
#include <vector>

typedef std::vector<std::string> ActionParameterList;
class MachineInstance;

class Action;
class ActionTemplate {
  public:
    ActionTemplate() = default;
    virtual ~ActionTemplate() = default;
    virtual void toC(std::ostream &out, std::ostream &vars) const;
    virtual Action *factory(MachineInstance *mi) = 0;
    virtual std::ostream &operator<<(std::ostream &out) const { return out << "(ActionTemplate)"; }
    virtual bool canBlock() const { return false; }
};
std::ostream &operator<<(std::ostream &out, const ActionTemplate &a);

class TriggerInternals;
class Trigger;
struct TriggerOwner {
    virtual ~TriggerOwner() = default;
    virtual void triggerFired(Trigger *trigger){};
};

class Trigger {
  public:
    explicit Trigger(const std::string &n);
    Trigger(TriggerOwner *, const std::string &n);

    virtual ~Trigger();
    Trigger *retain();
    virtual Trigger *release();
    static char *getTriggers();
    void addHolder(Action *h);
    void removeHolder(Action *h);

    void setOwner(TriggerOwner *new_owner);
    bool enabled() const;
    bool fired() const;
    void fire();
    void disable();
    virtual const std::string &getName() const;
    //const std::string &getName();
    bool matches(const std::string &event);

    uint64_t startTime();
    void report(const char *message);
    int getRefs() const { return refs; }

    std::ostream &operator<<(std::ostream &out) const;

  protected:
    TriggerInternals *_internals;
    std::string name;
    bool seen;
    TriggerOwner *owner;
    bool deleted;
    int refs;

  private:
    bool is_active;

  public:
    // None of these constructors and assignment operators are
    // implemented because they are not supported
    Trigger() = delete;
    Trigger(const Trigger &o) = delete;
    Trigger &operator=(const Trigger &o) = delete;
};
std::ostream &operator<<(std::ostream &out, const Trigger &t);

// an action is started by operator(). If the action successfully starts,
// running() will return true. When the action is complete, complete() will
// return true;
class Action : public TriggerOwner {
  public:
    explicit Action(MachineInstance *m = nullptr);
    Action(MachineInstance *m, Trigger *t);
    ~Action() override;

    Action *retain() {
        ++refs;
        return this;
    }
    int references() const { return refs; }
    virtual void release();

    const char *error() {
        const char *res = error_str.get();
        return (res) ? res : "";
    }
    const char *result() {
        const char *res = result_str.get();
        return (res) ? res : "";
    }
    void setError(const std::string &err);

    enum Status { New, Running, Complete, Failed, Suspended, NeedsRetry };

    Status operator()();
    bool complete();
    bool running();
    bool suspended() { return status == Suspended; }
    void suspend();
    void resume();
    void recover(); // debug TBD
    void abort();
    virtual void reset(); // reinitialise an action for re-execution

    bool debug();

    Status getStatus();
    Action *blocker();
    bool isBlocked();
    void setBlocker(Action *a);

    void setTrigger(Trigger *t);
    Trigger *getTrigger() const;
    void disableTrigger();
    void cleanupTrigger();

    MachineInstance *getOwner() { return owner; }
    bool started() const;
    void start();
    void stop();
    bool aborted() const;

    virtual void toString(char *buf, int buffer_size);
    virtual std::ostream &operator<<(std::ostream &out) const { return out << "(Action)"; }

  protected:
    virtual Status run() = 0;
    virtual Status checkComplete() = 0;
    int refs;
    MachineInstance *owner = nullptr;
    CStringHolder error_str;
    CStringHolder result_str;
    Status status;
    Status saved_status; // used when an action is suspended
    Action *blocked = nullptr;     // blocked on this action
    Trigger *trigger;
    uint64_t start_time{};
    bool started_ = false;
    bool aborted_ = false;
    CStringHolder *timeout_msg = nullptr; // message to send on timeout
    CStringHolder *error_msg = nullptr;   // message to send on error
};

std::ostream &operator<<(std::ostream &out, const Action::Status &state);
const char *actionStatusName(const Action::Status &state);

class IOComponent;
class TriggeredAction : public Action {
  public:
    explicit TriggeredAction(MachineInstance *m) : Action(m), has_triggered(false) {}
    enum TriggerType { TT_ON, TT_OFF };
    void addComponent(IOComponent *ioc, TriggerType which) {
        switch (which) {
        case TT_ON:
            trigger_on.insert(ioc);
            break;
        case TT_OFF:
            trigger_off.insert(ioc);
            break;
        }
    }
    void removeComponent(IOComponent *ioc, TriggerType which) {
        switch (which) {
        case TT_ON:
            trigger_on.erase(ioc);
            break;
        case TT_OFF:
            trigger_off.erase(ioc);
            break;
        }
    }
    bool depends(IOComponent *iod) const { return trigger_on.count(iod) || trigger_off.count(iod); }
    // active() indicates the action has triggered. It will automatically
    // reset the trigger only when one of the dependent inputs changes
    bool active();
    // set() only fires once and resets only when reset() is called.
    bool set() {
        if (has_triggered) {
            return true;
        }
        has_triggered = active();
        return true;
    }
    void reset() override { has_triggered = false; }
    virtual bool usesTimer() { return false; }

    std::set<IOComponent *> trigger_on;
    std::set<IOComponent *> trigger_off;
    bool has_triggered;
};

std::ostream &operator<<(std::ostream &out, const Action &);

#endif
