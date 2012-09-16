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

#ifndef latprocc_Action_h
#define latprocc_Action_h

#include <stdlib.h>
#include <string.h>
#include <vector>
#include <set>
#include <sys/types.h>
#include <boost/foreach.hpp>
#include "Message.h"

typedef std::vector<std::string> ActionParameterList;
class MachineInstance;

class Action;
struct ActionTemplate {
    virtual ~ActionTemplate() {}
    virtual Action *factory(MachineInstance *mi) = 0;
    virtual std::ostream & operator<<(std::ostream &out) const { return out << "(ActionTemplate)"; }
};
std::ostream &operator<<(std::ostream &out, const ActionTemplate &a);

class Trigger;
struct TriggerOwner {
    virtual void triggerFired(Trigger *trigger) {}
};

struct Trigger {
	Trigger() : name("inactive"), seen(false), is_active(false), owner(0), deleted(false) {}
	Trigger(const std::string &n) : name(n), seen(false), is_active(true), owner(0), deleted(false) {}
	Trigger(const Trigger &o) : name(o.name), seen(o.seen), is_active(o.is_active), owner(o.owner), deleted(false) {}
	Trigger &operator=(const Trigger &o) { 
		name = o.name;
		seen = o.seen;
		is_active = o.is_active;
        owner = o.owner;
		return *this;
	}
    void setOwner(TriggerOwner *new_owner) { owner = new_owner; }
	bool enabled() const { return is_active; }
	bool fired() const { return seen; }
	void fire() { seen = true; if (owner) owner->triggerFired(this); }
	void reset() { seen = false; }
	void enable() { is_active = true; }
	void disable() { is_active = false; }
	const std::string& getName() const { return name; }
	bool matches(const std::string &event) { 
		return is_active && event == name;
	}
	
protected:
	std::string name;
	bool seen;
	bool is_active;
    TriggerOwner *owner;
	bool deleted;
};

// an action is started by operator(). If the action successfully starts, 
// running() will return true. When the action is complete, complete() will
// return true;
struct Action {
public:
    Action(MachineInstance *m = 0)  : refs(1), owner(m), error_str(""), result_str(""), status(New), saved_status(Running), blocked(0) {}
    virtual ~Action(){ }
    
    Action*retain() { ++refs; return this; }
    void release();
    
    const char *error() { const char *res = error_str.get(); return (res) ? res : "" ;  }
    const char *result() { const char *res = result_str.get(); return (res) ? res : "" ;  }

	enum Status { New, Running, Complete, Failed, Suspended, NeedsRetry };
    
    Status operator()() {
		if (status == New) status = Running;
		status = run();
		return status;
	}
    bool complete() {
		if(status == Running || status == Suspended) status = checkComplete();
		return (status == Complete || status == Failed);
	}
    bool running() { 
		if(status == Running || status == Suspended) status = checkComplete();
		return status == Running; 
	}
	bool suspended() { return status == Suspended; }
	void suspend();
	void resume();
	void recover(); // debug TBD

	bool debug();

	Status getStatus() { return status; }
	Action *blocker() { return blocked; }
	bool isBlocked() { return blocked != 0; }
	void setBlocker(Action *a) { blocked = a; }
	
	void setTrigger(const Trigger &t) { trigger = t; }
	Trigger &getTrigger() { return trigger; }
	void disableTrigger() { trigger.disable(); }

	MachineInstance *getOwner() { return owner; }
    
    virtual std::ostream & operator<<(std::ostream &out) const { return out << "(Action)"; }
    
protected:
	virtual Status run() = 0;
    virtual Status checkComplete() = 0;
    int refs;
    MachineInstance *owner;
	CStringHolder error_str;
	CStringHolder result_str;
	Status status;
	Status saved_status; // used when an action is suspended
	Action *blocked; // blocked on this action
	Trigger trigger;
};

struct IOComponent;
struct TriggeredAction : public Action {
	TriggeredAction(MachineInstance *m) : Action(m), has_triggered(false) {}
	enum TriggerType { TT_ON, TT_OFF };
	void addComponent(IOComponent *ioc, TriggerType which) {
		switch(which) {
			case TT_ON: 
				trigger_on.insert(ioc);
				break;
			case TT_OFF:
				trigger_off.insert(ioc);
				break;
		}
	}
	void removeComponent(IOComponent *ioc, TriggerType which) {
		switch(which) {
			case TT_ON: 
				trigger_on.erase(ioc);
				break;
			case TT_OFF:
				trigger_off.erase(ioc);
				break;
		}
	}
	bool depends(IOComponent *iod) {
		return trigger_on.count(iod) || trigger_off.count(iod);
	}
	// active() indicates the action has triggered. It will automatically 
	// reset the trigger only when one of the dependent inputs changes
	bool active();
	// set() only fires once and resets only when reset() is called.
	bool set() {
		if (has_triggered) return true;
		has_triggered = active();
		return true;
	}
	void reset() { has_triggered = false; }
	virtual bool usesTimer() { return false; }
	
	std::set<IOComponent *> trigger_on;
	std::set<IOComponent *> trigger_off;
	bool has_triggered;
};

std::ostream &operator<<(std::ostream &out, const Action &);



#endif
