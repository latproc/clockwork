#ifndef __CONDITION_HANDLER_H
#define __CONDITION_HANDLER_H

#include <string>
#include <lib_clockwork_client/includes.hpp>
// #include "PredicateOperator.hpp"
#include "Expression.h"
// // #include "value.h"

class Action;
class Trigger;
class ConditionHandler {
public:
	ConditionHandler(const ConditionHandler &other);
	ConditionHandler &operator=(const ConditionHandler &other);
	ConditionHandler() : timer_val(0), timer_op(opGE), action(0), trigger(0), uses_timer(false), triggered(false) {}

    bool check(MachineInstance *machine);
    void reset();

	Condition condition;
	std::string command_name;
	std::string flag_name;
	Value timer_val;
  PredicateOperator timer_op;
	Action *action;
	Trigger *trigger;
	bool uses_timer;
	bool triggered;
};

#endif
