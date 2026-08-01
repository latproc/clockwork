# This is a switch that turns on when a certain
# number of counts have been seen on the input.
# Count.VALUE may be negative when COUNTER/ANALOG has signed:1.
# Signed32 remains for legacy channels still published as unsigned wire bits.

UNDERRANGE MACHINE Count {
	OPTION SetPoint 0;
	OPTION Signed32 0;
	PLUGIN "underrange.so.1.0";
	on STATE;				# counter is less than or equal to the set point
	off INITIAL;		# counter is greater than the set point
}

%BEGIN_PLUGIN
#include <Plugin.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>

static int64_t as_signed32_bits(int64_t value)
{
  return (int64_t)(int32_t)(uint32_t)value;
}

struct MyData {
	const int64_t *set_point;
	const int64_t *count;
	const int64_t *debug;
	const int64_t *signed32;
	char *machine_name;

	int64_t dummy_set_point;
	int64_t dummy_count;
	int64_t dummy_debug;
	int64_t dummy_signed32;
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
			snprintf(buf, buf_size, "underrange %s SetPoint property is not an integer", data->machine_name);
			log_message(scope, buf);
			data->set_point = & data->dummy_set_point;
		}
		if (!getIntValue(scope, "Count.VALUE", &data->count)) {
			snprintf(buf, buf_size, "underrange %s Count property is not an integer", data->machine_name);
			log_message(scope, buf);
			data->count = &data->dummy_count;
		}
		if (!getIntValue(scope, "DEBUG", &data->debug)) data->debug = 0;
		if (!getIntValue(scope, "Signed32", &data->signed32)) {
			data->dummy_signed32 = 0;
			data->signed32 = &data->dummy_signed32;
		}
    snprintf(buf, buf_size, "underrange %s plugin initialised", data->machine_name);
    log_message(scope, buf);
	}
	else {
		char *current = getState(scope);
		int64_t count = *data->count;
		int64_t set_point = *data->set_point;
    if (!current) return PLUGIN_ERROR;
		/* Legacy: reinterpret low 32 bits as signed when iod still publishes unsigned. */
		if (data->signed32 && *data->signed32) {
			count = as_signed32_bits(count);
			set_point = as_signed32_bits(set_point);
		}
		if ( count <= set_point )  {
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
			printf("%s poll. count: %" PRId64 "\n", data->machine_name, *data->count);
	return PLUGIN_COMPLETED;
}
%END_PLUGIN
