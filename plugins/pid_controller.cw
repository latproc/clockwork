PIDSPEEDCONFIGURATION MACHINE {
  OPTION PERSISTENT true;
  EXPORT RW 32BIT SlowSpeed, FullSpeed, PowerFactor, StoppingDistance, TravelAllowance, tolerance, Kp, Ki, Kd;
  OPTION SlowSpeed 40;
  OPTION FullSpeed 1000;
  OPTION StoppingDistance 3000;  # distance from the stopping point to begin slowing down
  OPTION TravelAllowance 0;      # amount to travel after the mark before slowing down
  OPTION tolerance 100;          # stopping position tolerance

	OPTION Kp 100000;
	OPTION Ki 0;
	OPTION Kd 0;
}


PIDCONFIGURATION MACHINE {
	OPTION PERSISTENT true;
	EXPORT RW 32BIT min_update_time, StartTimeout, startup_time, stopping_time, Kp, Ki, Kd;

	OPTION min_update_time 20; # minimum time between normal control updates
	OPTION StartTimeout 500;	 # conveyor start timeout
	OPTION inverted false;     # do not invert power
	OPTION startup_time 500;   # 500ms startup ramp time
	OPTION stopping_time 300;  # 300ms stopping time
	OPTION MinSpeed 1000; 
	OPTION RampLimit 1000;		# maximum increase in control power per cycle

	OPTION Kp 1000000;
	OPTION Ki 0;
	OPTION Kd 0;
}

PIDCONTROLLER MACHINE M_Control, settings, output_settings, fwd_settings, rev_settings, driver, pos {
    PLUGIN "pid_controller.so.1.0";

%BEGIN_PLUGIN
#include <Plugin.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/time.h>
#include <stdint.h>
#include <math.h>
#include <arraystr.c>
#include <buffering.c>

enum State { 
		cs_init,
		cs_interlocked, /* something upstream is interlocked */
		cs_stopped, 		/* sleeping */
		cs_prestart,		/* waiting for pressure to build up */
		cs_position,
		cs_speed,
		cs_atposition
};

struct PIDData {
	enum State state;
	struct CircularBuffer *samples;
	struct CircularBuffer *fwd_power_offsets;
	struct CircularBuffer *rev_power_offsets;

	/**** clockwork interface ***/
	long *set_point;
	long *stop_position;
	long *mark_position;
	long *position;

	/* ramp settings */
	long *min_update_time;
	long *stopping_time;
	long *startup_time;
	long *min_speed;
	long *ramp_limit;

	long *max_forward;
	long *max_reverse;
	long *zero_pos;
	long *min_forward;
	long *min_reverse;

	long *fwd_tolerance;
	long *fwd_stopping_dist;
	long *fwd_ramp_time;

	long *rev_tolerance;
	long *rev_stopping_dist;
	long *rev_ramp_time;

	/* statistics/debug */
	long *stop_error;
	long *estimated_speed;
	long *current_position;

	/* internal values for ramping */
	double current_power;
	uint64_t ramp_start_time;
	uint64_t last_poll;
	long last_position;
	long tolerance;
	long last_set_point;
	uint64_t changing_set_point;
	long last_stop_position;
	long default_debug; /* a default debug level if one is not specified in the script */
	int inverted;
	long start_position;
	double last_Ep;
	double filtered_Ep;
	double last_filtered_Ep;
	uint64_t last_sample_time;

	// keep a record of the power at which movement started
	long *fwd_start_power; 
	long *rev_start_power;
	// keep a record of how long it took to see movement
	uint64_t start_time;
	uint64_t fwd_start_delay; 
	uint64_t rev_start_delay;

	uint64_t last_prestart_change;
	long prestart_change_delay;
	
	uint64_t ramp_down_start;

	char *conveyor_name;

	/* stop/start control */
	long stop_marker;
	long *debug;

	long *Kp_long;
	long *Ki_long;
	long *Kd_long;

	double Kp;
	double Ki;
	double Kd;

	long *Kpf_long, *Kif_long, *Kdf_long, *Kpr_long, *Kir_long, *Kdr_long;
	double Kpf, Kif, Kdf, Kpr, Kir, Kdr;
  int use_Kpidf;
  int use_Kpidr;
	
	double total_err;
};

int getInt(void *scope, const char *name, long **addr) {
	struct PIDData *data = (struct PIDData*)getInstanceData(scope);
	if (!getIntValue(scope, name, addr)) {
		char buf[100];
		char *val = getStringValue(scope, name);
		if (data)
			snprintf(buf, 100, "%s PIDController: %s (%s) is not an integer", data->conveyor_name, name, (val) ? val : "null");
		else
			snprintf(buf, 100, "PIDController: %s (%s) is not an integer", name, (val) ? val : "null");
		printf("%s\n", buf);
		log_message(scope, buf);
		free(val);
		return 0;
	}
	/* printf("%s: %d\n", name, **addr); */
	return 1;
}

long output_scaled(struct PIDData * settings, long power) {

	long raw = *settings->zero_pos;
	if ( settings->inverted ) power = -power;
	if (power > 0)
		raw = power + *settings->min_forward;
	else if (power < 0)
		raw = power + *settings->min_reverse;
	
	if (raw < *settings->max_reverse) raw = *settings->max_reverse;
  if (raw > *settings->max_forward) raw = *settings->max_forward;

	return raw;
}

PLUGIN_EXPORT
int check_states(void *scope)
{
	int ok = 1;
	struct PIDData *data = (struct PIDData*)getInstanceData(scope);
	if (!data) {
		/* one-time initialisation*/
		data = (struct PIDData*)malloc(sizeof(struct PIDData));
		memset(data, 0, sizeof(struct PIDData));
		setInstanceData(scope, data);
		{ 
			data->conveyor_name = getStringValue(scope, "NAME");
			if (!data->conveyor_name) data->conveyor_name = strdup("UNKNOWN CONVEYOR");
		}

		data->samples = createBuffer(3);
		data->fwd_power_offsets = createBuffer(8);
		data->rev_power_offsets = createBuffer(8);
		ok = ok && getInt(scope, "SetPoint", &data->set_point);
		ok = ok && getInt(scope, "StopMarker", &data->mark_position);
		ok = ok && getInt(scope, "StopPosition", &data->stop_position);
		ok = ok && getInt(scope, "pos.VALUE", &data->position);
		ok = ok && getInt(scope, "settings.min_update_time", &data->min_update_time);
		ok = ok && getInt(scope, "settings.stopping_time", &data->stopping_time);
		ok = ok && getInt(scope, "settings.startup_time", &data->startup_time);
		ok = ok && getInt(scope, "settings.MinSpeed", &data->min_speed);
		ok = ok && getInt(scope, "settings.RampLimit", &data->ramp_limit);
		ok = ok && getInt(scope, "settings.Kp", &data->Kp_long);
		ok = ok && getInt(scope, "settings.Ki", &data->Ki_long);
		ok = ok && getInt(scope, "settings.Kd", &data->Kd_long);
		ok = ok && getInt(scope, "Velocity", &data->estimated_speed);
		ok = ok && getInt(scope, "Position", &data->current_position);
		ok = ok && getInt(scope, "StopError", &data->stop_error);
		ok = ok && getInt(scope, "MinForward", &data->fwd_start_power);
		ok = ok && getInt(scope, "MinReverse", &data->rev_start_power);

		ok = ok && getInt(scope, "output_settings.MaxForward", &data->max_forward);
		ok = ok && getInt(scope, "output_settings.MaxReverse", &data->max_reverse);
		ok = ok && getInt(scope, "output_settings.ZeroPos", &data->zero_pos);
		ok = ok && getInt(scope, "output_settings.MinForward", &data->min_forward);
		ok = ok && getInt(scope, "output_settings.MinReverse", &data->min_reverse);

		ok = ok && getInt(scope, "fwd_settings.tolerance", &data->fwd_tolerance);
		ok = ok && getInt(scope, "fwd_settings.StoppingDistance", &data->fwd_stopping_dist);
		ok = ok && getInt(scope, "fwd_settings.RampTime", &data->fwd_ramp_time);
		ok = ok && getInt(scope, "rev_settings.tolerance", &data->rev_tolerance);
		ok = ok && getInt(scope, "rev_settings.StoppingDistance", &data->rev_stopping_dist);
		ok = ok && getInt(scope, "rev_settings.RampTime", &data->rev_ramp_time);

		if ( getInt(scope, "fwd_settings.Kp", &data->Kpf_long) ) {
			data->use_Kpidf = 1;
			ok = ok && getInt(scope, "fwd_settings.Ki", &data->Kif_long);
			ok = ok && getInt(scope, "fwd_settings.Kd", &data->Kdf_long);
		}
		if ( getInt(scope, "rev_settings.Kp", &data->Kpr_long) ) {
			data->use_Kpidr = 1;
			ok = ok && getInt(scope, "rev_settings.Ki", &data->Kir_long);
			ok = ok && getInt(scope, "rev_settings.Kd", &data->Kdr_long);
		}


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
		data->current_power = 0.0;
		data->last_poll = 0;
		data->last_position = *data->position;
		data->start_position = data->last_position;

		data->stop_marker = 0;
		data->last_stop_position = 0;
		data->last_set_point = 0;
		data->changing_set_point = 0;
		data->start_time = 0;
		data->fwd_start_delay = 50; // assume 50ms to start
		data->rev_start_delay = 50; // assume 50ms to start
		data->last_prestart_change = 0;
		data->prestart_change_delay = 80;

		data->last_Ep = 0.0;
		data->filtered_Ep = 0.0;
		data->last_filtered_Ep = 0.0;
		data->total_err = 0.0;
		data->ramp_start_time = 0;
		data->last_sample_time = 0;
		data->ramp_down_start = 0;

		printf("%s plugin initialised ok: update time %ld\n", data->conveyor_name, *data->min_update_time);
	}

	return PLUGIN_COMPLETED;
}

static void atposition(struct PIDData*data, void *scope) {
	data->last_Ep = 0;
	data->total_err = 0;
}

static void stop(struct PIDData*data, void *scope) {
	if (data->debug && *data->debug) printf("%s stop command\n", data->conveyor_name);
	data->state = cs_stopped;
	data->current_power = 0.0;
	*data->set_point = 0;
	data->stop_marker = 0;
	data->last_set_point = 0;
	data->changing_set_point = 0;
	data->last_stop_position = 0;
	data->last_Ep = 0;
	data->total_err = 0;
	data->ramp_down_start = 0;
	setIntValue(scope, "SetPoint", 0);
	if (!data->state == cs_interlocked) {
		if (data->debug && *data->debug)
			printf("%s stopped\n", data->conveyor_name);
		data->state = cs_stopped;
	}
	setIntValue(scope, "driver.VALUE", output_scaled(data, 0) );
}

static int sign(double val) { return (0 < val) - (val < 0); }

PLUGIN_EXPORT
int poll_actions(void *scope) {
	struct PIDData *data = (struct PIDData*)getInstanceData(scope);
	struct timeval now;
	uint64_t now_t = 0;
	double new_power = 0.0f;
	char *current = 0;

	if (!data) return PLUGIN_COMPLETED; /* not initialised yet; nothing to do */

	gettimeofday(&now, 0);
	now_t = now.tv_sec * 1000000 + now.tv_usec;
	uint64_t delta_t = now_t - data->last_poll;

	if (now_t - data->last_sample_time >= 10000) {
/*		if (data->debug && *data->debug)
			addSampleDebug(data->samples, now_t, *data->position); 
		else
*/
			addSample(data->samples, now_t, *data->position); 
		data->last_sample_time = now_t;
	}

	if ( delta_t/1000 < *data->min_update_time) return PLUGIN_COMPLETED;

	//long speed = ((double)(*data->position - data->last_position) * 1000000) / delta_t;
	long speed = 0;
/*	if (data->debug && *data->debug)
		speed = 1000000 * rateDebug(data->samples); 
	else
*/		speed = 1000000 * rate(data->samples); 
	setIntValue(scope, "Velocity", speed);
	setIntValue(scope, "Position", *data->position);
	/*if (data->debug && *data->debug) printf("%s estimated speed: %ld\n", data->conveyor_name, speed);*/

	data->Kp = (double) *data->Kp_long / 1000000.0f;
	data->Ki = (double) *data->Ki_long / 1000000.0f;
	data->Kd = (double) *data->Kd_long / 1000000.0f;
	if (data->use_Kpidf) {
		data->Kpf = (double) *data->Kpf_long / 1000000.0f;
		data->Kif = (double) *data->Kif_long / 1000000.0f;
		data->Kdf = (double) *data->Kdf_long / 1000000.0f;
	}
	if (data->use_Kpidr) {
		data->Kpr = (double) *data->Kpr_long / 1000000.0f;
		data->Kir = (double) *data->Kir_long / 1000000.0f;
		data->Kdr = (double) *data->Kdr_long / 1000000.0f;
	}

	new_power = data->current_power;
	enum State new_state = data->state;

	long next_position = 0;
	double set_point = *data->set_point;

	long ramp_time = *data->fwd_ramp_time;
	if (set_point < 0  || 
				(set_point > 0 
						&& (new_state == cs_position || new_state == cs_atposition ) 
						&& *data->stop_position < *data->position) ) {
		ramp_time = *data->rev_ramp_time;
	}
	
	/* check if we need to ramp to a new set_point when we are already moving */
	if ( fabs(*data->estimated_speed) > 10 ) {

		if (!data->changing_set_point) {
			if ( *data->set_point != data->last_set_point) {
				data->changing_set_point = now_t;
			}
		}
		else {
			if ( now_t - data->changing_set_point > ramp_time * 1000 ) {
				// finished ramping
				data->changing_set_point = 0;
				data->last_set_point = *data->set_point;
			}
			else {
				// calculate the ramped set point
				set_point = data->last_set_point;
				set_point += (double)( now_t - data->changing_set_point ) 
									/ ramp_time / 1000 
									* (*data->set_point - data->last_set_point);
				if (data->debug && *data->debug)
					printf("%s ramping to new set point: %ld, now using: %5.2f\n", data->conveyor_name, *data->set_point, set_point);
			}
		}
	}
	double dt = (double)(delta_t)/1000000.0; /* 2ms allowance for latency */

	long fwd_power_offset = *data->fwd_start_power;
	long rev_power_offset = *data->rev_start_power;

	/* Determine what the controller should be doing */
	{
		/* detect the current state of the clockwork machine */
		current = getState(scope);

		if (strcmp(current, "interlocked") == 0) new_state = cs_interlocked;
		else if (strcmp(current, "restore") == 0) new_state = cs_interlocked;
		else if (strcmp(current, "stopped") == 0) new_state = cs_stopped;
		else if (strcmp(current, "speed") == 0) new_state = cs_speed;
		else if (strcmp(current, "seeking") == 0) new_state = cs_position;
		else if (strcmp(current, "atposition") == 0) new_state = cs_atposition;

		/* add the correct sign to the set point */
		if (set_point != 0 && (new_state == cs_position || new_state == cs_atposition ) 
				&& sign(set_point) != sign(*data->stop_position - *data->position)) {
			set_point = -set_point;
		}

		/* if debugging, display what we have found */
		if (data->debug && *data->debug && (new_state == cs_position || new_state == cs_speed) )
			printf("%ld\t %s test: %s %ld, stop: %ld, pow: %5.3f, pos: %ld\n", (long)delta_t, data->conveyor_name,
							 (current)? current : "null", 
                *data->set_point,
                *data->stop_position,
                data->current_power,
                *data->position);
		
		if (*data->position == 0) {
			data->last_position = 0; /* startup compensation before the conveyor starts to move*/
			data->start_position = 0;
		}
		else if (data->last_position == 0) {
			data->last_position = *data->position;
			data->start_position = data->last_position;
		}
		if ( abs(*data->position - data->last_position) > 10000 ) { /* protection against wraparound */
			data->last_position = *data->position;
			return PLUGIN_COMPLETED;
		}

		/* if interlocked,  stop if necessary and return */
		if (new_state == cs_interlocked ) {
			if (data->state != cs_interlocked)  {
				stop(data, scope);
				data->state = cs_interlocked;
			}
			goto done_polling_actions;
		}

		/* similarly, if interlocked stop if necessary and return */
		if (new_state == cs_stopped) {
			if (data->state != cs_stopped) {
				stop(data, scope);
			}
			data->state = cs_stopped;
			goto done_polling_actions;
		}

		long dist_to_stop = 0;

		/* has the stop position changed while we are at position or seeking? */
		if (new_state == cs_position || new_state == cs_atposition) {
			dist_to_stop = *data->stop_position - *data->position;
			if ( data->last_stop_position != *data->stop_position ) {
					if (data->debug) printf("%s new stop position %ld\n", data->conveyor_name, *data->stop_position);
					data->last_stop_position = *data->stop_position;
					data->stop_marker = *data->mark_position;
			}
		}

		/* if we are at position check for drift and if necessary seek again */
		if (new_state == cs_atposition) {
			if ( ( *data->position < *data->stop_position 
						&& *data->stop_position - *data->position > *data->fwd_tolerance )
				|| ( *data->position > *data->stop_position 
						&& *data->position - *data->stop_position > *data->rev_tolerance ) )
			changeState(scope, "seeking");
    	return PLUGIN_COMPLETED;
		}

		if ( (data->state == cs_stopped || data->state == cs_atposition)  && (new_state == cs_position || new_state == cs_speed)) {
			data->state = cs_prestart;
		}

		/* even in prestart, we need to check whether we are at position and stop if necessary */
		int close_to_target = 0;
		if (new_state == cs_position) {
			/* once we are very close we no longer calculate a moving target */
			if ( dist_to_stop >= 0 && dist_to_stop < *data->fwd_tolerance) {
					if (data->debug && *data->debug) printf("%s within tolerance (fwd) speed = %ld pos:%ld stop:%ld\n", 
						data->conveyor_name, *data->estimated_speed, *data->position, *data->stop_position);
					close_to_target = 1;
					if ( fabs(*data->estimated_speed) < 50) {
						if (data->debug && *data->debug) printf("%s at position (fwd)\n", data->conveyor_name);
						data->state = cs_atposition;
						data->ramp_down_start = 0;
						changeState(scope, "atposition");
						setIntValue(scope, "StopError", *data->stop_position - *data->position);
						new_power = 0;
						goto calculated_power;
					}
			}
			else if ( dist_to_stop <= 0 && -dist_to_stop <= *data->rev_tolerance) {
					if (data->debug) printf("%s within tolerance (rev) speed = %ld pos:%ld stop:%ld\n", 
						data->conveyor_name, *data->estimated_speed, *data->position, *data->stop_position);
					close_to_target = 1;
					if (fabs(*data->estimated_speed) < 50) {
						if (data->debug && *data->debug) printf("%s at position (rev)\n", data->conveyor_name);
						data->state = cs_atposition;
						data->ramp_down_start = 0;
						changeState(scope, "atposition");
						setIntValue(scope, "StopError", *data->stop_position - *data->position);
						new_power = 0;
						goto calculated_power;
					}
			}
		}


		/* when prestarting, we wait for the conveyor to show some movement and keep a record of when it happens.
			If no movement occurs after a time we gradually increasing power until that happens */
		if (data->state == cs_prestart) {
			fwd_power_offset = 0; // manually adjust these offsets by changes to new_power
			rev_power_offset = 0;

			/* have we found forward movement during prestart? */
			if (set_point > 0 && *data->position - data->last_position > 4) { // fwd motion
				if (new_state == cs_position || new_state == cs_speed) data->state = new_state;
				data->ramp_start_time = now_t;
				data->last_position = *data->position;
				if (data->current_power>0) {
					double mean_offset = bufferAverage(data->fwd_power_offsets);
					if (*data->position - data->last_position > 10 && data->current_power > 0.5 * mean_offset) // overpowered
						addSample(data->fwd_power_offsets, now_t, 0.5 * mean_offset);
					else
						addSample(data->fwd_power_offsets, now_t, data->current_power);
					long new_start_power = bufferAverage(data->rev_power_offsets);
					if (new_start_power <= 0) new_start_power = 1;
					setIntValue(scope, "MinForward", new_start_power);

					char *tmpstr = doubleArrayToString(length(data->fwd_power_offsets), data->fwd_power_offsets->values);
					setStringValue(scope,"ForwardPowerOffsets", tmpstr);
					free(tmpstr);
					data->fwd_start_delay = now_t - data->start_time;
				}
				fwd_power_offset = *data->fwd_start_power;
				if (data->debug && *data->debug)
					printf("%s fwd movement started. time: %ld power: %ld\n", 
						data->conveyor_name, data->fwd_start_delay, *data->fwd_start_power);
			}

			/* have we found reverse movement during prestart */
			if (set_point < 0 && *data->position - data->last_position < -4) { // reverse motion
				if (new_state == cs_position || new_state == cs_speed) data->state = new_state;
				data->ramp_start_time = now_t;
				data->last_position = *data->position;
				if (data->current_power < 0) {
					double mean_offset = bufferAverage(data->rev_power_offsets);
					if (*data->position - data->last_position < -10 && data->current_power < 0.5 * mean_offset) // overpowered
						addSample(data->rev_power_offsets, now_t, 0.5 * mean_offset);
					else
						addSample(data->rev_power_offsets, now_t, bufferAverage(data->rev_power_offsets));
					long new_start_power = bufferAverage(data->rev_power_offsets);
					if (new_start_power >= 0) new_start_power = -1;
					setIntValue(scope, "MinReverse", new_start_power);
					
					char *tmpstr = doubleArrayToString(length(data->rev_power_offsets), data->rev_power_offsets->values);
					setStringValue(scope,"ReversePowerOffsets", tmpstr);
					free(tmpstr);

					data->rev_start_delay = now_t - data->start_time;
				}
				rev_power_offset = *data->rev_start_power;
				if (data->debug && *data->debug)
					printf("%s rev movement started. time: %ld power: %ld\n", 
						data->conveyor_name, data->rev_start_delay, *data->rev_start_power);
			}
			else {
				/* waiting for motion during prestart */
				if (data->current_power == 0) {
					data->start_time = now_t;
					new_power = (set_point>0) ? *data->fwd_start_power : *data->rev_start_power;
					data->last_prestart_change = now_t;
				}
				else if (data->current_power > 0) {
					if (now_t - data->start_time >= data->fwd_start_delay
							&& now_t - data->last_prestart_change >= data->prestart_change_delay )  {
						new_power = data->current_power + 20;
						data->last_prestart_change = now_t;
					}
				}
				else if (data->current_power < 0) {
					if (now_t - data->start_time >= data->rev_start_delay
							&& now_t - data->last_prestart_change >= data->prestart_change_delay )  {
						new_power = data->current_power - 20;
						data->last_prestart_change = now_t;
					}
				}
				printf("%s waiting for movement. time: %ld power: %5.2f\n", 
						data->conveyor_name, now_t - data->start_time, new_power);
				goto calculated_power;
			}
		}

		if (new_state == cs_speed && data->state != cs_speed) {
			data->state = cs_speed;
		}

		/* if we got here, we must be moving and we may be ramping up or down, controlling to a speed or 
				seeking a position */

		double ramp_down_ratio = 0.0;
		double ramp_up_ratio = 1.0;
		long startup_time = (now_t - data->ramp_start_time + 500)/1000;

		if (new_state == cs_position || new_state == cs_speed && startup_time < *data->startup_time) {
				ramp_up_ratio = (double)startup_time / *data->startup_time;
				if (ramp_up_ratio < 0.1) ramp_up_ratio = 0.1;
				if (data->debug && *data->debug && ramp_up_ratio >0.0 && ramp_up_ratio<=1.0) 
					printf("%s rampup ratio: %5.3f\n", data->conveyor_name, ramp_up_ratio);
		}

		if (new_state == cs_position) {
			/* adjust the set point to cater for whether we are close to the stop position */
			double stopping_time = (double)*data->stopping_time / 1000.0; /* secs */
			
			/* stopping distance at vel v with an even ramp is s=vt/2
			   we add 300ms to the time to cater for the delay before the system responds
			   to changes of input */
			double ramp_dist = fabs(*data->estimated_speed * stopping_time / 2.0);
			if ( data->ramp_down_start == 0 && fabs(*data->stop_position - *data->position) < ramp_dist) {
				data->ramp_down_start = now_t;
			}
			if (stopping_time > 0 && data->ramp_down_start) {
				ramp_down_ratio = (data->ramp_down_start + stopping_time - now_t) / stopping_time;
				if (ramp_down_ratio < 0.1) ramp_down_ratio = 0.1;
				double scale = log(fabs(ramp_down_ratio/0.6)+0.3)/2 + 0.6; 
				if (scale < 0.1) scale = 0.1;
				if (scale > 1.0) scale = 1.0;
				set_point *= scale;
			}
			if (set_point != 0 && sign(set_point) != sign(dist_to_stop)) {
				set_point = -set_point;
				if (data->debug && *data->debug) printf("%s fixing direction: %5.3f\n", data->conveyor_name, set_point);
			}
			if (data->debug && *data->debug) printf("%s approaching %s, ratio = %5.2f (%ld) set_point: %5.2f\n", data->conveyor_name, 
					( sign(dist_to_stop) >= 0) ? "fwd" : "rev", ramp_down_ratio, dist_to_stop, set_point);
		}
		// if no ramp down was applied but we are within the ramp up time, apply the ramp up
		if ( (ramp_down_ratio <= 0.0 || fabs(ramp_down_ratio) >= 1.0) && ramp_up_ratio <1.0)
			set_point *= ramp_up_ratio;

	
		if (data->state == cs_speed) {
			next_position = data->last_position + set_point;
		}
		else if (data->state == cs_position) {
			if (fabs(dist_to_stop) > fabs(set_point))
				next_position = data->last_position + set_point;
			else 
				next_position = *data->stop_position;

#if 0
			if ( fabs(next_position - *data->stop_position) > fabs(dist_to_stop) ) {
				if (data->debug && *data->debug) printf("%s calculation yields worse position (%ld)", data->conveyor_name, next_position);
				next_position = *data->stop_position;
			}
#endif
		}
		else if (data->state == cs_atposition)
			next_position = *data->stop_position;
		else {
			next_position = *data->position;
		}

		double Ep = next_position - *data->position;
		
/*
		long changed_pos = *data->position - data->start_position;
		if (changed_pos != *data->position_change)
			setIntValue(scope, "position", changed_pos);
*/


		if (data->state == cs_speed || data->state == cs_position) {
			data->total_err += (data->last_Ep + Ep)/2 * dt;

			// cap the total error to limit integrator windup effects
			if (data->total_err > 2000) data->total_err = 2000;
			else if (data->total_err < -2000) data->total_err = -2000;

			/* add filtereing on the error term changes to limit the effect of noise */
			data->last_filtered_Ep = data->filtered_Ep;
			data->filtered_Ep = 0.8 * Ep + 0.2 * data->last_Ep;
			double de = data->filtered_Ep - data->last_filtered_Ep; /*set_point - speed; (Ep - data->last_Ep); */
			data->last_Ep = Ep;

			double Dout = 0.0;
			if ( next_position > *data->position && data->use_Kpidf) // forward
				Dout = (int) (data->Kpf * Ep + data->Kif * data->total_err + data->Kdf * de);
			else if (next_position < *data->position && data->use_Kpidr) // reverse
				Dout = (int) (data->Kpr * Ep + data->Kir * data->total_err + data->Kdr * de);
			else  // at target position (at speed)
				Dout = (int) (data->Kp * Ep + data->Ki * data->total_err + data->Kd * de);

			if (data->debug && *data->debug && fabs(Ep)>5) 
					printf("%s Set: %5.3f Ep: %5.3f Ierr: %5.3f de/dt: %5.3f\n", data->conveyor_name, 
						set_point, Ep, data->total_err, de/dt );
			
			new_power = Dout;
		}

		if (data->debug && *data->debug)
			printf ("%s pos: %ld, Ep: %5.3f, tot_e %5.3f, dt: %5.3f, "
					"pwr: %5.3f, spd: %5.3f, setpt: %5.3f, next: %ld\n", 
			data->conveyor_name,
			*data->position, Ep, data->total_err, dt, 
			data->current_power,
			(double)*data->estimated_speed,
			set_point, next_position);
	}

calculated_power:
	if (new_power != data->current_power) {
		if (new_power == 0.0) {}
		else if ( new_power > 0 && new_power > data->current_power + *data->ramp_limit) {
			printf("warning: %s wanted change of %f6.2, limited to %ld\n", data->conveyor_name, new_power - data->current_power, *data->ramp_limit);
			new_power = data->current_power + *data->ramp_limit;
		}
		else if ( new_power < 0 && new_power < data->current_power - *data->ramp_limit) {
			printf("warning: %s wanted change of %f6.2, limited to %ld\n", data->conveyor_name, new_power - data->current_power, - *data->ramp_limit);
			new_power = data->current_power - *data->ramp_limit;
		}
		long power = 0;
		if (new_power>0) power = output_scaled(data, (long) new_power + fwd_power_offset);
		else if (new_power<0) power = output_scaled(data, (long) new_power + rev_power_offset);
		else power = output_scaled(data, 0);

		if (data->debug && *data->debug) 
			printf("%s setting power to %ld (scaled: %ld, fwd offset: %ld, rev offset:%ld)\n", data->conveyor_name, (long)new_power, power, fwd_power_offset, rev_power_offset);

		
		setIntValue(scope, "driver.VALUE", power);
		data->current_power = new_power;
	}

done_polling_actions:
{
	/*
	long speed = (double)(*data->position - data->last_position) / dt;
	setIntValue(scope, "Velocity", speed);
	setIntValue(scope, "Position", *data->current_position);
	*/
}
	data->last_position = *data->position;
	data->last_poll = now_t;
	if (current) { free(current); current = 0; }
	data->last_poll = now_t;
	
    return PLUGIN_COMPLETED;
}

%END_PLUGIN
	EXPORT RW 32BIT Velocity, StopError;
	EXPORT RO 16BIT MinForward, MinReverse;
	OPTION SetPoint 0;
	OPTION StopPosition 0;
	OPTION StopMarker 0;
	OPTION DEBUG 0;
	OPTION Velocity 0;            # estimated current velocity 
	OPTION StopError 0;
	OPTION Position 0;
	OPTION MinForward 1;
	OPTION MinReverse -1;
	OPTION ForwardPowerOffsets "";
	OPTION ReversePowerOffsets "";
	
	# This module uses the restore state to give the machine 
	# somewhere to go once the driver is no longer interlocked. 
	# Since we are not using stable states for stopped, seeking etc we 
	# the machine would otherwise never leave interlocked.
	interlocked WHEN driver IS interlocked;
	restore WHEN SELF IS interlocked; # restore state to what was happening before the interlock
	stopped INITIAL;
	seeking STATE;
	speed STATE;
	atposition STATE;

	ENTER interlocked {
		SET M_Control TO Unavailable;
	}
	ENTER stopped { 
		SET M_Control TO Ready;
		SetPoint := 0; StopPosition := 0; StopMarker := 0; }
	ENTER seeking { 
		SET M_Control TO Resetting;
	}
	ENTER speed { 
		SET M_Control TO Working;
	}
	ENTER atposition { 
		SET M_Control TO Ready;
	}
	ENTER restore { SET SELF TO stopped; } 

	COMMAND stop { SET SELF TO stopped; }

	COMMAND MarkPos {
		StopMarker := pos.VALUE;
		IF (SetPoint == 0) {
			StopPosition := StopMarker;
		};
		IF (SetPoint > 0) {
			StopPosition := StopMarker + fwd_settings.StoppingDistance;
		};
		IF (SetPoint < 0) {
			StopPosition := StopMarker - rev_settings.StoppingDistance;
		};
		SET SELF TO seeking;
	}

	# convenience commands
	COMMAND slow {
	   SetPoint := fwd_settings.SlowSpeed; 
		SET SELF TO speed;
	}
	
	COMMAND start { 
    SetPoint := fwd_settings.FullSpeed;
		SET SELF TO speed;
  }
	
	COMMAND slowrev { 
	  SetPoint := rev_settings.SlowSpeed;
		SET SELF TO speed;
	}

	COMMAND startrev { 
	  SetPoint := rev_settings.FullSpeed;
		SET SELF TO speed;
	}

	COMMAND clear {
		StopPosition := pos.VALUE;
		SET SELF TO seeking;
	}
}
