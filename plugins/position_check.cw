# This is a switch that turns on when a certain 
# number of counts have been seen on the input

POSITIONCHECK MACHINE Count {
	OPTION Mark 0;
	PLUGIN "position_check.so.1.0";
	on STATE;		# seen counter pass the marked position
	waiting STATE;	# active, watching counter
	off INITIAL;	# not active, counter not seen

  COMMAND mark { 
		Mark := Count.VALUE; 
		LOG "mark set: " + Mark;
		SET SELF TO waiting; 
  }
  COMMAND reset { SET SELF TO off; }
  ENTER on { LOG "on: " + Count.VALUE }
}

%BEGIN_PLUGIN
#include <Plugin.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

struct MyData {
	const long *mark;
	const long *count;
	const long *debug;
	char *machine_name;

	long dummy_mark;
	long dummy_count;
	long dummy_debug;

	long last_position;
};

PLUGIN_EXPORT
int check_states(void *scope)
{
	struct MyData *data = (struct MyData*)getInstanceData(scope);
	if (!data) {
		data = (struct MyData*)malloc(sizeof(struct MyData));
		setInstanceData(scope, data);
		printf("position_check plugin initialised\n");
		if (!getIntValue(scope, "Mark", &data->mark)) {
			log_message(scope, "CounterSwitch Mark property is not an integer");
			data->mark = &data->dummy_mark;
		}
		else if (!getIntValue(scope, "Count.VALUE", &data->count)) {
			log_message(scope, "CounterSwitch Count property is not an integer");
			data->count = &data->dummy_count;
		}
		{ 
			data->machine_name = getStringValue(scope, "NAME");
			if (!data->machine_name) data->machine_name = strdup("UNKNOWN COUNTER");
		}
		if (!getIntValue(scope, "DEBUG", &data->debug)) data->debug = 0;
		data->dummy_mark = 0;
		data->dummy_count = 0;
		data->dummy_debug = 0;
		data->last_position = *data->count;
	}
	
	else {

		char *current = getState(scope);
		if (current && strcmp(current, "waiting") == 0) {
			if (data->debug && *data->debug) printf("%s count: %ld mark: %ld\n", data->machine_name, *data->count, *data->mark);
			if ( (*data->count >= *data->mark  && data->last_position <= *data->mark)
				 || (*data->count <= *data->mark  && data->last_position >= *data->mark)
				)  {
				changeState(scope, "on");
				if (data->debug && *data->debug) printf("%s turned on\n", data->machine_name);
			}
		}
		free(current);
		return PLUGIN_COMPLETED;
	}
	
	return PLUGIN_ERROR;
}

PLUGIN_EXPORT
int poll_actions(void *scope) {
	struct MyData *data = (struct MyData*)getInstanceData(scope);
	if (data && data->debug && *data->debug)	
			printf("%s poll. count: %ld mark: %ld\n", data->machine_name, *data->count, *data->mark);
	return PLUGIN_COMPLETED;
}
%END_PLUGIN
