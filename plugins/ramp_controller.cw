status STANDARDCONTROL;
speed_settings RampSettings;
forward_config DirectionSettings;
reverse_config DirectionSettings;
power DummyDriver;
freq DummySampler;
controller RampSpeedController status, speed_settings, forward_config, reverse_config, power, freq;

DummyDriver MACHINE {
	OPTION VALUE 0;
	active FLAG;
}

DummySampler MACHINE {
	OPTION position 0;
	OPTION VALUE 0;
}

Status MACHINE {
	ramping STATE;
	failed_to_start STATE;
	stopping STATE;
	monitoring STATE;
	seeking_position STATE;
	off INITIAL;
}

RampSettings MACHINE {
	OPTION StartTimeout 20000;
	OPTION min_update_time 20; # minimum time between normal control updates
	OPTION fwd_step_up 4000; # maximum change per second
	OPTION fwd_step_down 6000; # maximum change per second
	OPTION rev_step_up 4000; # maximum change per second
	OPTION rev_step_down 6000; # maximum change per second
}

DirectionSettings MACHINE {
	OPTION SlowSpeed 40;
	OPTION FullSpeed 1000;
	OPTION PowerFactor 750; 				 # converts from a speed value to a power value
	OPTION StoppingDistance 3000;  # distance from the stopping point to begin slowing down
	OPTION TravelAllowance 0; # amount to travel after the mark before slowing down
	OPTION tolerance 100;        #stopping position tolerance
}

ControlState MACHINE {
	ramping STATE;
	failed_to_start STATE;
	stopping STATE;
	monitoring STATE;
	seeking_position STATE;
	off INITIAL;
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

RampSpeedController MACHINE M_Control, settings, fwd_settings, rev_settings, driver, freq_sampler {
    PLUGIN "ramp_controller.so";

%BEGIN_PLUGIN
#include <Plugin.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/time.h>
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
		cs_seeking 		/* driving with a set position set */
	} state;

	/**** clockwork interface ***/
    long *set_point;
    long *stop_position; 
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

	/* stop/start control */
    long stop_marker;
	
};

int getInt(void *scope, const char *name, long **addr) {
	
	if (!getIntValue(scope, name, addr)) {
		char buf[100];
		snprintf(buf, 100, "RampSpeedController: %s is not an integer", name);
		printf("%s\n", buf);
		log_message(scope, buf);
		return 0;
	}
	printf("%s: %d\n", name, **addr);
	return 1;
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

		if (!ok) goto plugin_init_error;

		data->state = cs_init;
		data->current_power = 0;
		data->last_poll = 0;

		data->stop_marker = 0;
		setIntValue(scope, "SetPoint", 0);
		setIntValue(scope, "StopPosition", 0);
		data->last_set_point = *data->set_point;
		calculated_set_point = data->last_set_point;
		calculated_target_power = *data->target_power;

		printf("plugin initialised ok: update time %d\n", *data->min_update_time);
        goto continue_plugin;

plugin_init_error:
		printf("plugin failed to initialise\n");
        setInstanceData(scope, 0);
        free(data);
        return PLUGIN_ERROR;
    }

continue_plugin:
	{
		struct timeval now;
		gettimeofday(&now, 0);
		uint64_t now_t = now.tv_sec * 1000000 + now.tv_usec;

        char *current = getState(scope);
        printf("test: %s %ld\n", (current)? current : "null", *data->target_power); 

		if (strcmp(current, "off") == 0 || strcmp(current, "interlocked") == 0) 
			goto done_checking_states;

		if ( (*data->stop_position != 0) 
				&& ( (data->state == cs_off) || (data->state == cs_seeking && *data->set_point == 0) ) ) {
			// also set control to Resetting
			data->state = cs_seeking;
			if (*data->stop_position > *data->position + *data->fwd_tolerance) {
				data->stop_marker = *data->stop_position + *data->fwd_stopping_distance;
				if (*data->stop_position - *data->position < *data->fwd_stopping_distance)
					calculated_set_point = *data->fwd_slow_speed;
				else
					calculated_set_point = *data->fwd_full_speed;
				calculated_target_power = *data->set_point * *data->fwd_power_factor / 100;
				data->tolerance = *data->fwd_tolerance;
			}
			else if (*data->stop_position < *data->position - *data->rev_tolerance) {
				data->stop_marker = *data->stop_position + *data->rev_stopping_distance;
				if (calculated_stop_position - *data->position < *data->rev_stopping_distance)
					calculated_set_point = *data->rev_slow_speed;
				else
					calculated_set_point = *data->rev_full_speed;
				calculated_target_power = *data->set_point * *data->rev_power_factor / 100;
				data->tolerance = *data->rev_tolerance;
			}

			setIntValue(scope, "StopPosition", calculated_stop_position);
			setIntValue(scope, "SetPoint", calculated_set_point);
			data->last_set_point = *data->set_point;
		}

		if (data->last_set_point != *data->set_point) { /* setpoint has changed */
			data->last_set_point = *data->set_point;
			calculated_target_power = *data->set_point * *data->rev_power_factor / 100;
		}

		if ( data->state == cs_seeking ) {
			if ( ( data->current_power > 0 
					&& *data->position <= data->stop_marker + *data->fwd_travel_allowance )
			  || (data ->current_power < 0
					&& *data->position >= data->stop_marker - *data->rev_travel_allowance ) ) {
				/* coasting */
			}
			else if ( ( data->current_power > 0
					&& *data->position >= *data->stop_position  + *data->fwd_tolerance  )
				|| ( data->current_power < 0
					&& *data->position >= *data->stop_position - *data->rev_tolerance ) )
			{
				/* force a stop */
				calculated_set_point = 0;
				calculated_target_power = 0;
				data->current_power = 0;
				data->stop_marker = 0;
				if ( fabs(*data->speed) < 50 ) {
					calculated_stop_position = 0;
					/* set control to ready */
					data->state = cs_off;
					setIntValue(scope, "StopPosition", 0); 
				}
				setIntValue(scope, "TargetPower", calculated_target_power);
				setIntValue(scope, "driver.VALUE", calculated_target_power);
			}
			else {
				/* begin ramping down */
				if (data->current_power > 0)
					calculated_target_power = *data->fwd_slow_speed * *data->fwd_power_factor / 100;
				else 
					calculated_target_power = *data->rev_slow_speed * *data->rev_power_factor / 100;
				setIntValue(scope, "TargetPower", calculated_target_power);
			}
		}

		/* ramping calculation */
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
				data->ramp_start_time = now_t;
				data->ramp_start_power = data->current_power;
				data->ramp_start_target = *data->target_power;
			}
		}
		else if ( (*data->target_power < 0 && data->current_power <= 0) 
				|| (*data->target_power >= 0 && data->current_power < 0) ) {
			data->ramp_state = rs_reverse;
			data->ramp_start_time = now_t;
			data->ramp_start_power = data->current_power;
		}
		else
			data->ramp_state = rs_idle;

		done_checking_states:;
	}
    return PLUGIN_COMPLETED;
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

	switch (data->ramp_state) {
		case rs_forward: {
			/* calculate new output, set new output */
			new_power = (float) data->ramp_start_power;
			if (*data->target_power > data->current_power) {
				new_power += (float)(now_t - data->ramp_start_time) * *data->fwd_step_up / 1000000.0f;
				printf("new power: %f\n", new_power);
				if (new_power > *data->target_power) new_power = *data->target_power;
			}
			else {
				new_power -= (float)(now_t - data->ramp_start_time) * *data->fwd_step_down / 1000000.0f;
				if (new_power < *data->target_power) new_power = *data->target_power;
			}
		}   
		break;
		case rs_reverse: {
			/* calculate new output, set new output */
			new_power = (float) data->ramp_start_power;
			if (*data->target_power < data->current_power) {
				new_power -= (float)(now_t - data->ramp_start_time) * *data->rev_step_up / 1000000.0f;
				if (new_power < *data->target_power) new_power = *data->target_power;
			}
			else {
				new_power += (float)(now_t - data->ramp_start_time) * *data->fwd_step_down / 1000000.0f;
				if (new_power > *data->target_power) new_power = *data->target_power;
			}
		}   
		break;
		default: 
		    goto done_polling_actions;
	}

	if (new_power != data->current_power) {
		printf("setting power to %d\n", (uint32_t)new_power);
		setIntValue(scope, "driver.VALUE", (long)new_power);
		data->current_power = new_power;
	}

done_polling_actions:
	data->last_poll = now_t;
	
    return PLUGIN_COMPLETED;
}

%END_PLUGIN

	OPTION SetPoint 0;
	OPTION StopPosition 0;
	OPTION TargetPower 0;
	
	active FLAG;
	state ControlState;
	timer TIMER;
	
	# configuration checking
	fwd_check RAMPSPEEDCONFIGCHECK fwd_settings;
	rev_check RAMPSPEEDCONFIGCHECK rev_settings;

	config_error WHEN fwd_check IS error || rev_check IS error;
	ENTER config_error { SET M_Control TO Error }

	interlocked WHEN driver IS interlocked;
	ENTER interlocked { SET M_Control TO Unavailable; }
	
	error WHEN state IS failed_to_start;
	ENTER error { SEND sleep TO SELF }

	# on/off control
	off WHEN active IS off;

	# sleep control
	ENTER off { 
	    SET driver.active TO off; 
	    SET state TO off;
	    SEND reset TO timer;
		SET M_Control TO Ready;
	}
	LEAVE off { 
	    SET driver.active TO on; 
			timer.timeout := settings.min_update_time;
			SEND start TO timer;
	}
	COMMAND wakeup { 
		SEND wakeup TO freq_sampler;
		WAIT 1;
		SEND wakeup TO driver;
		WAIT 1;
		SET active TO on;
	}

	COMMAND sleep { 
		CALL stop ON SELF;
		WAIT 2;
		SEND sleep TO driver;
		WAIT 2;
		SEND sleep TO freq_sampler;
		WAIT 2;
		SET active TO off;
	}
    
	# switching operating modes
	startupError WHEN SELF IS starting && TIMER > settings.StartTimeout;
	starting WHEN state IS ramping && freq_sampler.VALUE < 5 AND freq_sampler.VALUE > -5;

	ENTER StartupError { SET state TO failed_to_start; SET M_Control TO Error }

 	COMMAND halt {
		SetPoint := 0; 
		Current := 0; 
		LastSent := 0;
		driver.VALUE := 0; 
		StopMarker := 0; 
		SET state TO off;
		IF (freq_sampler.position < StopPosition + tolerance 
		        AND freq_sampler.position > StopPosition - tolerance 
		        && freq_sampler.VALUE < 100 && freq_sampler.VALUE > -100) {
		    StopPosition := 0;
				SET M_Control TO Ready;
		}
 	}

	waiting DEFAULT;

	COMMAND stop { 
		StopPosition := 0; # disable stop position
		SetPoint := 0; 
		Current := 0; 
		LastSent := 0;
		driver.VALUE := 0; 
		StopMarker := 0; 
		SET state TO off;
	}

	COMMAND MarkPos {
		SET M_Control TO Resetting;
		SET state TO seeking_position;
		StopMarker := freq_sampler.position;
		IF (freq_sampler.VALUE >= 0) {
			StopPosition := StopMarker + fwd_settings.StoppingDistance;
		} ELSE {
			StopPosition := StopMarker - rev_settings.StoppingDistance;
		};
	}

	# convenience commands
	COMMAND slow {
	    IF (active IS off) { SEND wakeup TO SELF; };
	    SetPoint := fwd_settings.SlowSpeed; 
	}
	
	COMMAND start { 
	    IF (active IS off) { SEND wakeup TO SELF; };
    	SetPoint := fwd_settings.FullSpeed;
    }
	
	COMMAND slowrev { 
	    IF (active IS off) { SEND wakeup TO SELF; };
	    SetPoint := rev_settings.SlowSpeed;
	}

	COMMAND startrev { 
	    IF (active IS off) { SEND wakeup TO SELF; };
	    SetPoint := rev_settings.FullSpeed;
	}
}
