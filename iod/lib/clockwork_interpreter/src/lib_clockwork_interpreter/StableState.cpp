
#include "StableState.h"
#include "MachineInstance.h"
#include "State.h"
#include <algorithm>

StableState::StableState()
    : state_name(""), uses_timer(false), timer_val(0), trigger(0),
      subcondition_handlers(0), owner(0) {}

StableState::StableState(const char *s, Predicate *p, MachineInstance *o)
    : state_name(s), condition(p), uses_timer(false), timer_val(0), trigger(0),
      subcondition_handlers(0), owner(0) {
    uses_timer = p->usesTimer(timer_val);
}

StableState::StableState(const char *s, Predicate *p, Predicate *q,
                         MachineInstance *o)
    : state_name(s), condition(p), uses_timer(false), timer_val(0), trigger(0),
      subcondition_handlers(0), owner(o) {
    uses_timer = p->usesTimer(timer_val);
}

std::ostream &operator<<(std::ostream &out, const StableState &ss) {
    return ss.operator<<(out);
}

StableState::~StableState() {
    if (subcondition_handlers) {
        subcondition_handlers->clear();
        delete subcondition_handlers;
    }
}

StableState::StableState(const StableState &other)
    : state_name(other.state_name), condition(other.condition),
      uses_timer(other.uses_timer), timer_val(0), trigger(0),
      subcondition_handlers(0), owner(other.owner), _name(other.state_name) {
    if (other.subcondition_handlers) {
        subcondition_handlers = new std::list<ConditionHandler>;
        std::copy(other.subcondition_handlers->begin(),
                  other.subcondition_handlers->end(),
                  back_inserter(*subcondition_handlers));
        std::copy(other.timer_predicates.begin(), other.timer_predicates.end(),
                  back_inserter(timer_predicates));
    }
}

StableState &StableState::operator=(const StableState *other) {
    state_name = other->state_name;
    condition = other->condition;
    uses_timer = other->uses_timer;
    timer_val = 0;
    trigger = 0;
    std::list<ConditionHandler>::iterator iter =
        other->subcondition_handlers->begin();
    while (iter != other->subcondition_handlers->end()) {
        owner = other->owner;
    }
    return *this;
}

//bool StableState::isSpecial() const{
//  const State *s = owner->getStateMachine()->findState(state_name.c_str());
//  assert(s);
//
//  return s->isPrivate() || s->isTransitional();
//}
//bool StableState::isPrivate() const{
//  const State *s = owner->getStateMachine()->findState(state_name.c_str());
//  return s->isPrivate();
//}
//bool StableState::isTransitional() const{
//  const State *s = owner->getStateMachine()->findState(state_name.c_str());
//  return s->isTransitional();
//}

std::ostream &StableState::displayName(std::ostream &out) const {
    //  if (isPrivate())
    //      return out << "|" << state_name << ((isTransitional())?"*":"") << "|";
    //  else
    return out << state_name; // << ((isTransitional())?"*":"");
}

std::ostream &StableState::operator<<(std::ostream &out) const {
    //  if (isPrivate())
    //      return out << "|" << state_name << ((isTransitional())?"*":"") << "|";
    //  else
    return out << state_name; // << ((isTransitional())?"*":"");
}

void StableState::triggerFired(Trigger *trig) {
    if (owner) {
        owner->setNeedsCheck();
    }
}

void StableState::collectTimerPredicates() {
    if (!subcondition_handlers) {
        return;
    }
    assert(timer_predicates.size() == 0);
    std::list<ConditionHandler>::const_iterator iter =
        subcondition_handlers->begin();
    while (iter != subcondition_handlers->end()) {
        const ConditionHandler &ch = *iter++;
        ch.condition.predicate->findTimerClauses(timer_predicates);
    }
    std::list<Predicate *>::const_iterator ti = timer_predicates.begin();
    while (ti != timer_predicates.end()) {
        std::cout << "subcondition on " << state_name
                  << " timer clause: " << *(*ti++) << "\n";
    }
}

Value StableState::checkSubconditionTimes(const Value &schedule_time,
                                          bool include_trace) {
    Value earliest(schedule_time);
    std::list<ConditionHandler>::iterator iter = subcondition_handlers->begin();
    while (iter != subcondition_handlers->end()) {
        ConditionHandler *ch = &(*iter++);
        if (timer_predicates.size()) {
            earliest = std::min(earliest,
                                owner->earliestScheduleTime(timer_predicates));
        }
        if (include_trace) {
            std::stringstream ss;
            ss << owner->getCurrentStateString(owner) << "->" << state_name
               << " " << *ch->condition.predicate;
            owner->setValue("TRACE", Value(ss.str(), Value::t_string));
        }
    }
    return earliest;
}
