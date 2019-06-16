#ifndef __STABLESTATE_H_
#define __STABLESTATE_H_

#include <string>
#include <list>
#include <iostream>

#include "Action.h"
#include "Expression.h"
#include "value.h"
#include "ConditionHandler.h"

class MachineInstance;
class StableState : public TriggerOwner {
public:

	StableState() : state_name(""),
 		uses_timer(false), timer_val(0), trigger(0), subcondition_handlers(0),
		owner(0), _name("") { }

	StableState(const char *s, Predicate *p, MachineInstance *o) : state_name(s),
		condition(p), uses_timer(false), timer_val(0),
			trigger(0), subcondition_handlers(0), owner(0), _name(s)
 	{
		uses_timer = p->usesTimer(timer_val);
	}

	StableState(const char *s, Predicate *p, Predicate *q, MachineInstance *o)
		: state_name(s),
			condition(p), uses_timer(false), timer_val(0), trigger(0),
			subcondition_handlers(0), owner(o), _name(s){
		uses_timer = p->usesTimer(timer_val);
	}

	~StableState();

	bool operator<(const StableState &other) const {  // used for std::sort
			if (!condition.predicate || !other.condition.predicate) return false;
			return condition.predicate->priority < other.condition.predicate->priority;
	};
	std::ostream &operator<< (std::ostream &out)const;
	std::ostream &displayName (std::ostream &out)const;

	StableState& operator=(const StableState* other);
	StableState (const StableState &other);

	void setOwner(MachineInstance *m) { owner = m; }
	void fired(Trigger *trigger);
	void triggerFired(Trigger *trigger);
	void refreshTimer();

	bool isTransitional() const;
	bool isPrivate() const;
	bool isSpecial() const;

	const Value &name() { return _name; }
  void collectTimerPredicates();

	std::string state_name;
	Condition condition;
	bool uses_timer;
	Value timer_val;
	Trigger *trigger;
	std::list<ConditionHandler> *subcondition_handlers;
  MachineInstance *owner;
  std::list<Predicate*>timer_predicates;
private:
	Value _name;
};
std::ostream &operator<<(std::ostream &out, const StableState &ss);


#endif
