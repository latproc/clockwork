
#include "StableState.h"
#include "MachineInstance.h"
#include <algorithm>

std::ostream &operator<<(std::ostream &out, const StableState &ss) { return ss.operator<<(out); }

StableState::~StableState() {
    if (trigger) {
        if (trigger->enabled()) {
            trigger->disable();
        }
        trigger = trigger->release();
    }
    if (subcondition_handlers) {
        subcondition_handlers->clear();
        delete subcondition_handlers;
    }
}

StableState::StableState(const StableState &other)
    : state_name(other.state_name), condition(other.condition), uses_timer(other.uses_timer),
      timer_val(0), trigger(0), subcondition_handlers(0), owner(other.owner) {
    if (other.subcondition_handlers) {
        subcondition_handlers = new std::list<ConditionHandler>;
        std::copy(other.subcondition_handlers->begin(), other.subcondition_handlers->end(),
                  back_inserter(*subcondition_handlers));
        std::copy(other.timer_predicates.begin(), other.timer_predicates.end(),
                  back_inserter(timer_predicates));
    }
}

void StableState::collectTimerPredicates() {
    if (!subcondition_handlers) {
        return;
    }
    assert(timer_predicates.size() == 0);
    std::list<ConditionHandler>::const_iterator iter = subcondition_handlers->begin();
    while (iter != subcondition_handlers->end()) {
        const ConditionHandler &ch = *iter++;
        ch.condition.predicate->findTimerClauses(timer_predicates);
    }
    std::list<Predicate *>::const_iterator ti = timer_predicates.begin();
    while (ti != timer_predicates.end()) {
        std::cout << "subcondition on " << state_name << " timer clause: " << *(*ti++) << "\n";
    }
}

void StableState::triggerFired(Trigger *trig) {
    if (owner) {
        owner->setNeedsCheck();
    }
}
