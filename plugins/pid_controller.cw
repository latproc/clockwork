PIDCONFIGURATION MACHINE {
	OPTION PERSISTENT true;
	EXPORT RW 32BIT min_update_time, StartTimeout, stopping_time, Kp, Ki, Kd;

	OPTION min_update_time 20; # minimum time between normal control updates
	OPTION StartTimeout 500;	 # conveyor start timeout
	OPTION inverted false;     # do not invert power
	OPTION stopping_time 300;  # 300ms stopping time

	OPTION Kp 2000000;
	OPTION Ki 800000;
	OPTION Kd 40000;
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
#include <buffering.c>

enum State { 
		cs_init,
		cs_interlocked, /* something upstream is interlocked */
		cs_stopped, 		/* sleeping */
		cs_position,
		cs_speed,
		cs_atposition
};

struct PIDData {
	enum State state;
	struct CircularBuffer *samples;

	/**** clockwork interface ***/
    long *set_point;
    long *stop_position;
    long *mark_position;
	long *position;

	/* ramp settings */
	long *min_update_time;
	long *stopping_time;

	long *max_forward;
	long *max_reverse;
	long *zero_pos;
	long *min_forward;
	long *min_reverse;

	long *fwd_tolerance;
//	long *fwd_stopping_distance;
//	long *fwd_slow_speed;
//	long *fwd_full_speed;
//	long *fwd_power_factor;
//	long *fwd_travel_allowance;

	long *rev_tolerance;
//	long *rev_stopping_distance;
//	long *rev_slow_speed;
//	long *rev_full_speed;
//	long *rev_power_factor;
//	long *rev_travel_allowance;

	long *Kp_long;
	long *Ki_long;
	long *Kd_long;

	/* statistics/debug */
	long *stop_error;
	long *estimated_speed;
	long *current_position;

	/* internal values for ramping */
	double current_power;
	uint64_t ramp_start_time;
	long ramp_start_power;		/* the power level at the point ramping started */
	long ramp_start_target; 	/* the target that was set when ramping started */
	uint64_t last_poll;
	long last_position;
	long tolerance;
	long last_set_point;
	long last_stop_position;
	long default_debug; /* a default debug level if one is not specified in the script */
	int inverted;
	long start_position;
	double last_Ep;

	char *conveyor_name;

	/* stop/start control */
	long stop_marker;
	long *debug;

	double Kp;
	double Ki;
	double Kd;
	
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
	struct PIDData *data = (struct PIDData*)getInstanceData(scope);
	if (!data) {
		// one-time initialisation
		data = (struct PIDData*)malloc(sizeof(struct PIDData));
		memset(data, 0, sizeof(struct PIDData));
		setInstanceData(scope, data);
		{ 
			data->conveyor_name = getStringValue(scope, "NAME");
			if (!data->conveyor_name) data->conveyor_name = strdup("UNKNOWN CONVEYOR");
		}

		data->samples = createBuffer(8);
		ok = ok && getInt(scope, "SetPoint", &data->set_point);
		ok = ok && getInt(scope, "StopMarker", &data->mark_position);
		ok = ok && getInt(scope, "StopPosition", &data->stop_position);
		ok = ok && getInt(scope, "pos.VALUE", &data->position);
		ok = ok && getInt(scope, "settings.min_update_time", &data->min_update_time);
		ok = ok && getInt(scope, "settings.stopping_time", &data->stopping_time);
		ok = ok && getInt(scope, "settings.Kp", &data->Kp_long);
		ok = ok && getInt(scope, "settings.Ki", &data->Ki_long);
		ok = ok && getInt(scope, "settings.Kd", &data->Kd_long);
		ok = ok && getInt(scope, "Velocity", &data->estimated_speed);
		ok = ok && getInt(scope, "Position", &data->current_position);
		ok = ok && getInt(scope, "StopError", &data->stop_error);

		ok = ok && getInt(scope, "output_settings.MaxForward", &data->max_forward);
		ok = ok && getInt(scope, "output_settings.MaxReverse", &data->max_reverse);
		ok = ok && getInt(scope, "output_settings.ZeroPos", &data->zero_pos);
		ok = ok && getInt(scope, "output_settings.MinForward", &data->min_forward);
		ok = ok && getInt(scope, "output_settings.MinReverse", &data->min_reverse);

		ok = ok && getInt(scope, "fwd_settings.tolerance", &data->fwd_tolerance);
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
		data->current_power = 0.0;
		data->last_poll = 0;
		data->last_position = *data->position;
		data->start_position = data->last_position;

		data->stop_marker = 0;
		data->last_stop_position = 0;
		data->last_set_point = 0;

		data->total_err = 0.0;

		printf("%s plugin initialised ok: update time %ld\n", data->conveyor_name, *data->min_update_time);
	}
	data->Kp = (double) *data->Kp_long / 1000000.0f;
	data->Ki = (double) *data->Ki_long / 1000000.0f;
	data->Kd = (double) *data->Kd_long / 1000000.0f;

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
	data->last_stop_position = 0;
	data->last_Ep = 0;
	data->total_err = 0;
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
	long calculated_set_point = 0;
	long calculated_stop_position = 0;
	char *current = 0;

	if (!data) return PLUGIN_COMPLETED; /* not initialised yet; nothing to do */

	gettimeofday(&now, 0);
	now_t = now.tv_sec * 1000000 + now.tv_usec;
	uint64_t delta_t = now_t - data->last_poll;

	if ( delta_t/1000 < *data->min_update_time) return PLUGIN_COMPLETED;

	new_power = data->current_power;
	enum State new_state = data->state;

	long next_position = 0;
	double set_point = *data->set_point;
	double dt = (double)(delta_t)/1000000.0; // 2ms allowance for latency

	/* Determine what the controller should be doing */
	{

		current = getState(scope);

		if (strcmp(current, "interlocked") == 0) new_state = cs_interlocked;
		else if (strcmp(current, "restore") == 0) new_state = cs_interlocked;
		else if (strcmp(current, "stopped") == 0) new_state = cs_stopped;
		else if (strcmp(current, "speed") == 0) new_state = cs_speed;
		else if (strcmp(current, "seeking") == 0) new_state = cs_position;
		else if (strcmp(current, "atposition") == 0) new_state = cs_atposition;

		if (set_point != 0 && (new_state == cs_position || new_state == cs_atposition ) 
				&& sign(set_point) != sign(*data->stop_position - *data->position)) {
			set_point = -set_point;
		}

		if (data->debug && *data->debug)
			printf("%ld\t %s test: %s %ld, stop: %ld, pow: %5.3f, pos: %ld\n", (long)delta_t, data->conveyor_name,
							 (current)? current : "null", 
                *data->set_point,
                *data->stop_position,
                data->current_power,
                *data->position);
		
		if (*data->position == 0) {
			data->last_position = 0; // startup compensation before the conveyor starts to move
			data->start_position = 0;
		}
		else if (data->last_position == 0) {
			data->last_position = *data->position;
			data->start_position = data->last_position;
		}
		if ( abs(*data->position - data->last_position) > 10000 ) { // protection against wraparound
			data->last_position = *data->position;
			return PLUGIN_COMPLETED;
		}

		if (new_state == cs_interlocked ) {
			if (data->state != cs_interlocked)  {
				stop(data, scope);
				data->state = cs_interlocked;
			}
			goto done_polling_actions;
		}

		if (new_state == cs_stopped) {
			if (data->state != cs_stopped) {
				stop(data, scope);
			}
			data->state = cs_stopped;
			goto done_polling_actions;
		}

		if (new_state == cs_atposition) {
			if ( ( *data->position < *data->stop_position 
						&& *data->stop_position - *data->position > *data->fwd_tolerance )
				|| ( *data->position > *data->stop_position 
						&& *data->position - *data->stop_position > *data->rev_tolerance ) )
			changeState(scope, "seeking");
			goto done_polling_actions;
		}

		int approaching = new_state == cs_position 
												&& sign(set_point) == sign(*data->stop_position - *data->position);
		int overshot = new_state == cs_position && !approaching;

		if (data->state == cs_stopped && (new_state == cs_position || new_state == cs_speed)) 
			data->start_position = *data->position;

		// adjust the set point to cater for whether we are close to the stop position
		if (new_state == cs_position) {
			double stopping_time = (double)*data->stopping_time / 1000.0; // secs
			if (!approaching) set_point = 0.0;
			else {
				// stopping distance at vel v with an even ramp is s=vt/2
				double dist = set_point * stopping_time / 2.0;
				double ratio = 0;
				if (dist != 0) ratio = (*data->stop_position - *data->position) / dist;
				if ( fabs(ratio) <= 1.0 ) // time to ramp down
					set_point *= fabs(ratio); // using an extra scaling factor here.. TBD
			}
		}

		next_position = data->last_position + dt * set_point;

		if (new_state == cs_position || new_state == cs_atposition) {
			if (data->state != cs_position || (data->last_stop_position != *data->stop_position) ) {
					data->state = cs_position;
					printf("%s new stop position %ld\n", data->conveyor_name, *data->stop_position);
					data->last_stop_position = *data->stop_position;
					data->stop_marker = *data->mark_position;
					// we may need to change direction for this movement
					if (sign(set_point) != sign(*data->stop_position - *data->position))
						setIntValue(scope, "SetPoint", *data->set_point);
			}
		}
		if (new_state == cs_position) {
			// once we are very close we no longer calculate a moving target
			if ( set_point >= 0 && *data->position >= *data->stop_position - *data->fwd_tolerance/2 
							&& *data->position <= *data->stop_position + *data->fwd_tolerance) {
					atposition(data, scope);
					new_power = 0;
					if ( fabs(*data->estimated_speed) < 50) {
						changeState(scope, "atposition");
						setIntValue(scope, "StopError", *data->stop_position - *data->position);
					}
					goto calculated_power;
			}
			else if ( set_point <= 0 &&  *data->position >= *data->stop_position + *data->rev_tolerance/2 
							&&  *data->position <= *data->stop_position - *data->rev_tolerance) {
					atposition(data, scope);
					new_power = 0;
					if (fabs(*data->estimated_speed) < 50) {
						changeState(scope, "atposition");
						setIntValue(scope, "StopError", *data->stop_position - *data->position);
					}
					goto calculated_power;
			}
		}

		if (new_state == cs_speed && data->state != cs_speed) {
			data->state = cs_speed;
		}
		if (data->state == cs_position || data->state == cs_speed)
			next_position += data->last_Ep;
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

		if (data->debug && *data->debug)
			printf ("%s pos: %ld, Ep: %5.3f, tot_e %5.3f, dt: %5.3f, "
					"pwr: %5.3f, spd: %5.3f, setpt: %5.3f, next: %ld\n", 
			data->conveyor_name,
			*data->position, Ep, data->total_err, dt, 
			data->current_power,
			(double)(*data->position - data->last_position) / dt,
			set_point, next_position);

		if (data->state == cs_speed || data->state == cs_position) {
			double de = Ep - data->last_Ep;
			data->total_err += (data->last_Ep + Ep)/2 * dt;
			data->last_Ep = Ep;

			double Dout = (int) (data->Kp * Ep + data->Ki * data->total_err + data->Kd * de / dt);
			if (data->debug && *data->debug && fabs(Ep)>5) 
					printf("%s Set: %5.3f Ep: %5.3f Ierr: %5.3f de/dt: %5.3f\n", data->conveyor_name, 
						set_point, Ep, data->total_err, de/dt );
			
			new_power = Dout;
		}

	}

calculated_power:
	if (new_power != data->current_power) {
		if (new_power == 0.0) {}
		else if ( new_power > data->current_power + 2000) new_power = data->current_power + 1000;
		else if (new_power < data->current_power - 2000) new_power = data->current_power - 1000;
		long power = output_scaled(data, (long) new_power);

		if (data->debug && *data->debug) 
			printf("%s setting power to %ld (scaled: %ld)\n", 
			data->conveyor_name, (long)new_power, power);

		
		setIntValue(scope, "driver.VALUE", power);
		data->current_power = new_power;
	}

done_polling_actions:
{
	addSample(data->samples, *data->position, now_t);
	long speed = rate(data->samples);
	//long speed = (double)(*data->position - data->last_position) / dt;
	setIntValue(scope, "Velocity", speed);
	setIntValue(scope, "Position", *data->current_position);
}
	data->last_position = *data->position;
	data->last_poll = now_t;
	if (current) { free(current); current = 0; }
	data->last_poll = now_t;
	
    return PLUGIN_COMPLETED;
}

%END_PLUGIN
	EXPORT RW 32BIT Velocity, StopError;
	OPTION SetPoint 0;
	OPTION StopPosition 0;
	OPTION StopMarker 0;
	OPTION DEBUG 0;
	OPTION Velocity 0;            # estimated current velocity 
	OPTION StopError 0;
	OPTION Position 0;
	
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
