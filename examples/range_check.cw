# This is a switch that turns on when the counter
# is within a given range and turns off when it
# leaves that range.
# 
# The plugin is inactive when in the idle state
#
# By default the POSITIONCHECK machine starts inactive
# and is started by the start message.
#
# The POSITIONCHECK machine can be started in the 
# active state by providing both position values and
# setting active to 1;

RANGECHECK MACHINE Count {
	OPTION Pos1 0;
	OPTION Pos2 0;
	OPTION active 0;

	PLUGIN "range_check.so.1.0";
	on STATE;	# at last check the counter was within the range
	off STATE;	# at last check the counter was outside the range
	idle STATE;	# inactive; not watching counter

	COMMAND start { active := 1; }
	COMMAND stop { active := 0; }
	COMMAND reset { active := 0; }
	ENTER idle { active := 0; }
 
	ENTER on { LOG "on: " + Count.Position }
}

%BEGIN_PLUGIN
#include <Plugin.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

struct MyData {
	const long *p1, *p2;
	const long *count;
	const long *active;
	const long *debug;
	char *machine_name;

	long dummy_pos;
	long dummy_count;
	long dummy_debug;
	
	long last_active;
	long last_position;
};

PLUGIN_EXPORT
int check_states(void *scope)
{
	struct MyData *data = (struct MyData*)getInstanceData(scope);
	if (!data) {
		data = (struct MyData*)malloc(sizeof(struct MyData));
		if (!getIntValue(scope, "active", &data->active)) {
			log_message(scope, "RangeCheck active property is not an integer");
			free(data);
			return PLUGIN_COMPLETED;
		}
		setInstanceData(scope, data);

		if (!getIntValue(scope, "Pos1", &data->p1)) {
			log_message(scope, "RangeCheck Pos1 property is not an integer");
			data->p1 = &data->dummy_pos;
		}
		if (!getIntValue(scope, "Pos2", &data->p2)) {
			log_message(scope, "RangeCheck Pos2 property is not an integer");
			data->p2 = &data->dummy_pos;
		}
		if (!getIntValue(scope, "Count.Position", &data->count)) {
			log_message(scope, "RangeCheck Count does not have an integer Position property");
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
		data->last_active = 0;
		data->last_position = *data->count;
		char *current = getState(scope);

		/* automatically switch to idle if active is 0 */
		if (current && !*data->active && strcmp(current, "idle") != 0) 
			changeState(scope, "idle");
		
		printf("range check %s initialised\n", data->machine_name);
	}
	
	{

		char *current = getState(scope);
		long count = *data->count;
		/* only check position if not idle */
		if (*data->active) {
			if (data->debug && *data->debug) printf("%s count: %ld Pos1: %ld Pos2: %ld\n", 
				data->machine_name, count, *data->p1, *data->p2);
			if (
				   (*data->p1 <= *data->p2 && count >= *data->p1  && count <= *data->p2) 
				|| (*data->p1 >  *data->p2 && count >= *data->p2  && count <= *data->p1) 
				|| (*data->p1 <= *data->p2 && (
						(*data->p1 > data->last_position && *data->p2 < count)
						|| (*data->p1 > count && *data->p2 < data->last_position)
					))
				|| (*data->p1 > *data->p2 && (
						(*data->p2 > data->last_position && *data->p1 < count)
						|| (*data->p2 > count && *data->p1 < data->last_position)
					))
			) {
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
		else {
			if (data->last_active) {
				/* automatically switch out of idle if active is not 0 */
				changeState(scope, "idle");
			}
		}
		data->last_active = *data->active;
		data->last_position = count;
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
