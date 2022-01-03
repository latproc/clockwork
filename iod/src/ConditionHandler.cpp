#include "ConditionHandler.h"
#include "MachineInstance.h"
#include "Logger.h"
#include "DebugExtra.h"

ConditionHandler::ConditionHandler() : timer_val(0), timer_op(opGE), trigger(0), uses_timer(false), triggered(false) {}

ConditionHandler::ConditionHandler(const ConditionHandler &other)
: condition(other.condition),
	timer_val(other.timer_val),
	trigger(other.trigger),
	uses_timer(other.uses_timer),
	triggered(other.triggered),
_command_name(other._command_name),
_flag_name(other._flag_name)
{
	if (trigger) trigger->retain();
}

ConditionHandler::~ConditionHandler() {
	if (trigger) trigger->release();
}

ConditionHandler &ConditionHandler::operator=(const ConditionHandler &other) {
	condition = other.condition;
	timer_val = other.timer_val;
	trigger = other.trigger;
	if (trigger) trigger->retain();
	uses_timer = other.uses_timer;
	triggered = other.triggered;
  _command_name = other._command_name;
  _flag_name = other._flag_name;
	return *this;
}

bool ConditionHandler::check(MachineInstance *machine) {
	if (!machine) return false;
	if (_command_name == "FLAG" ) {
		MachineInstance *flag = machine->lookup(_flag_name);
		if (!flag)
			std::cerr << machine->getName() << " error: flag " << _flag_name << " not found\n";
		else {
			if (condition(machine)) {
				if (strcmp("on", flag->getCurrentStateString())) {
					if (tracing() && machine->isTraceable()) {
						machine->resetTemporaryStringStream();
						machine->ss << machine->getCurrentStateString() << " " << *condition.predicate;
						machine->setValue("TRACE", Value(machine->ss.str(), Value::t_string));
					}
					const State *on = flag->state_machine->findState("on");
					if (!flag->isActive()) {
						flag->setState(*on);
					}
					else {
						// execute this state change once all other actions are complete
						SetStateActionTemplate ssat("SELF", "on" );
						flag->enqueueAction(ssat.factory(flag));
					}
				}
			}
			else if (strcmp("off", flag->getCurrentStateString())) {
				if (tracing() && machine->isTraceable()) {
					machine->resetTemporaryStringStream();
					machine->ss << machine->getCurrentStateString() << " " << *condition.predicate;
					machine->setValue("TRACE", Value(machine->ss.str(), Value::t_string));
				}
				const State *off = flag->state_machine->findState("off");
				if (!flag->isActive()) flag->setState(*off);
				else {
					// execute this state change once all other actions are complete
					SetStateActionTemplate ssat("SELF", "off" );
					SetStateAction *ssa = dynamic_cast<SetStateAction*>(ssat.factory(flag));
					flag->enqueueAction(ssa);
				}
			}
		}
	}
	else if (!triggered && ( (trigger && trigger->fired()) || !trigger) && condition(machine)) {
		triggered = true;
		DBG_AUTOSTATES << machine->getName() << " subcondition triggered: " << _command_name << " " << *(condition.predicate) << "\n";
		machine->execute(Message(_command_name.c_str()),machine);
		if (trigger) trigger->disable();
	}
	else {
		DBG_AUTOSTATES <<"condition: " << (*condition.predicate) << "\n";
		if (!trigger) { DBG_AUTOSTATES << "		condition does not have a timer\n"; }
		if (triggered) {DBG_AUTOSTATES <<"		 condition " << (condition.predicate) << " already triggered\n";}
		if (condition(machine)) {DBG_AUTOSTATES <<"		condition " << (condition.predicate) << " passes\n";}
	}
	if (triggered) return true;
	return false;
}

void ConditionHandler::reset() {
	if (trigger) {
		//NB_MSG << "clearing trigger on subcondition of state " << s.state_name << "\n";
		if (trigger->enabled())
			trigger->disable();
		trigger = trigger->release();
	}
	trigger = 0;
	triggered = false;
}
