#ifndef __TRANSITION_H__
#define __TRANSITION_H__

#include "State.h"
#include "Message.h"

class Condition;
class Predicate;
class Transition {
public:
    State source;
    State dest;
    Message trigger;
    Condition *condition;
    Transition(State s, State d, Message t, Predicate *p=0);
    Transition(const Transition &other);
    Transition &operator=(const Transition &other);
    ~Transition();
};

#endif
