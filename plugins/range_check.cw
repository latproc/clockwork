# This is a switch that turns on when the counter
# is within a given range and turns off when it
# leaves that range.
# 
# The plugin is inactive when in the waiting state

RANGECHECK MACHINE Count {
	OPTION Pos1 0;
	OPTION Pos2 0;
	PLUGIN "range_check.so.1.0";
	on STATE;	# at last check the counter was within the range
	off STATE;	# at last check the counter was outside the range
	idle STATE;	# inactive; not watching counter

  COMMAND start { SET SELF TO waiting; }
  COMMAND reset { SET SELF TO off; }
  ENTER on { LOG "on: " + Count.VALUE }
}

%BEGIN_PLUGIN
#include <Plugin.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

struct MyData {
	const long *p1, *p2;
	const long *count;
	const long *debug;
	char *machine_name;

	long dummy_pos;
	long dummy_count;
	long dummy_debug;
};

PLUGIN_EXPORT
int check_states(void *scope)
{
	struct MyData *data = (struct MyData*)getInstanceData(scope);
	if (!data) {
		data = (struct MyData*)malloc(sizeof(struct MyData));
		setInstanceData(scope, data);
		if (!getIntValue(scope, "Pos1", &data->p1)) {
			log_message(scope, "RangeCheck Pos1 property is not an integer");
			data->p1 = &data->dummy_pos;
		}
		if (!getIntValue(scope, "Pos2", &data->p2)) {
			log_message(scope, "RangeCheck Pos2 property is not an integer");
			data->p2 = &data->dummy_pos;
		}
		if (!getIntValue(scope, "Count.VALUE", &data->count)) {
			log_message(scope, "RangeCheck Count does not have an integer VALUE property");
			data->count = &data->dummy_count;
		}
		{ 
			data->machine_name = getStringValue(scope, "NAME");
			if (!data->machine_name) data->machine_name = strdup("UNKNOWN RANGECHECK");
		}
		if (!getIntValue(scope, "DEBUG", &data->debug)) data->debug = 0;
		data->dummy_pos = 0;
		data->dummy_count = 0;
		data->dummy_debug = 0;
		printf("range check %s initialised\n", data->machine_name);
	}
	
	else {

		char *current = getState(scope);
		long count = *data->count;
		/* only check position if not idle */
		if (current && strcmp(current, "idle") != 0) {
			if (data->debug && *data->debug) printf("%s count: %ld Pos1: %ld Pos2: %ld\n", 
				data->machine_name, count, *data->p1, *data->p2);
			if (   (*data->p1<= *data->p2 && count >= *data->p1  && count <= *data->p2) 
				|| (*data->p1 > *data->p2 && count >= *data->p2  && count <= *data->p1) ) {
				if (strcmp("on", current) != 0) {
					changeState(scope, "on");
					if (data->debug && *data->debug) printf("%s turned on\n", data->machine_name);
				}
			}
			else {
				if (strcmp("off", current) != 0) {
					changeState(scope, "off");
					if (data->debug && *data->debug) printf("%s turned off\n", data->machine_name);
				}
			}
		}
		free(current);
		return PLUGIN_COMPLETED;
	}
	
	return PLUGIN_ERROR;
}

PLUGIN_EXPORT
int poll_actions(void *scope) {
	//struct MyData *data = (struct MyData*)getInstanceData(scope);
	return PLUGIN_COMPLETED;
}
%END_PLUGIN
