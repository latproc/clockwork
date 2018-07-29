RATEESTIMATORGLOBALS MACHINE {
	OPTION PERSISTENT true;
}

ANALOGINPUTGLOBALS MACHINE {
	OPTION PERSISTENT true;
}

# A_Output Must be set to ZeroPos before turning on O_Enable, which must be supplied as a config property
ANALOGDUALGUARD MACHINE Guard, O_Enable, A_Output {
	OPTION PERSISTENT true;
	OPTION VALUE 0;

	disabled WHEN A_Output DISABLED;
	interlocked_check WHEN SELF IS interlocked && A_Output.VALUE != ZeroPos;
	interlocked WHEN Guard != true;
	updating WHEN SELF IS on && VALUE != A_Output.VALUE;
	on WHEN O_Enable IS on && VALUE != ZeroPos;
	off DEFAULT;

	ENTER updating { 
		A_Output.VALUE := VALUE;
	}

	ENTER disabled {
		A_Output.VALUE := ZeroPos + 1;
	}
	ENTER interlocked_check {
		VALUE := ZeroPos;
		A_Output.VALUE := VALUE;
	}

	ENTER interlocked {
		VALUE := ZeroPos;
		A_Output.VALUE := VALUE;
	}

	ENTER off {
		VALUE := ZeroPos;
		A_Output.VALUE := ZeroPos;
	}

	ENTER INIT {
		VALUE := ZeroPos;
		A_Output.VALUE := VALUE;
		WAIT 2;
	}
}

# this output driver takes an analogue output or guarded analogue
# output such as an ANALOGDUALGUARD and drives the output in the
# range 0..32767. 
#
# The zero position is the midpoint of the range: 16383
# An error is given if the input value 'NewValue' is out of range.

GLOBALDRIVER MACHINE {
	OPTION PERSISTANT true;
	EXPORT RW 32BIT MaxForward, MaxReverse, ZeroPos, MinForward, MinReverse;
	OPTION MaxForward 32767;
	OPTION MaxReverse 0;
	OPTION ZeroPos 16383;
	OPTION MinForward 19800;
	OPTION MinReverse 11800;
}

RAMPSPEEDMACHINECONFIGURATION MACHINE {
	OPTION PERSISTENT true;
	EXPORT RW 32BIT min_update_time, StartTimeout, fwd_step_up, fwd_step_down, rev_step_up, rev_step_down;
	OPTION min_update_time 20; # minimum time between normal control updates
	OPTION StartTimeout 500;		#conveyor start timeout
	OPTION fwd_step_up 40000; # maximum change per second
	OPTION fwd_step_down 60000; # maximum change per second
	OPTION rev_step_up 40000; # maximum change per second
	OPTION rev_step_down 60000; # maximum change per second
	OPTION inverted false; # do not invert power
}

RAMPSPEEDCONFIGURATION MACHINE {
	OPTION PERSISTENT true;
	EXPORT RW 32BIT SlowSpeed, FullSpeed, PowerFactor, StoppingDistance, TravelAllowance, tolerance;
	OPTION SlowSpeed 40;
	OPTION FullSpeed 1000;
	OPTION PowerFactor 750; 				 # converts from a speed value to a power value
	OPTION StoppingDistance 3000;  # distance from the stopping point to begin slowing down
	OPTION TravelAllowance 0; # amount to travel after the mark before slowing down
	OPTION tolerance 100;        #stopping position tolerance
}

SPEEDCONTROL MACHINE {
	ramp STATE;
	seek STATE;
	stop INITIAL;
	none STATE;
}

RAMPSPEEDCONFIGCHECK MACHINE config {
	error WHEN config.SlowSpeed > 0 && config.FullSpeed < 0;
	error WHEN config.FullSpeed > 0 && config.SlowSpeed < 0;
	error WHEN config.FullSpeed < 0 && config.SlowSpeed <= config.FullSpeed;
	error WHEN config.FullSpeed > 0 && config.FullSpeed <= config.SlowSpeed;
	error WHEN config.PowerFactor == 0;
	error WHEN config.PowerFactor > 4000;
	ok DEFAULT;
}

RAMPSPEEDCONTROLLER MACHINE M_Control, settings, output_settings, fwd_settings, rev_settings, driver, pos, freq_sampler {
    PLUGIN "ramp_controller.so.1.0";

%BEGIN_PLUGIN
#include <Plugin.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/time.h>
#include <stdint.h>
#include <math.h>

struct RCData {
	enum { 
		rs_init, 
		rs_interlocked, 
		rs_off, 
		rs_forward, 
		rs_reverse, 
		rs_idle 
	} ramp_state;
    enum { 
		cs_init, 
		cs_interlocked, /* something upstream is interlocked */
		cs_stopped, 		/* sleeping */
		cs_ramping, 	/* ramping up or down to a power level */
		cs_stopping, 	/* driving to within tolerance of a count */
		cs_monitoring,  /* driving with no stop condition set */
		cs_startup_seeking, 	/* detected a change to stop position */
		cs_seeking, 	/* driving with a set position set */
		cs_seeking_slow /* ramped down to slow speed to meet the target */
	} state;

	/**** clockwork interface ***/
    long *set_point;
    long *stop_position; 
    long *mark_position; 
	long *speed;
	long *position;
	long *target_power;

	/* ramp settings */
	long *min_update_time;
	long *fwd_step_up;		/* ramp increase per second */
	long *fwd_step_down;
	long *rev_step_up;		/* downward ramp change per second */
	long *rev_step_down;

	long *output_value;
	long *max_forward;
	long *max_reverse;
	long *zero_pos;
	long *min_forward;
	long *min_reverse;

	long *fwd_tolerance;
	long *fwd_stopping_distance;
	long *fwd_slow_speed;
	long *fwd_full_speed;
	long *fwd_power_factor;
	long *fwd_travel_allowance;

	long *rev_tolerance;
	long *rev_stopping_distance;
	long *rev_slow_speed;
	long *rev_full_speed;
	long *rev_power_factor;
	long *rev_travel_allowance;

	/* internal values for ramping */
	long current_power;
	uint64_t ramp_start_time;
	long ramp_start_power;		/* the power level at the point ramping started */
	long ramp_start_target; 	/* the target that was set when ramping started */
	uint64_t last_poll;
	long tolerance;
	long last_set_point;
	long last_stop_position;
	long default_debug; /* a default debug level if one is not specified in the script */
	int inverted;

	char *conveyor_name;

	/* stop/start control */
    long stop_marker;
	long *debug;
	
};

int getInt(void *scope, const char *name, long **addr) {
	struct RCData *data = (struct RCData*)getInstanceData(scope);
	if (!getIntValue(scope, name, addr)) {
		char buf[100];
		char *val = getStringValue(scope, name);
		if (data)
			snprintf(buf, 100, "%s RampSpeedController: %s (%s) is not an integer", data->conveyor_name, name, (val) ? val : "null");
		else
			snprintf(buf, 100, "RampSpeedController: %s (%s) is not an integer", name, (val) ? val : "null");
		printf("%s\n", buf);
		log_message(scope, buf);
		free(val);
		return 0;
	}
	/* printf("%s: %d\n", name, **addr); */
	return 1;
}

long output_scaled(struct RCData * settings, long power) {

	long raw = *settings->zero_pos;
	if (power > 0)
		raw = power + *settings->min_forward;
	else if (power < 0)
		raw = power + *settings->min_reverse;
	
	if (raw < *settings->max_reverse) raw = *settings->max_reverse;
  if (raw > *settings->max_forward) raw = *settings->max_forward;

  if ( settings->inverted ) {
		raw = 32766 - (uint16_t)raw;
		if (raw < 0) raw = 0;
	}
	return raw;
}
    
PLUGIN_EXPORT
int check_states(void *scope)
{
	int ok = 1;
	struct RCData *data = (struct RCData*)getInstanceData(scope);
	if (!data) {
		// one-time initialisation
		data = (struct RCData*)malloc(sizeof(struct RCData));
		memset(data, 0, sizeof(struct RCData));
		setInstanceData(scope, data);
		{ 
			data->conveyor_name = getStringValue(scope, "NAME");
			if (!data->conveyor_name) data->conveyor_name = strdup("UNKNOWN CONVEYOR");
		}

		ok = ok && getInt(scope, "SetPoint", &data->set_point);
		ok = ok && getInt(scope, "StopMarker", &data->mark_position);
		ok = ok && getInt(scope, "StopPosition", &data->stop_position);
		ok = ok && getInt(scope, "TargetPower", &data->target_power);
		ok = ok && getInt(scope, "freq_sampler.VALUE", &data->speed);
		ok = ok && getInt(scope, "pos.VALUE", &data->position);
		ok = ok && getInt(scope, "settings.min_update_time", &data->min_update_time);
		ok = ok && getInt(scope, "settings.fwd_step_up", &data->fwd_step_up);
		ok = ok && getInt(scope, "settings.fwd_step_down", &data->fwd_step_down);
		ok = ok && getInt(scope, "settings.rev_step_up", &data->rev_step_up);
		ok = ok && getInt(scope, "settings.rev_step_down", &data->rev_step_down);

		ok = ok && getInt(scope, "driver.VALUE", &data->output_value);
		ok = ok && getInt(scope, "output_settings.MaxForward", &data->max_forward);
		ok = ok && getInt(scope, "output_settings.MaxReverse", &data->max_reverse);
		ok = ok && getInt(scope, "output_settings.ZeroPos", &data->zero_pos);
		ok = ok && getInt(scope, "output_settings.MinForward", &data->min_forward);
		ok = ok && getInt(scope, "output_settings.MinReverse", &data->min_reverse);

		ok = ok && getInt(scope, "fwd_settings.SlowSpeed", &data->fwd_slow_speed);
		ok = ok && getInt(scope, "fwd_settings.FullSpeed", &data->fwd_full_speed);
		ok = ok && getInt(scope, "fwd_settings.PowerFactor", &data->fwd_power_factor);
		ok = ok && getInt(scope, "fwd_settings.StoppingDistance", &data->fwd_stopping_distance);
		ok = ok && getInt(scope, "fwd_settings.TravelAllowance", &data->fwd_travel_allowance);
		ok = ok && getInt(scope, "fwd_settings.tolerance", &data->fwd_tolerance);

		ok = ok && getInt(scope, "rev_settings.SlowSpeed", &data->rev_slow_speed);
		ok = ok && getInt(scope, "rev_settings.FullSpeed", &data->rev_full_speed);
		ok = ok && getInt(scope, "rev_settings.PowerFactor", &data->rev_power_factor);
		ok = ok && getInt(scope, "rev_settings.StoppingDistance", &data->rev_stopping_distance);
		ok = ok && getInt(scope, "rev_settings.TravelAllowance", &data->rev_travel_allowance);
		ok = ok && getInt(scope, "rev_settings.tolerance", &data->rev_tolerance);

		if (!getInt(scope, "DEBUG", &data->debug) ) data->debug = &data->default_debug;
		
	  {
			char *invert = getStringValue(scope, "settings.inverted");
			if (invert && strcmp(invert,"true") == 0) {
				data->inverted = 1; 
				if (data->debug) printf("inverted output");
			}
			else data->inverted = 0;
			free(invert);
		}

		if (!ok) {
			printf("%s plugin failed to initialise\n", data->conveyor_name);
			setInstanceData(scope, 0);
			free(data);
			return PLUGIN_ERROR;
		}

		data->state = cs_init;
		data->current_power = 0;
		data->last_poll = 0;

		data->stop_marker = 0;
		data->last_stop_position = 0;
		data->last_set_point = 0;

		printf("%s plugin initialised ok: update time %ld\n", data->conveyor_name, *data->min_update_time);
	}
	return PLUGIN_COMPLETED;
}

PLUGIN_EXPORT
int poll_actions(void *scope) {
	struct RCData *data = (struct RCData*)getInstanceData(scope);
	struct timeval now;
	uint64_t now_t = 0;
	float new_power = 0.0f;
	long calculated_target_power = 0;
	long calculated_set_point = 0;
	long calculated_stop_position = 0;
	char *current = 0;
	char *control = 0;

	if (!data) return PLUGIN_COMPLETED; /* not initialised yet; nothing to do */

	gettimeofday(&now, 0);
	now_t = now.tv_sec * 1000000 + now.tv_usec;

	calculated_set_point = data->last_set_point;
	calculated_target_power = *data->target_power;

	if ( (now_t - data->last_poll)/1000 < *data->min_update_time) return PLUGIN_COMPLETED;

	{
		enum {cmd_none, cmd_stop, cmd_ramp, cmd_seek} command;
		void *controller = 0;
		current = getState(scope);
		control = 0;
		/*printf("%s test: %s %ld, stop: %ld, pow: %ld, pos: %ld\n", data->conveyor_name,
							 (current)? current : "null", 
                *data->set_point,
                *data->stop_position,
                *data->target_power,
                *data->position);
		*/
		controller = getNamedScope(scope, "control");
		if (controller) {
			control = getState(controller);
			//printf("%s controller state: %s\n", data->conveyor_name, (control) ? control : "null");
			if (strcmp(control, "stop") == 0)
				command = cmd_stop;
			else if (strcmp(control, "ramp") == 0)
				command = cmd_ramp;
			else if (strcmp(control, "seek") == 0)
				command = cmd_seek;
			else 
				command = cmd_none;
		}
		else
			command = cmd_stop;
		void *std_state = getNamedScope(scope, "M_Control");
		if (!std_state) log_message(scope, "Could not find named scope M_Control");

		int interlocked = current && strcmp(current, "interlocked") == 0;

		if (interlocked && data->state != cs_interlocked) {
			if (std_state) changeState(std_state, "Unavailable");
			data->state = cs_interlocked;
			command = cmd_stop;
		}

		if (command == cmd_stop) {
			if (data->debug && *data->debug) printf("%s stop command\n", data->conveyor_name);
			data->current_power = 0;
			data->stop_marker = 0;
			data->last_set_point = 0;
			setIntValue(scope, "SetPoint", 0);
			if (!interlocked) {
				data->state = cs_stopped;
				if (std_state) changeState(std_state, "Ready");
			}
			setIntValue(scope, "TargetPower", 0);
			setIntValue(scope, "driver.VALUE", output_scaled(data, 0) );
			if (controller) changeState(controller, "none");
			goto done_polling_actions;
		}
		if (interlocked) goto done_polling_actions;

		if (data->current_power == 0 && (data->state == cs_init || data->state == cs_interlocked) ) {
			if (std_state) changeState(std_state, "Ready");
			data->state = cs_stopped;
		}
		/* if the stop position has changed, we attempt to move to that position */
		if (*data->stop_position && data->last_stop_position != *data->stop_position) {
			printf("%s new stop position %ld\n", data->conveyor_name, *data->stop_position);
			data->last_stop_position = *data->stop_position;
			data->state = cs_startup_seeking;
			data->stop_marker = *data->mark_position;
			if (!data->stop_marker) *data->set_point = 0;
			if (std_state) changeState(std_state, "Resetting");
		}
		/* otherwise, if the set point was changed, use it */
		else if (data->state != cs_seeking_slow && data->state != cs_seeking 
					&& data->state != cs_startup_seeking 
					&& data->last_set_point != *data->set_point) {
			printf("%s new set point %ld\n", data->conveyor_name, *data->set_point);
			setIntValue(scope, "StopPosition", 0);
			data->state = cs_ramping;
			if (*data->set_point && std_state) changeState(std_state, "Working");
		}

		if (data->state == cs_startup_seeking && *data->set_point == 0) {
			if (std_state) changeState(std_state, "Resetting");
			printf("%s starting to a position: %ld\n", data->conveyor_name, *data->stop_position);
			data->state = cs_seeking;
			if (*data->position <= *data->stop_position - *data->fwd_tolerance) {
				printf("%s need to move forward from %ld\n", data->conveyor_name, *data->position);
				data->stop_marker = *data->stop_position - *data->fwd_stopping_distance;
				if (*data->stop_position - *data->position < *data->fwd_stopping_distance)
					calculated_set_point = *data->fwd_slow_speed;
				else
					calculated_set_point = *data->fwd_full_speed;
				calculated_target_power = calculated_set_point * *data->fwd_power_factor / 100;
				data->tolerance = *data->fwd_tolerance;
			}
			else if (*data->position >= *data->stop_position + *data->rev_tolerance) {
				printf("%s need to move backward from %ld\n", data->conveyor_name, *data->position);
				data->stop_marker = *data->stop_position + *data->rev_stopping_distance;
				if (*data->position - *data->stop_position < *data->rev_stopping_distance)
					calculated_set_point = *data->rev_slow_speed;
				else
					calculated_set_point = *data->rev_full_speed;
				calculated_target_power = calculated_set_point * *data->rev_power_factor / 100;
				data->tolerance = *data->rev_tolerance;
			}
			else {
					if (data->debug && *data->debug)printf("%s already close enough to position %ld\n", data->conveyor_name, *data->stop_position);
			    calculated_set_point = 0;
			    calculated_target_power = 0;
			    printf("%s stopping error: %ld", data->conveyor_name, *data->stop_position - *data->position);
			    setIntValue(scope, "TargetPower", calculated_target_power);
			}

			setIntValue(scope, "SetPoint", calculated_set_point);
			calculated_target_power = calculated_set_point * *data->rev_power_factor / 100;
			setIntValue(scope, "TargetPower", calculated_target_power);
		}

		if (data->state == cs_ramping && data->last_set_point != *data->set_point) { 
			/* setpoint has changed */
	    printf("%s new setpoint: %ld; fixing power: ", data->conveyor_name, *data->set_point);
			data->last_set_point = *data->set_point;
			if (*data->set_point > 0) 
				calculated_target_power = *data->set_point * *data->fwd_power_factor / 100;
			else
				calculated_target_power = *data->set_point * *data->rev_power_factor / 100;
			setIntValue(scope, "TargetPower", calculated_target_power);
			if (data->debug && *data->debug) printf("%s setting ramp to power: %ld\n", data->conveyor_name, calculated_target_power);
		}
		if (data->state == cs_ramping  && data->current_power == *data->target_power) {
			if (data->current_power) {
				data->state = cs_monitoring;
				if (data->debug && *data->debug) printf("%s changing state to monitoring\n", data->conveyor_name);
			}
/*
			else {
				data->state = cs_stopped;
				setIntValue(scope, "SetPoint", 0);
				setIntValue(scope, "TargetPower", 0);
				setIntValue(scope, "driver.VALUE", output_scaled(data, 0) );
				if (std_state) changeState(std_state, "Ready");
			}
*/
		}
		
		if ( data->state != cs_stopped && data->state != cs_seeking && data->state != cs_seeking_slow 
					&& data->state != cs_interlocked && *data->stop_position != 0) {
				printf("%s detected (state: %d), stop position %ld (mark: %ld) state<-seeking (was %d)\n",
						data->conveyor_name, data->state, *data->stop_position, *data->mark_position, data->state);
				if (std_state) changeState(std_state, "Resetting");
		    data->state = cs_seeking;
				data->stop_marker = *data->mark_position;
		}

		if ( data->state == cs_seeking || data->state == cs_seeking_slow ) {
			if ( data->current_power  > 0 
					&& *data->position < data->stop_marker + *data->fwd_travel_allowance ) {
			}
			else if (data->current_power  < 0
					&& *data->position > data->stop_marker - *data->rev_travel_allowance ) {
			}
			else if ( 
					 ( data->current_power  > 0 && *data->position >= *data->stop_position - *data->fwd_tolerance  )
				|| ( data->current_power  < 0 && *data->position <= *data->stop_position + *data->rev_tolerance ) )
			{
			    printf("%s halting at %ld, aiming for %ld (error %ld) current power: %ld\n", 
						data->conveyor_name, *data->position, *data->stop_position, 
							(*data->position - *data->stop_position), data->current_power);
				data->state = cs_seeking_slow;
				data->current_power = 0;
				setIntValue(scope, "SetPoint", 0);
				setIntValue(scope, "TargetPower", 0);
				setIntValue(scope, "driver.VALUE", output_scaled(data, 0) );
			}
			else if (data->current_power  > 0 && *data->position + *data->fwd_stopping_distance >= *data->stop_position 
						&& data->state != cs_seeking_slow){
			    printf("%s ramping down at %ld aiming for %ld \n", data->conveyor_name, *data->position, *data->stop_position);
			    data->state = cs_seeking_slow;
				calculated_target_power = *data->fwd_slow_speed * *data->fwd_power_factor / 100;
				setIntValue(scope, "TargetPower", calculated_target_power);
				if (data->debug && *data->debug) printf("%s new power target %ld, current power %ld\n", 
					data->conveyor_name, calculated_target_power, data->current_power);
			}
			else if (data->current_power  < 0 && *data->position - *data->rev_stopping_distance <= *data->stop_position 
						&& data->state != cs_seeking_slow){
			    //if (data->debug && *data->debug) 
				printf("%s ramping down at %ld (tol: %ld) aiming for %ld\n", 
					data->conveyor_name, *data->position, *data->rev_stopping_distance, *data->stop_position);
			    data->state = cs_seeking_slow;
				calculated_target_power = *data->rev_slow_speed * *data->rev_power_factor / 100;
				setIntValue(scope, "TargetPower", calculated_target_power);
				if (data->debug && *data->debug) printf("%s new power target %ld, current power %ld\n", 
					data->conveyor_name, calculated_target_power, data->current_power);
			}
			else if (data->current_power == 0 && *data->target_power == 0 && fabs(*data->speed) < 100) {
				if (*data->speed > 5 
						&& (*data->position < *data->stop_position - *data->fwd_tolerance
							|| *data->position > *data->stop_position + *data->fwd_tolerance ) ) {
					/* make last_stop_position different to stop_position*/
					printf("%s bad stop at %ld, %ld\n", data->conveyor_name, *data->position, *data->speed);
					data->last_stop_position = *data->stop_position; 
					data->state = cs_startup_seeking;
				}
				if (*data->speed < -5 
						&& (*data->position > *data->stop_position + *data->fwd_tolerance
							|| *data->position < *data->stop_position - *data->fwd_tolerance ) ) {
					/* make last_stop_position different to stop_position*/
					printf("%s bad stop (rev) at %ld, %ld\n", data->conveyor_name, *data->position, *data->speed);
					data->last_stop_position = *data->stop_position; 
					data->state = cs_startup_seeking;
				}
				else if (*data->stop_position - *data->fwd_tolerance <= *data->position 
							&& *data->stop_position + *data->rev_tolerance >= *data->position) {
					calculated_stop_position = 0;
					data->state = cs_stopped;
					data->last_stop_position = *data->stop_position; 
					setIntValue(scope, "driver.VALUE", output_scaled(data, 0) );
					printf("%s stopped ok at %ld\n", data->conveyor_name, *data->position );
					setIntValue(scope, "SetPoint", 0);
					if (std_state) changeState(std_state, "Ready");
				}
				else {
					// power up again because we are not close
					data->last_stop_position = *data->position;
					printf("moving slow %ld but not close (%ld), stop pos: %ld\n", *data->speed, *data->position, *data->stop_position);
				}
			}
		}

		if (*data->target_power == data->current_power) {
				data->ramp_state = rs_idle;
		}
		/* determine ramping state */
		else if ( (*data->target_power > 0 && data->current_power  >= 0) 
				|| (*data->target_power <= 0 && data->current_power  > 0) ) {
			/* enter state */
			if (data->ramp_state != rs_forward) {

			if (data->debug && *data->debug) printf("%s ramping fwd. target: %ld, current %ld\n", data->conveyor_name, *data->target_power, data->current_power);
				data->ramp_state = rs_forward;
				data->ramp_start_time = now_t;
				data->ramp_start_power = data->current_power;
				data->ramp_start_target = *data->target_power;
			}
			/* handle changes to the ramp power */
			if (*data->target_power != data->ramp_start_target) {
				if (data->debug && *data->debug) printf("%s ramp target changed (%ld != %ld)\n", data->conveyor_name, *data->target_power, data->ramp_start_target);
				data->ramp_start_time = now_t;
				data->ramp_start_power = data->current_power;
				data->ramp_start_target = *data->target_power;
			}
		}
		else if ( (*data->target_power < 0 && data->current_power  <= 0) 
				|| (*data->target_power >= 0 && data->current_power  < 0) ) {
			if (data->ramp_state != rs_reverse) {
			if (data->debug && *data->debug) printf("%s ramping rev. target: %ld, current %ld\n", data->conveyor_name, *data->target_power, data->current_power);
    			data->ramp_state = rs_reverse;
    			data->ramp_start_time = now_t;
    			data->ramp_start_power = data->current_power;
				data->ramp_start_target = *data->target_power;
			}
			/* handle changes to the ramp power */
			if (*data->target_power != data->ramp_start_target) {
				if (data->debug && *data->debug) printf("%s ramp target changed [rev] (%ld != %ld)\n", data->conveyor_name, *data->target_power, data->ramp_start_target);
				data->ramp_start_time = now_t;
				data->ramp_start_power = data->current_power;
				data->ramp_start_target = *data->target_power;
			}
		}
		else
			data->ramp_state = rs_idle;

		command = cmd_none;
		
		done_checking_states:;
	}

	switch (data->ramp_state) {
		case rs_forward: {
			/* calculate new output, set new output */
			new_power = (float) data->ramp_start_power;
			if (*data->target_power  > data->current_power ) {
				new_power += (float)(now_t - data->ramp_start_time) * *data->fwd_step_up / 1000000.0f;
				if (new_power > *data->target_power) new_power = *data->target_power;
 		    if (data->debug && *data->debug) printf("%s ramp forward calculation %6.3f pos: %ld\n", data->conveyor_name, new_power, *data->position);
			}
			else if (*data->target_power  < data->current_power ){
				new_power -= (float)(now_t - data->ramp_start_time) * *data->fwd_step_down / 1000000.0f;
				if (new_power < *data->target_power) new_power = *data->target_power;
 		    if (data->debug && *data->debug) printf("%s ramp forward calculation %6.3f pos: %ld\n", data->conveyor_name, new_power, *data->position);
			}
			else {
			    data->ramp_state = rs_idle;
		        goto done_polling_actions;
			}
		}   
		break;
		case rs_reverse: {
			/* calculate new output, set new output */
			new_power = (float) data->ramp_start_power;
			if (*data->target_power  < data->current_power ) {
				new_power -= (float)(now_t - data->ramp_start_time) * *data->rev_step_up / 1000000.0f;
				if (new_power < *data->target_power ) new_power = *data->target_power ;
 		    if (data->debug && *data->debug) printf("%s ramp reverse calculation %6.3f pos: %ld\n", data->conveyor_name, new_power, *data->position);
			}
			else if (*data->target_power  > data->current_power ){
				new_power += (float)(now_t - data->ramp_start_time) * *data->fwd_step_down / 1000000.0f;
				if (new_power  > *data->target_power ) new_power = *data->target_power;
 		    if (data->debug && *data->debug) printf("%s ramp reverse calculation %6.3f pos: %ld\n", data->conveyor_name, new_power, *data->position);
			}
			else {
			    data->ramp_state = rs_idle;
		        goto done_polling_actions;
			}
		}   
		break;
		default: 
		    goto done_polling_actions;
	}

	if (new_power != data->current_power) {
		if (data->debug && *data->debug) printf("%s setting power to %d\n", data->conveyor_name, (int)new_power);
		setIntValue(scope, "driver.VALUE", output_scaled( data, (long)new_power) );
		data->current_power = new_power;
	}

done_polling_actions:
	if (current) { free(current); current = 0; }
	if (control) { free(control); control = 0; }
	data->last_poll = now_t;
	
    return PLUGIN_COMPLETED;
}

%END_PLUGIN

	OPTION SetPoint 0;
	OPTION StopPosition 0;
	OPTION TargetPower 0;
	OPTION StopMarker 0;
	OPTION DEBUG 0;
	
	control SPEEDCONTROL;
	
	interlocked WHEN driver IS interlocked;
	stopped WHEN control IS stop;
	waiting DEFAULT;

	COMMAND stop { SET control TO stop; }

	COMMAND MarkPos {
		StopMarker := pos.VALUE;
		IF (SetPoint > 0) {
			StopPosition := StopMarker + fwd_settings.StoppingDistance;
		} ELSE {
			StopPosition := StopMarker - rev_settings.StoppingDistance;
		};
	}

	# convenience commands
	COMMAND slow {
	    SetPoint := fwd_settings.SlowSpeed; 
	}
	
	COMMAND start { 
    	SetPoint := fwd_settings.FullSpeed;
    }
	
	COMMAND slowrev { 
	    SetPoint := rev_settings.SlowSpeed;
	}

	COMMAND startrev { 
	    SetPoint := rev_settings.FullSpeed;
	}

	COMMAND clear {
		StopPosition := pos.VALUE;
	}
}
