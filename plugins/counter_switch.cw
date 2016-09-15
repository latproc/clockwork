# This is a switch that turns on when a certain 
# number of counts have been seen on the input

CounterSwitch MACHINE Count {
	OPTION POLLING_DELAY 4000;
	OPTION SetPoint 0;
	OPTION Mark 0;
	PLUGIN "counter_switch.so.1.0";
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
test_switch CounterSwitch(SetPoint:3) counter;
test Driver test_switch;


%BEGIN_PLUGIN
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <Plugin.h>

struct MyData {
	long *set_point;
	long *mark;
	long *count;
};

PLUGIN_EXPORT
int check_states(void *scope)
{
	struct MyData *data = (struct MyData*)getInstanceData(scope);
	char *current;
	if (!data) {
		data = (struct MyData*)malloc(sizeof(struct MyData));
		setInstanceData(scope, data);
		if (!getIntValue(scope, "SetPoint", &data->set_point)) {
			log_message(scope, "CounterSwitch SetPoint property is not an integer");
			goto plugin_init_error;
		}
		else if (!getIntValue(scope, "Mark", &data->mark)) {
			log_message(scope, "CounterSwitch Mark property is not an integer");
			goto plugin_init_error;
		}
		else if (!getIntValue(scope, "Count.VALUE", &data->count)) {
			log_message(scope, "CounterSwitch Count property is not an integer");
			goto plugin_init_error;
		}
		goto continue_plugin;
plugin_init_error:
		setInstanceData(scope, 0);
		free(data);
		return PLUGIN_ERROR;
	}
continue_plugin:

	current = getState(scope);
	/*printf("test: %s %ld\n", (current)? current : "null", *data->count);*/
	if (current && strcmp(current, "waiting") == 0 && *data->count >= *data->mark ) {
		printf("changing to state on\n");
		int x = changeState(scope, "on");
	}
	free(current);
	return PLUGIN_COMPLETED;
}

PLUGIN_EXPORT
int poll_actions(void *scope) {
	return PLUGIN_COMPLETED;
}

