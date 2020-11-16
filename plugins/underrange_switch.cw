# This is a switch that turns on when a certain 
# number of counts have been seen on the input

UNDERRANGE MACHINE Count {
	OPTION SetPoint 0;
	PLUGIN "underrange.so.1.0";
	on STATE;				# counter is less than or equal to the set point
	off INITIAL;		# counter is greater than the set point
}

%BEGIN_PLUGIN
#include <Plugin.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

struct MyData {
	const long *set_point;
	const long *count;
	const long *debug;
	char *machine_name;

	long dummy_set_point;
	long dummy_count;
	long dummy_debug;
};

PLUGIN_EXPORT
int check_states(void *scope)
{
	struct MyData *data = (struct MyData*)getInstanceData(scope);
  enum { buf_size = 100 };
  char buf[buf_size];

	if (!data) {
		data = (struct MyData*)malloc(sizeof(struct MyData));
		setInstanceData(scope, data);
		{ 
			data->machine_name = getStringValue(scope, "NAME");
			if (!data->machine_name) data->machine_name = strdup("UNKNOWN COUNTER");
		}
		if (!getIntValue(scope, "SetPoint", &data->set_point)) {
			snprintf(buf, buf_size, "count_lessthan %s SetPoint property is not an integer", data->machine_name);
			log_message(scope, buf);
			data->set_point = & data->dummy_set_point;
		}
		if (!getIntValue(scope, "Count.VALUE", &data->count)) {
			snprintf(buf, buf_size, "count_lessthan %s Count property is not an integer", data->machine_name);
			log_message(scope, buf);
			data->count = &data->dummy_count;
		}
		if (!getIntValue(scope, "DEBUG", &data->debug)) data->debug = 0;
    snprintf(buf, buf_size, "count_lessthan %s plugin initialised", data->machine_name);
    log_message(scope, buf);
	}
	else {
		char *current = getState(scope);
    if (!current) return PLUGIN_ERROR;
		if ( *data->count <= *data->set_point )  {
		  if (strcmp(current, "on") != 0) {
				changeState(scope, "on");
				if (data->debug && *data->debug) {
          snprintf(buf, buf_size, "%s turned on\n", data->machine_name);
          log_message(scope, buf);
        }
			}
		}
    else {
		  if (strcmp(current, "on") == 0) {
				changeState(scope, "off");
				if (data->debug && *data->debug) {
          snprintf(buf, buf_size, "%s turned off\n", data->machine_name);
          log_message(scope, buf);
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
	struct MyData *data = (struct MyData*)getInstanceData(scope);
	if (data && data->debug && *data->debug)	
			printf("%s poll. count: %ld\n", data->machine_name, *data->count);
	return PLUGIN_COMPLETED;
}
%END_PLUGIN
