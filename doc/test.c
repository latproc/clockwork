#line 552 "../Lib/generic_analog.lpc"
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
		cs_off, 		/* sleeping */
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

	/* stop/start control */
    long stop_marker;
	
};

int getInt(void *scope, const char *name, long **addr) {
	
	if (!getIntValue(scope, name, addr)) {
		char buf[100];
		char *val = getStringValue(scope, name);
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

  printf("scaled output power %d to %d\n", (int)power, (int)raw);
	return raw;
}
    
PLUGIN_EXPORT
int check_states(void *scope)
{
	int ok = 1;
	long calculated_target_power = 0;
	long calculated_set_point = 0;
	long calculated_stop_position = 0;
	struct RCData *data = (struct RCData*)getInstanceData(scope);
	if (!data) {
		// one-time initialisation
		data = (struct RCData*)malloc(sizeof(struct RCData));
		memset(data, 0, sizeof(struct RCData));
		setInstanceData(scope, data);

		ok = ok && getInt(scope, "SetPoint", &data->set_point);
		ok = ok && getInt(scope, "StopMarker", &data->mark_position);
		ok = ok && getInt(scope, "StopPosition", &data->stop_position);
		ok = ok && getInt(scope, "TargetPower", &data->target_power);
		ok = ok && getInt(scope, "freq_sampler.VALUE", &data->speed);
		ok = ok && getInt(scope, "freq_sampler.position", &data->position);
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

		if (!ok) {
			printf("plugin failed to initialise\n");
			setInstanceData(scope, 0);
			free(data);
			return PLUGIN_ERROR;
		}

		data->state = cs_init;
		data->current_power = 0;
		data->last_poll = 0;

		data->stop_marker = 0;
		data->last_stop_position = *data->stop_position;
		data->last_set_point = *data->set_point;
		calculated_set_point = data->last_set_point;
		calculated_target_power = *data->target_power;

		printf("plugin initialised ok: update time %ld\n", *data->min_update_time);
}

    
PLUGIN_EXPORT
int poll_actions(void *scope) {
	struct RCData *data = (struct RCData*)getInstanceData(scope);
	struct timeval now;
	uint64_t now_t = 0;
	float new_power = 0.0f;
	if (!data) return PLUGIN_COMPLETED; /* not initialised yet; nothing to do */

	gettimeofday(&now, 0);
	now_t = now.tv_sec * 1000000 + now.tv_usec;

	if ( (now_t - data->last_poll)/1000 < *data->min_update_time) return PLUGIN_COMPLETED;

//printf("poll actions %d\n", (now_t - data->last_poll)/1000);
	{
		struct timeval now;
		gettimeofday(&now, 0);
		uint64_t now_t = now.tv_sec * 1000000 + now.tv_usec;
		char *current = getState(scope);
		void *controller = 0;
		char *control = 0;
		/*printf("test: %s %ld, stop: %ld, pow: %ld, pos: %ld\n", (current)? current : "null", 
                *data->set_point,
                *data->stop_position,
                *data->target_power,
                *data->position);
		*/
		controller = getNamedScope(scope, "state");
		if (controller) {
			control = getState(controller);
			printf("controller state: %s\n", (control) ? control : "null");
		}

		int interlocked = strcmp(current, "interlocked") == 0;

		/* if the stop position has changed, we attempt to move to that position */
		if (! interlocked && *data->stop_position && data->last_stop_position != *data->stop_position) {
			printf("new stop position %ld\n" , *data->stop_position);
			data->last_stop_position = *data->stop_position;
			if (data->state == cs_off) 
				data->state = cs_startup_seeking;
			else {
				data->state = cs_seeking;
				data->stop_marker = *data->mark_position;
			}
			void *std_state = getNamedScope(scope, "M_Control");
			if (std_state) changeState(std_state, "Resetting");
		}
		/* otherwise, if the set point was changed, use it */
		else if (!interlocked && data->last_set_point != *data->set_point) {
			printf("new set point %ld\n" , *data->set_point);
			setIntValue(scope, "StopPosition", 0);
			data->last_set_point = *data->set_point;
			data->state = cs_ramping;
			void *std_state = getNamedScope(scope, "M_Control");
			if (std_state) changeState(std_state, "Resetting");
		}
		/* otherwise, if we are interlocked or off, stop everything */
		else if (interlocked || strcmp(current, "off") == 0 || control && strcmp(control, "off") == 0 ) {
			printf("stopping\n");
			data->current_power = 0;
			data->stop_marker = 0;
			data->state = cs_off;
			void *std_state = getNamedScope(scope, "M_Control");
			if (std_state) changeState(std_state, "Ready");
			setIntValue(scope, "TargetPower", 0);
			setIntValue(scope, "driver.VALUE", output_scaled(data, 0) );
			goto done_checking_states;
		}

		if (data->state == cs_startup_seeking) {
			// also set control to Resetting
			printf("starting to a position: %ld\n", *data->stop_position);
			data->state = cs_seeking;
			if (*data->position >= *data->stop_position - *data->fwd_tolerance) {
				data->stop_marker = *data->stop_position - *data->fwd_stopping_distance;
				if (*data->stop_position - *data->position < *data->fwd_stopping_distance)
					calculated_set_point = *data->fwd_slow_speed;
				else
					calculated_set_point = *data->fwd_full_speed;
				calculated_target_power = *data->set_point * *data->fwd_power_factor / 100;
				data->tolerance = *data->fwd_tolerance;
			}
			else if (*data->position <= *data->stop_position + *data->rev_tolerance) {
				data->stop_marker = *data->stop_position + *data->rev_stopping_distance;
				if (calculated_stop_position - *data->position < *data->rev_stopping_distance)
					calculated_set_point = *data->rev_slow_speed;
				else
					calculated_set_point = *data->rev_full_speed;
				calculated_target_power = *data->set_point * *data->rev_power_factor / 100;
				data->tolerance = *data->rev_tolerance;
			}
			else {
			    calculated_set_point = 0;
			    calculated_target_power = 0;
			    printf("stopping error: %ld", *data->stop_position - *data->position);
			    setIntValue(scope, "TargetPower", calculated_target_power);
			}

			setIntValue(scope, "SetPoint", calculated_set_point);
			calculated_target_power = *data->set_point * *data->rev_power_factor / 100;
			setIntValue(scope, "TargetPower", calculated_target_power);
		}

		if (data->state == cs_ramping || data->last_set_point != *data->set_point) { /* setpoint has changed */
		    printf("new setpoint: %ld; fixing power: ", *data->set_point);
			data->last_set_point = *data->set_point;
			calculated_target_power = *data->set_point * *data->rev_power_factor / 100;
			setIntValue(scope, "TargetPower", calculated_target_power);
			printf("setting requested power: %ld\n", calculated_target_power);
		}
		if (data->state == cs_ramping  && data->current_power == *data->target_power) {
			data->state = cs_monitoring;
			printf("changing state to monitoring\n");
		}
		
		if ( data->state != cs_seeking && data->state != cs_seeking_slow && data->state != cs_interlocked && *data->stop_position != 0) {
				printf("detected stop position %ld (mark: %ld) state<-seeking (was %d)\n", *data->stop_position, *data->mark_position, data->state);
		    data->state = cs_seeking;
				if (*data->stop_position < *data->position)
					data->stop_marker = *data->mark_position;
		}

		if ( data->state == cs_seeking || data->state == cs_seeking_slow ) {
			if ( data->current_power > 0 
					&& *data->position < data->stop_marker + *data->fwd_travel_allowance )
				printf("coasting: %ld < %ld\n", *data->position, data->stop_marker + *data->fwd_travel_allowance);
			else if (data->current_power < 0
					&& *data->position > data->stop_marker - *data->rev_travel_allowance ) {
				printf("coasting: %ld > %ld\n", *data->position, data->stop_marker + *data->fwd_travel_allowance);
			}
			else if ( ( data->current_power > 0
					&& *data->position >= *data->stop_position  - *data->fwd_tolerance  )
				|| ( data->current_power < 0
					&& *data->position <= *data->stop_position + *data->rev_tolerance ) )
			{
		    printf("halting at %ld, aiming for %ld (error %ld), speed %ld\n", 
						*data->position, *data->stop_position, (*data->position - *data->stop_position), *data->speed);
				changeState(control, "off");
				data->state = cs_off;
				setIntValue(scope, "TargetPower", 0);
				setIntValue(scope, "driver.VALUE", output_scaled(data, 0) );
/*
				calculated_set_point = 0;
				calculated_target_power = 0;
				data->current_power = 0;
				data->stop_marker = 0;
				if ( fabs(*data->speed) < 50 ) {
					calculated_stop_position = 0;
					data->state = cs_off;
					setIntValue(scope, "StopPosition", 0); 
				}
				else 
				    printf("speed is %ld\n", *data->speed);
*/
			}
			else if (data->current_power == *data->target_power && data->state != cs_seeking_slow){
			    printf("ramping down at %ld aiming for %ld (overshoot: %ld), (state: %d)\n", 
							*data->position, *data->stop_position, (*data->position - *data->stop_position), data->state);
			    data->state = cs_seeking_slow;
				/* begin ramping down */
				if (data->current_power > 0)
					calculated_target_power = *data->fwd_slow_speed * *data->fwd_power_factor / 100;
				else if (data->current_power < 0)
					calculated_target_power = *data->rev_slow_speed * *data->rev_power_factor / 100;
				else {
				    calculated_target_power = 0;
						setIntValue(scope, "driver.VALUE", output_scaled(data, 0) );
						if (controller) changeState(controller, "off");
				}
				setIntValue(scope, "TargetPower", calculated_target_power);
				printf("new power target %ld, current power %ld\n", calculated_target_power, data->current_power);
			}
			else if (data->current_power == 0 && *data->target_power == 0 ) {
				calculated_stop_position = 0;
				data->state = cs_off;
				/* set control to ready */
				if (*data->position < *data->stop_position - *data->fwd_tolerance
							&& *data->position > *data->stop_position + *data->rev_tolerance) {
					/* make last_stop_position different to stop_position*/
					printf("premature stop?\n");
					data->last_stop_position = *data->stop_position -1; 
					data->state = cs_startup_seeking;
				}
				else if (controller) {
					void *std_state = getNamedScope(scope, "M_Control");
					if (std_state) changeState(std_state, "Resetting");
					changeState(controller, "off");
				}
			}
		}

		/* determine ramping state */
		if ( (*data->target_power > 0 && data->current_power >= 0) 
				|| (*data->target_power <= 0 && data->current_power > 0) ) {
			/* enter state */
			if (data->ramp_state != rs_forward) {
				data->ramp_state = rs_forward;
				data->ramp_start_time = now_t;
				data->ramp_start_power = data->current_power;
				data->ramp_start_target = *data->target_power;
			}
			/* handle changes to the ramp power */
			if (*data->target_power != data->ramp_start_target) {
				printf("ramp target changed (%ld != %ld)\n", *data->target_power, data->ramp_start_target);
				data->ramp_start_time = now_t;
				data->ramp_start_power = data->current_power;
				data->ramp_start_target = *data->target_power;
			}
		}
		else if ( (*data->target_power < 0 && data->current_power <= 0) 
				|| (*data->target_power >= 0 && data->current_power < 0) ) {
			if (data->ramp_state != rs_reverse) {
    			data->ramp_state = rs_reverse;
    			data->ramp_start_time = now_t;
    			data->ramp_start_power = data->current_power;
				data->ramp_start_target = *data->target_power;
			}
			/* handle changes to the ramp power */
			if (*data->target_power != data->ramp_start_target) {
				printf("ramp target changed [rev] (%ld != %ld)\n", *data->target_power, data->ramp_start_target);
				data->ramp_start_time = now_t;
				data->ramp_start_power = data->current_power;
				data->ramp_start_target = *data->target_power;
			}
		}
		else
			data->ramp_state = rs_idle;

		done_checking_states:;
	}

	switch (data->ramp_state) {
		case rs_forward: {
			/* calculate new output, set new output */
			new_power = (float) data->ramp_start_power;
			if (*data->target_power > data->current_power) {
				new_power += (float)(now_t - data->ramp_start_time) * *data->fwd_step_up / 1000000.0f;
				if (new_power > *data->target_power) new_power = *data->target_power;
    		    printf("ramp forward calculation %6.3f\n", new_power);
			}
			else if (*data->target_power < data->current_power){
				new_power -= (float)(now_t - data->ramp_start_time) * *data->fwd_step_down / 1000000.0f;
				if (new_power < *data->target_power) new_power = *data->target_power;
    		    printf("ramp forward calculation %6.3f\n", new_power);
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
			if (*data->target_power < data->current_power) {
				new_power -= (float)(now_t - data->ramp_start_time) * *data->rev_step_up / 1000000.0f;
				if (new_power < *data->target_power) new_power = *data->target_power;
    		    printf("ramp forward calculation %6.3f\n", new_power);
			}
			else if (*data->target_power > data->current_power){
				new_power += (float)(now_t - data->ramp_start_time) * *data->fwd_step_down / 1000000.0f;
				if (new_power > *data->target_power) new_power = *data->target_power;
    		    printf("ramp forward calculation %6.3f\n", new_power);
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
		printf("setting power to %d\n", (int)new_power);
		setIntValue(scope, "driver.VALUE", output_scaled( data, (long)new_power) );
		data->current_power = new_power;
	}

done_polling_actions:
	data->last_poll = now_t;
	
    return PLUGIN_COMPLETED;
}

