RAMPSPEEDCONTROLLER_PLUGIN MACHINE M_Control, settings, fwd_settings, rev_settings, driver, freq_sampler {
    PLUGIN "ramp_controller.so";

%BEGIN_PLUGIN
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/time.h>

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

	/* ramp settings */
	long *min_update_time;
	long *fwd_steup_up;
	long *fwd_step_down;
	long *rev_step_up;
	long *rev_step_down;

	long *output_value;

	/* internal values for ramping */
	long target_power;
	long current_power;
	uint64_t last_poll;

	/* stop/start control */
    long stop_marker;
	
};

int getInt(void *scope, const char *name, long **addr) {
	
	if (!getIntValue(scope, name, addr)) {
		char buf[100];
		snprintf(buf, 100, "RampSpeedController: %s is not an integer", name);
		return 0;
	}
	return 1;
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

		ok = ok && getInt(scope, "SetPoint", &data->set_point);
		ok = ok && getInt(scope, "StopPosition", &data->stop_position);
		ok = ok && getInt(scope, "freq_sampler.VALUE", &data->speed);
		ok = ok && getInt(scope, "freq_sampler.position", &data->position);
		ok = ok && getInt(scope, "settings.min_update_time", &data->min_update_time);
		ok = ok && getInt(scope, "settings.min_update_time", &data->min_update_time);
		ok = ok && getInt(scope, "settings.min_update_time", &data->min_update_time);
		ok = ok && getInt(scope, "settings.min_update_time", &data->min_update_time);
		ok = ok && getInt(scope, "settings.fwd_steup_up", &data->fwd_steup_up);
		ok = ok && getInt(scope, "settings.fwd_step_down", &data->fwd_step_down);
		ok = ok && getInt(scope, "settings.rev_step_up", &data->rev_step_up);
		ok = ok && getInt(scope, "settings.rev_step_down", &data->rev_step_down);

		ok = ok && getInt(scope, "driver.VALUE", &data->output_value);

		if (!ok) goto plugin_init_error;

		data->state = cs_init;
		data->target_power = 0;
		data->current_power = 0;
		data->last_poll = 0;

		data->stop_marker = 0;
		data->set_point = 0;
		data->stop_position = 0; 

        goto continue_plugin;
plugin_init_error:
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
        printf("test: %s %ld\n", (current)? current : "null", *data->position);

		data->last_poll = now_t;
	}
    return PLUGIN_COMPLETED;
}
    
PLUGIN_EXPORT
int poll_actions(void *scope) {
	struct RCData *data = (struct RCData*)getInstanceData(scope);
	if (!data) return PLUGIN_COMPLETED; /* not initialised yet; nothing to do */

	switch (data->ramp_state) {
		case rs_forward: {
			/* calculate new output, set new output */
		}   
		break;
		case rs_reverse: {
			/* calculate new output, set new output */
		}   
		break;
		default: ;
	}
    return PLUGIN_COMPLETED;
}

%END_PLUGIN

	OPTION SetPoint 0;
	OPTION StopPosition 0;
	
	active FLAG;
	timer TIMER;
	state SPEEDCONTROL;
	
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
    
	# starting the conveyor to move to a preset point

	starting_to_position WHEN SELF IS waiting 
	            && (state IS off || state IS seeking_position && SetPoint == 0) AND StopPosition != 0;

	# switching operating modes
	startupError WHEN SELF IS starting && TIMER > settings.StartTimeout;
	starting WHEN state IS ramping && freq_sampler.VALUE < 5 AND freq_sampler.VALUE > -5;

	ENTER StartupError { SET state TO failed_to_start; SET M_Control TO Error }

	changing WHEN SELF IS waiting AND state != seeking_position && VALUE != 0 && Target != VALUE;
	stopping WHEN SELF IS waiting AND (state == ramping || state == monitoring || state == seeking_position) 
	        AND LastSent != 0 AND Target == VALUE && VALUE == 0;
	at_speed WHEN SELF IS updated AND state IS ramping AND SpeedPercent > 90 AND SpeedPercent < 110;
		stopped WHEN SELF IS waiting AND state IS stopping AND freq_sampler.VALUE < 5 && freq_sampler.VALUE > -5;
   
 	# stopping at a position (state == seeking_position)
	# (these rely on tolerance having been set before the state is set to seeking_position)

 	driving_fwd WHEN state IS seeking_position AND VALUE > 0 AND freq_sampler.position <= StopMarker + fwd_settings.TravelAllowance;
 	driving_rev WHEN state IS seeking_position AND VALUE < 0 AND freq_sampler.position >= StopMarker - rev_settings.TravelAllowance;
 	at_position WHEN state IS seeking_position AND freq_sampler.position < StopPosition + tolerance AND freq_sampler.position > StopPosition - tolerance;
 	ramping_down WHEN SELF IS waiting AND state IS seeking_position && SetPoint != 0;
 	abort WHEN state IS seeking_position 
 	        AND ( (VALUE > 0 AND freq_sampler.position > StopPosition) || (VALUE < 0 AND freq_sampler.position < StopPosition));
 	
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

	# monitoring
	updated WHEN SELF IS updating AND Target != 0;
	updating WHEN (state == monitoring || state == ramping) && SELF IS waiting AND timer IS expired;
	check WHEN SELF IS waiting AND TIMER > 1000;
	waiting DEFAULT;
    
	ENTER updated { SpeedPercent := 100 * freq_sampler.VALUE / Target; }
	ENTER updating {
		timer.timeout := settings.min_update_time;
		SEND start TO timer;
	}
       
	ENTER waiting {
		EstimatedSpeed := freq_sampler.VALUE;
	}

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
