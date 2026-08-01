# This is a switch that turns on when a certain
# number of counts have been seen on the input.
# Count.VALUE may be negative when COUNTER has signed:1.

COUNTGREATER MACHINE Count {
	OPTION SetPoint 100;
	OPTION Mark 0;
	PLUGIN "count_greater.so.1.0";
	on STATE;				# seen counter pass the marked position
	waiting STATE;	# active, watching counter
	off INITIAL;		# not active, counter not seen

  COMMAND mark {
		Mark := SetPoint + Count.VALUE;
#		LOG "mark set: " + Mark;
		SET SELF TO waiting;
  }
  COMMAND reset { Mark := 0 ; SET SELF TO off; }
#  ENTER on { LOG "on: " + Count.VALUE }
}

%BEGIN_PLUGIN
#include <Plugin.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

struct MyData {
	const int64_t *set_point;
	const int64_t *mark;
	const int64_t *count;
	const int64_t *debug;
	char *machine_name;

	int64_t dummy_set_point;
	int64_t dummy_mark;
	int64_t dummy_count;
	int64_t dummy_debug;
};

PLUGIN_EXPORT
int check_states(void *scope)
{
	struct MyData *data = (struct MyData*)getInstanceData(scope);
	if (!data) {
		data = (struct MyData*)malloc(sizeof(struct MyData));
		setInstanceData(scope, data);
		printf("count_greater plugin initialised\n");
		if (!getIntValue(scope, "SetPoint", &data->set_point)) {
			log_message(scope, "CounterSwitch SetPoint property is not an integer");
			data->set_point = & data->dummy_set_point;
		}
		else if (!getIntValue(scope, "Mark", &data->mark)) {
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
	}

	else {

		char *current = getState(scope);
		if (current && strcmp(current, "waiting") == 0) {
			if (data->debug && *data->debug)
				printf("%s count: %" PRId64 " mark: %" PRId64 "\n",
				       data->machine_name, *data->count, *data->mark);
			if ( *data->count >= *data->mark )  {
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
			printf("%s poll. count: %" PRId64 " mark: %" PRId64 "\n",
			       data->machine_name, *data->count, *data->mark);
	return PLUGIN_COMPLETED;
}
%END_PLUGIN
