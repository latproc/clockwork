#include "Transition.h"
#include "State.h"
#include "Expression.h"

Transition::Transition(State s, State d, Message t, Predicate *p, bool abort_semantics)
		: source(s), dest(d),trigger(t), condition(0), abort_on_failure(abort_semantics) {
	if (p) {
		condition = new Condition(p);
	}
}

Transition::Transition(const Transition &other) : source(other.source), dest(other.dest), trigger(other.trigger), condition(0), abort_on_failure(other.abort_on_failure) {
	if (other.condition && other.condition->predicate) {
		condition = new Condition(new Predicate(*other.condition->predicate));
	}
}

Transition::~Transition() {
	delete condition;
}

Transition &Transition::operator=(const Transition &other) {
	source = other.source;
	dest = other.dest;
	trigger = other.trigger;
	if (other.condition) {
		delete condition;
		condition = new Condition(other.condition->predicate);
	}
	abort_on_failure = other.abort_on_failure;
	return *this;
}
