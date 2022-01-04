#ifndef __CONDITION_HANDLER_H
#define __CONDITION_HANDLER_H

#include "Expression.h"
#include "value.h"
#include <string>

class Action;
class Trigger;
class ConditionHandler {
  public:
    ConditionHandler(const ConditionHandler &other);
    ConditionHandler &operator=(const ConditionHandler &other);
    ConditionHandler();
    ~ConditionHandler();

    bool check(MachineInstance *machine);
    void reset();
    const std::string &command_name() const { return _command_name; }
    void set_command_name(const std::string &cmd) { _command_name = cmd; }
    const std::string &flag_name() const { return _flag_name; }
    void set_flag_name(const std::string &flag) { _flag_name = flag; }

    Condition condition;
    Value timer_val;
    PredicateOperator timer_op;
    Trigger *trigger;
    bool uses_timer;
    bool triggered;

  private:
    std::string _command_name;
    std::string _flag_name;
};

#endif
