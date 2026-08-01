# This is a switch that turns on when a certain
# number of counts have been seen on the input.
# Count.VALUE may be negative when COUNTER has signed:1.

COUNTLESSTHAN MACHINE Count {
	OPTION SetPoint 100;
	OPTION Mark 0;
	PLUGIN "count_lessthan.so.1.0";
	on STATE;				# seen counter pass the marked position
	waiting STATE;	# active, watching counter
	off INITIAL;		# not active, counter not seen

  COMMAND mark {
		Mark := Count.VALUE - SetPoint;
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
		if (!getIntValue(scope, "Mark", &data->mark)) {
			snprintf(buf, buf_size, "count_lessthan %s Mark property is not an integer", data->machine_name);
			log_message(scope, buf);
			data->mark = &data->dummy_mark;
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
		if (current && strcmp(current, "waiting") == 0) {
			if (data->debug && *data->debug) {
        snprintf(buf, buf_size, "%s count: %" PRId64 " mark: %" PRId64 "\n",
                 data->machine_name, *data->count, *data->mark);
        log_message(scope, buf);
      }
			if ( *data->count <= *data->mark )  {
				changeState(scope, "on");
				if (data->debug && *data->debug) {
          snprintf(buf, buf_size, "%s turned on\n", data->machine_name);
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
			printf("%s poll. count: %" PRId64 " mark: %" PRId64 "\n",
			       data->machine_name, *data->count, *data->mark);
	return PLUGIN_COMPLETED;
}
%END_PLUGIN
