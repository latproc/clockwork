#ifndef __TRANSITION_H__
#define __TRANSITION_H__

#include <lib_clockwork_client/includes.hpp>
#include "State.h"
// // #include "Message.h"

class Condition;
class Predicate;
class Transition {
public:
	Transition(State s, State d, Message t, Predicate *p=0, bool abort_semantics = true);
	Transition(const Transition &other);
	Transition &operator=(const Transition &other);
	~Transition();

	State source;
	State dest;
	Message trigger;
	Condition *condition;
	bool abort_on_failure;
};

#endif
