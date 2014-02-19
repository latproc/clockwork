# This is a switch that turns on when a certain 
# number of counts have been seen on the input

CounterSwitch MACHINE Count {
	OPTION SetPoint 0;
	OPTION Mark 0;
	PLUGIN "counter_switch.so";
	on STATE;				# seen counter pass the marked position
	waiting STATE;	# active, watching counter
	off INITIAL;		# not active, counter not seen

  COMMAND mark { 
	Mark := SetPoint + Count.VALUE; 
	LOG "mark set: " + Mark;
	SET SELF TO waiting; 
  }
  COMMAND reset { Mark := 0 ; SET SELF TO off; }
  ENTER on { LOG "on" }
}

Counter MACHINE {
	OPTION VALUE 0;
	up WHEN TIMER>1000; ENTER up { INC VALUE; LOG VALUE; }
	waiting DEFAULT;
}

Driver MACHINE switch {
	COMMAND run { SEND mark TO switch }
}
counter Counter;
test_switch CounterSwitch(SetPoint:15) counter;
test Driver test_switch;


%BEGIN_PLUGIN
#include "MessageLog.h"

struct MyData : public PluginData {
	Value &set_point;
	Value &mark;
	Value &count;
	MyData(Value &sp, Value &mk, Value &ct) : set_point(sp), mark(mk), count(ct) { }
};

PLUGIN_EXPORT
int CheckStates(PluginScope *scope)
{
	MyData *data = dynamic_cast<MyData*>(scope->getInstanceData());
	if (!data) { 
		Value &set_point = scope->getValue("SetPoint");
		Value &mark = scope->getValue("Mark");
		Value &count = scope->getValue("Count.VALUE");

		if (set_point.kind == Value::t_integer 
				&& mark.kind == Value::t_integer
				&& count.kind == Value::t_integer) {
			data = new MyData(set_point, mark, count); 
			scope->setInstanceData(data);
			std::cout << "set data\n";
		}
		else {
			if (set_point.kind != Value::t_integer) 
				MessageLog::instance()->add("CounterSwitch SetPoint property is not an integer");
			if (mark.kind != Value::t_integer) 
				MessageLog::instance()->add("CounterSwitch Mark property is not an integer");
			if (count.kind != Value::t_integer) 
				MessageLog::instance()->add("CounterSwitch Count property is not an integer");
			std::cout << "not all properties found\n";
			return PLUGIN_ERROR;
		}
	}
	
	std::string current(scope->getState());
	if (current == "waiting" && data->count >= data->mark ) {
		std::cout << "setting state, count: " << data->count << " mark: " << data->mark << "\n";
		scope->changeState("on");
	}
	return PLUGIN_COMPLETED;
}

PLUGIN_EXPORT
int PollActions(PluginScope* scope) {
	return PLUGIN_COMPLETED;
}

%END_PLUGIN

/*
COUNTGREATER MACHINE Count {
  OPTION SetPoint 0;
  OPTION Mark 0;

  eval STATE;
  on WHEN SELF IS on || SELF IS waiting && Count.VALUE >= Mark;
  waiting WHEN Mark > 0;
  off DEFAULT;

  COMMAND mark { Mark := SetPoint + Count.VALUE; }
  COMMAND reset { Mark := 0 ; SET SELF TO eval; }
}
*/
