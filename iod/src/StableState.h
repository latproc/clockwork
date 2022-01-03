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
		owner(0), name("") { }

	StableState(const char *s, Predicate *p) : state_name(s),
		condition(p), uses_timer(false), timer_val(0),
			trigger(0), subcondition_handlers(0), owner(0), name(s)
 	{
		uses_timer = p->usesTimer(timer_val);
	}

	StableState(const char *s, Predicate *p, Predicate *q)
		: state_name(s),
			condition(p), uses_timer(false), timer_val(0), trigger(0),
			subcondition_handlers(0), owner(0), name(s) {
		uses_timer = p->usesTimer(timer_val);
	}

    ~StableState();

    bool operator<(const StableState &other) const {  // used for std::sort
        if (!condition.predicate || !other.condition.predicate) return false;
        return condition.predicate->priority < other.condition.predicate->priority;
    };
    std::ostream &operator<< (std::ostream &out)const { return out << name; }
	StableState& operator=(const StableState* other);
    StableState (const StableState &other);

    void setOwner(MachineInstance *m) { owner = m; }
    void fired(Trigger *trigger);
    void triggerFired(Trigger *trigger);
    void refreshTimer();
    void collectTimerPredicates();

	std::string state_name;
	Condition condition;
	bool uses_timer;
	Value timer_val;
	Trigger *trigger;
	std::list<ConditionHandler> *subcondition_handlers;
    MachineInstance *owner;
	Value name;
    std::list<Predicate*>timer_predicates;
};
std::ostream &operator<<(std::ostream &out, const StableState &ss);


#endif
