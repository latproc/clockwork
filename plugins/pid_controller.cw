PIDDIRECTIONCONFIGURATION MACHINE {
	OPTION PERSISTENT true;
	EXPORT RW 32BIT SlowSpeed, FullSpeed, StoppingTime, StoppingDistance, Tolerance, Kp, Ki, Kd;
	EXPORT RO 16BIT PowerOffset;
	OPTION SlowSpeed 40;
	OPTION FullSpeed 1000;

# settings that are specific to forward/reverse directions and are not provided in PIDCONFIGURATION
	OPTION Tolerance 100;          # stopping position tolerance
	OPTION RampTime 500;
	OPTION StartupPowerOffsets "0"; # a list of recent power offsets, averaged to provide PowerOffset
	OPTION PowerOffset 0;	# the current power offset used when starting movement in this direction
	OPTION StoppingDistance 3000;  # distance from the stopping point to begin slowing down

# settings that override values given in PIDCONFIGURATION 
	OPTION StoppingTime 300;  # 300ms stopping time
	OPTION Kp 0;
	OPTION Ki 0;
	OPTION Kd 0;
}


PIDCONFIGURATION MACHINE {
	OPTION PERSISTENT true;
	EXPORT RW 32BIT MinUpdateTime, StartupTime, StoppingTime, Kp, Ki, Kd;

	OPTION MinUpdateTime 20; # minimum time between normal control updates
	OPTION StartupTime 500;   # 500ms startup ramp time used for both forward and reverse cannot be overriden in PIDSPEEDCONFIGURATION
	OPTION StoppingTime 300;  # 300ms stopping time used if the forward/reverse values are not given
	OPTION Inverted false;     # do not invert power
	OPTION RampLimit 1000;		# maximum change in control power per cycle

	OPTION Kp 1000000;
	OPTION Ki 0;
	OPTION Kd 0;
}

PIDSTATISTICS MACHINE {
	OPTION Vel 0;
	OPTION SetPt 0;
	OPTION Pwr 0;
	OPTION Err_p 0;
	OPTION Err_i 0;
	OPTION Err_d 0;

	Idle INITIAL;
	Busy STATE;
	ENTER Busy { Err_p := 0; Err_i := 0; Err_d := 0; }
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
#include <unistd.h>

enum State {
	cs_init,
	cs_interlocked, /* something upstream is interlocked */
	cs_stopped, 		/* sleeping */
	cs_prestart,		/* waiting for pressure to build up */
	cs_position,
	cs_speed,
	cs_stopping,
	cs_atposition
};

enum InternalState {
	is_init,        /* program start */
	is_stopped,     /* not moving, not holding to current position */
	is_prestart_init,    /* initialisation of the prestart phase */
	is_prestart,    /* trying to find a power level that causes movement */
	is_ramping_up,  /* ramping to initial set point */
	is_speed,       /* pid control at a set point */
	is_changing,    /* changing to a new set point */
	is_ramping_down,/* ramping down to a stop at a position */
	is_stopping,    /* creeping to a set position */
	is_holding,     /* monitoring a position and driving back/forth as necessary */
};

struct PIDData {
	enum State state;
#ifdef USE_MEASURED_PERIOD
	struct CircularBuffer *psamples;
	long *grab_period;
#endif
	struct CircularBuffer *samples;
	struct CircularBuffer *fwd_power_offsets;
	struct CircularBuffer *rev_power_offsets;
	struct CircularBuffer *power_rates;
	struct CircularBuffer *power_records;
	
	uint64_t now_t;
	uint64_t delta_t;

	/**** clockwork interface ***/
	const long *set_point;
	const long *stop_position;
	const long *mark_position;
	const long *position;

	/* ramp settings */
	const long *min_update_time;
	const long *stopping_time;
	const long *startup_time;
	const long *ramp_limit;

	const long *max_forward;
	const long *max_reverse;
	const long *zero_pos;
	const long *min_forward;
	const long *min_reverse;

	const long *fwd_tolerance;
	const long *fwd_stopping_dist;
	const long *fwd_ramp_time;
	const long *fwd_stopping_time;
	const long *fwd_startup_time;

	const long *rev_tolerance;
	const long *rev_stopping_dist;
	const long *rev_ramp_time;
	const long *rev_stopping_time;
	const long *rev_startup_time;

	/* statistics/debug */
	const long *stop_error;
	/*const long *estimated_speed; */
	const long *current_position;

	/* internal values for ramping */
	double current_power;
	uint64_t ramp_start_time;
	uint64_t last_poll;
	
	
	long last_position;
	long tolerance;
	long saved_set_point;       /* saved copy of the last user set point */
	long filtered_set_point;    /* a filtered setpoint, with ramps applied */
	long last_set_point;        /* the previous value of the filtered set point */
	uint64_t changing_set_point;
	long last_stop_position;
	long default_debug; /* a default debug level if one is not specified in the script */
	int inverted;
	long start_position;
	double last_Ep;
	double filtered_Ep;
	double last_filtered_Ep;
	uint64_t last_sample_time;
	int overshot;
	int have_stopped;
	long speed; /* current estimated speed */
	int speedcount; /* how many counts have we been at this speed */
	enum InternalState sub_state;

	/* keep a record of the power at which movement started */
	const long *fwd_start_power;
	const long *rev_start_power;
	/* keep a record of how long it took to see movement */
	uint64_t start_time;
	double power_rate;
	uint64_t fwd_start_delay;
	uint64_t rev_start_delay;

	uint64_t last_prestart_change;
	long prestart_change_delay;

	uint64_t ramp_down_start;
	long curr_seek_power;
	double power_scalar;

	char *conveyor_name;

	/* stop/start control */
	long stop_marker;
	const long *debug;

	const long *Kp_long;
	const long *Ki_long;
	const long *Kd_long;

	double Kp;
	double Ki;
	double Kd;

	const long *Kpf_long, *Kif_long, *Kdf_long, *Kpr_long, *Kir_long, *Kdr_long;
	double Kpf, Kif, Kdf, Kpr, Kir, Kdr;
	int use_Kpidf;
	int use_Kpidr;

	double total_err;
	FILE *logfile;
	FILE *datafile;
};

static void reset_stop_test(struct PIDData *data) {
	if (data->debug && *data->debug) printf ("reset stop test\n");
	data->speedcount = 0;
}

static uint64_t microsecs() {
    struct timeval now;
    gettimeofday(&now, 0);
    return now.tv_sec * 1000000L + now.tv_usec;
}

int getInt(void *scope, const char *name, const long **addr) {
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
	return 1;
}

static long long_range_limited(long val, long min, long max) {
    if (val < min) return min;
    if (val > max) return max;
    return val;
}

static double double_range_limited(double val, double min, double max) {
    if (val < min) return min;
    if (val > max) return max;
    return val;
}

static int long_in_range(long val, long min, long max) {
    if (val >= min && val <= max) return 1;
    return 0;
}

static int double_in_range(double val, double min, double max) {
    if (val >= min && val <= max) return 1;
    return 0;
}


static int power_valid(struct PIDData *data, double power) {
    return double_in_range( power, *data->max_reverse - *data->min_reverse, 
        *data->max_forward - *data->min_forward );
}

static int raw_power_valid(struct PIDData *data, long power) {
    return power == *data->zero_pos
            || long_in_range(power, *data->max_reverse, *data->min_reverse)
            || long_in_range(power, *data->min_forward, *data->max_forward);
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

	if (!raw_power_valid(settings, raw)) {
        if (settings->debug && *settings->debug)
            fprintf(settings->logfile, "%s ERROR: raw power %ld was out of range"
            " [%ld,%ld], %ld, [%ld, %ld]\n",
                settings->conveyor_name, raw,
                *settings->max_reverse, *settings->min_reverse,
                *settings->zero_pos, *settings->min_forward, *settings->max_forward );
	    raw = *settings->zero_pos;
	}

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

#ifdef USE_MEASURED_PERIOD
		data->psamples = createBuffer(5);
#endif
		data->samples = createBuffer(8);
		data->fwd_power_offsets = createBuffer(8);
		data->rev_power_offsets = createBuffer(8);
		data->power_rates = createBuffer(8);
		data->power_records = createBuffer(8);
		{
		    int i; for (i=0; i<8; ++i) {
					addSample(data->fwd_power_offsets, i, 0);
					addSample(data->rev_power_offsets, i, 0);
					addSample(data->power_rates, i, 1.0);
				}
		}
#ifdef USE_MEASURED_PERIOD
		ok = ok && getInt(scope, "IA_GrabConveyorPeriod.VALUE", &data->grab_period);
#endif
		ok = ok && getInt(scope, "SetPoint", &data->set_point);
		ok = ok && getInt(scope, "StopMarker", &data->mark_position);
		ok = ok && getInt(scope, "StopPosition", &data->stop_position);
		ok = ok && getInt(scope, "pos.VALUE", &data->position);
		/*ok = ok && getInt(scope, "Velocity", &data->estimated_speed); */
		ok = ok && getInt(scope, "Position", &data->current_position);
		ok = ok && getInt(scope, "StopError", &data->stop_error);

		ok = ok && getInt(scope, "settings.MinUpdateTime", &data->min_update_time);
		ok = ok && getInt(scope, "settings.StoppingTime", &data->stopping_time);
		ok = ok && getInt(scope, "settings.StartupTime", &data->startup_time);
		ok = ok && getInt(scope, "settings.RampLimit", &data->ramp_limit);
		ok = ok && getInt(scope, "settings.Kp", &data->Kp_long);
		ok = ok && getInt(scope, "settings.Ki", &data->Ki_long);
		ok = ok && getInt(scope, "settings.Kd", &data->Kd_long);

		ok = ok && getInt(scope, "output_settings.MaxForward", &data->max_forward);
		ok = ok && getInt(scope, "output_settings.MaxReverse", &data->max_reverse);
		ok = ok && getInt(scope, "output_settings.ZeroPos", &data->zero_pos);
		ok = ok && getInt(scope, "output_settings.MinForward", &data->min_forward);
		ok = ok && getInt(scope, "output_settings.MinReverse", &data->min_reverse);

		ok = ok && getInt(scope, "fwd_settings.Tolerance", &data->fwd_tolerance);
		ok = ok && getInt(scope, "fwd_settings.StoppingDistance", &data->fwd_stopping_dist);
		ok = ok && getInt(scope, "fwd_settings.RampTime", &data->fwd_ramp_time);
		ok = ok && getInt(scope, "fwd_settings.PowerOffset", &data->fwd_start_power);
		ok = ok && getInt(scope, "fwd_settings.StoppingTime", &data->fwd_stopping_time);

		ok = ok && getInt(scope, "rev_settings.Tolerance", &data->rev_tolerance);
		ok = ok && getInt(scope, "rev_settings.StoppingDistance", &data->rev_stopping_dist);
		ok = ok && getInt(scope, "rev_settings.RampTime", &data->rev_ramp_time);
		ok = ok && getInt(scope, "rev_settings.PowerOffset", &data->rev_start_power);
		ok = ok && getInt(scope, "rev_settings.StoppingTime", &data->rev_stopping_time);

		/* collect fwd/rev specific startup times or use default */
		if ( !getInt(scope, "fwd_settings.StartupTime", &data->fwd_startup_time) )
			data->fwd_startup_time = data->startup_time;
		if ( !getInt(scope, "rev_settings.StartupTime", &data->rev_startup_time) )
			data->rev_startup_time = data->startup_time;

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


		if (!getInt(scope, "DEBUG", &data->debug) )
			data->debug = &data->default_debug;

		{
			char *invert = getStringValue(scope, "settings.Inverted");
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

		/* load the persistent list of recent power offsets */
		const char *startup_offset_str = getStringValue(scope, "fwd_settings.StartupPowerOffsets");
		if (startup_offset_str) {
			double offsets[8];
			int n = stringToDoubleArray(startup_offset_str, 8, offsets);
			printf("%s: %d forward startup power offsets found\n", data->conveyor_name, n);
			int i;
			for (i=0; i<n; ++i)
				addSample(data->fwd_power_offsets, i, offsets[i]);
			char *tmpstr = doubleArrayToString(length(data->fwd_power_offsets), data->fwd_power_offsets->values);
			setStringValue(scope,"fwd_settings.StartupPowerOffsets", tmpstr);
			free(tmpstr);
			long new_start_power =long_range_limited( bufferAverage(data->fwd_power_offsets), 1, 2000);
			setIntValue(scope, "fwd_settings.PowerOffset", new_start_power);
		}
		startup_offset_str = getStringValue(scope, "rev_settings.StartupPowerOffsets");
		if (startup_offset_str) {
			double offsets[8];
			int n = stringToDoubleArray(startup_offset_str, 8, offsets);
			printf("%s: %d reverse startup power offsets found\n", data->conveyor_name, n);
			int i;
			for (i=0; i<n; ++i)
				addSample(data->rev_power_offsets, i, offsets[i]);
			char *tmpstr = doubleArrayToString(length(data->rev_power_offsets), data->rev_power_offsets->values);
			setStringValue(scope,"rev_settings.StartupPowerOffsets", tmpstr);
			free(tmpstr);
			long new_start_power = long_range_limited( bufferAverage(data->rev_power_offsets), -2000, -1);
			setIntValue(scope, "rev_settings.PowerOffset", new_start_power);
		}

		data->state = cs_init;
		data->current_power = 0.0;

		data->now_t = 0;
		data->delta_t = 0;
		data->last_poll = 0;
		data->last_position = *data->position;
		data->start_position = data->last_position;
		
		{   /* prime the position samples */
		    int i; for (i=0; i<8; ++i) addSample(data->samples, i, data->start_position);
		    rate(data->samples);
		}
		data->power_rate = bufferAverage(data->power_rates);

		data->stop_marker = 0;
		data->last_stop_position = 0;
		data->last_set_point = 0;
		data->saved_set_point = 0;
		data->changing_set_point = 0;
		data->start_time = 0;
		data->fwd_start_delay = 150000; /* assume 50ms to start */
		data->rev_start_delay = 150000; /* assume 50ms to start */
		data->last_prestart_change = 0;
		data->prestart_change_delay = 80;

		data->last_Ep = 0.0;
		data->filtered_Ep = 0.0;
		data->last_filtered_Ep = 0.0;
		data->total_err = 0.0;
		data->ramp_start_time = 0;
		data->last_sample_time = 0;
		data->ramp_down_start = 0;
		data->overshot = 0;
		data->have_stopped = 1;
		data->speed = 0;
		data->speedcount = 0;

		data->curr_seek_power = 20;
		data->sub_state = is_stopped;
		data->power_scalar = 1.0;

		char buf[200];
		snprintf(buf, 200, "/tmp/%s", data->conveyor_name);
		data->logfile = fopen(buf, "a");

		fprintf(data->logfile,"%s plugin initialised ok: update time %ld\n", data->conveyor_name, *data->min_update_time);
	}

	return PLUGIN_COMPLETED;
}

#if 1
static void atposition(struct PIDData*data, void *scope) {
	data->last_Ep = 0;
	data->total_err = 0;
	data->overshot = 0;
	data->have_stopped = 1;
	reset_stop_test(data);
	data->state = cs_atposition;
	data->ramp_down_start = 0;
	changeState(scope, "atposition");
	setIntValue(scope, "StopError", *data->stop_position - *data->position);
}

static int has_stopped(struct PIDData *data) {
	if (data->debug && *data->debug) fprintf(data->logfile,"checkng for stop %ld %d\n", data->speed, data->speedcount);
	if ( labs(data->speed) <2 && data->speedcount++ >4) return 1;
	return 0;
}
#endif

static void halt(struct PIDData*data, void *scope) {
	if (data->debug && *data->debug) fprintf(data->logfile,"%s halt\n", data->conveyor_name);
	data->last_set_point = 0;
	data->saved_set_point = 0;
	data->changing_set_point = 0;
	data->last_Ep = 0;
	data->total_err = 0;
	data->ramp_down_start = 0;
	data->ramp_start_time = 0;
	data->current_power = 0;
	setIntValue(scope, "driver.VALUE", output_scaled(data, 0) );
	if (data->debug && *data->debug) 
	    fprintf(data->logfile, "output driver set to %ld\n", output_scaled(data, 0) );
}

static void stop(struct PIDData*data, void *scope) {
	if (data->debug && *data->debug) fprintf(data->logfile,"%s stop command\n", data->conveyor_name);
	halt(data, scope);
	data->stop_marker = 0;
	//data->last_stop_position = 0;
	setIntValue(scope, "SetPoint", 0);
	data->state = cs_stopped;
	data->curr_seek_power = 20;
	if (data->state == cs_interlocked) {
		if (data->debug && *data->debug)
			fprintf(data->logfile,"%s interlocked\n", data->conveyor_name);
	}
	else {
		if (data->debug && *data->debug)
		fprintf(data->logfile,"%s stopped\n", data->conveyor_name);
	}

}

static int sign(double val) { return (0 < val) - (val < 0); }

/* select whether to use the shared settings or custom forward/reverse settings for pid constants */
static void select_kpid(struct PIDData *data) {
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

}

/* crawl to the stop position in either direction */

static long perform_stopping(struct PIDData*data, void *scope, void* statistics_scope) {
    if (data->debug && *data->debug) fprintf(data->logfile, "%s stopping\n", data->conveyor_name);

	long new_power;
	/* check to see if we have entered the tolerance range */
	if ( *data->stop_position - *data->rev_tolerance <= *data->position &&
		*data->position <= *data->stop_position + *data->fwd_tolerance ) {
		if ( labs(data->speed) < 4 ) {
 			if (data->debug && *data->debug) {
				fprintf(data->logfile, "%s calling stop() within tolerance, %ld, %ld - %ld\n",
				 data->conveyor_name, *data->position, *data->stop_position, 
				 (long)(*data->position - *data->stop_position));
			}
			stop(data, scope);
			data->sub_state = is_stopped;
		}
		else {
       		if (data->debug && *data->debug)
    			fprintf(data->logfile, "%s calling halt() within tolerance\n", data->conveyor_name);
			halt(data, scope);
		}
    	setIntValue(statistics_scope, "Vel", data->speed);
    	setIntValue(statistics_scope, "Pwr", 0);
		return 0;
	}
	else {
    	/* increase power directly if the machine doesn't seem to be moving */
    	if ( labs(data->curr_seek_power) < 400 && labs(data->last_position - *data->position) < 4) {
    		data->curr_seek_power += 5;
    		if (data->debug && *data->debug)
    			fprintf(data->logfile, "%s output power: %ld\n", data->conveyor_name, data->curr_seek_power);
    	}
    	data->last_position = *data->position;
    	if ( *data->stop_position > *data->position )
    		new_power =  data->curr_seek_power;
    	else
    		new_power = - data->curr_seek_power;
    	if (data->debug && *data->debug)
    		fprintf(data->logfile,"%s stopping, power: %ld\n", data->conveyor_name, new_power);
    	setIntValue(statistics_scope, "Vel", data->speed);
    	setIntValue(statistics_scope, "Pwr", new_power);
    	return new_power;
	}
}

/* handle inter-set_point speed changes by ramping the set point from the original
    to the requested value
*/

static long check_ramp(struct PIDData *data, double set_point, uint64_t now_t, uint64_t ramp_time) {
	if (data->debug && *data->debug)
		fprintf(data->logfile,"%s checking for ramp end\n", data->conveyor_name);

	/* finished ramping? 
	    ramp_time - duration of the ramp
	    data->changing_set_point - time that the set point changed (microsecs) 
	*/
	
	data->sp_delta_t += delta_t; // total time spent ramping
	if (data->sp_delta_t >= ramp_time)
	    set_point = data->filtered_set_point;
	else
    	set_point = data->filtered_set_point - ( data->filtered_set_point - set_point ) * (ramp_time - data->sp_delta_t) / ramp_time;
	
	if (data->debug && *data->debug)
		fprintf(data->logfile,"%s ramping to new set point: %ld, now using: %5.2f\n",
					data->conveyor_name, data->filtered_set_point, set_point);

	return set_point;
}

static void display(struct PIDData *data, void* scope, uint64_t delta_t, char *current) {
	if (data->debug && *data->debug )
		fprintf(data->logfile,"%ld\t %s test: %s %ld, stop: %ld, spd: %ld, pow: %5.3f, pos: %ld\n",

				(long)delta_t, data->conveyor_name,
				(current)? current : "null",
				data->filtered_set_point,
				*data->stop_position,
				data->speed,
				data->current_power,
				*data->position);
}

static void display2(struct PIDData *data, double Ep, double dt, double set_point, long next_position) {
	if (data->debug && *data->debug && (data->speed != 0 || set_point != 0.0) )
		fprintf (data->logfile,"%s pos: %ld, Ep: %5.3f, tot_e %5.3f, dt: %5.3f, "
				"pwr: %5.3f, spd: %ld, setpt: %5.3f, next: %ld\n",
				data->conveyor_name,
				*data->position, Ep, data->total_err, dt,
				data->current_power,
				data->speed,
				set_point, next_position);

}

static int get_position(struct PIDData *data) {
	if (*data->position == 0) {
		data->last_position = 0; /* startup compensation before the conveyor starts to move*/
		data->start_position = 0;
	}
	else if (data->last_position == 0) {
		data->last_position = *data->position;
		data->start_position = data->last_position;
		{   /* prepare the speed sample buffer */
		    int i; for (i=0; i< 8; ++i) addSample(data->samples, i, data->start_position);
		}
	}
	if ( labs(*data->position - data->last_position) > 10000 ) { /* protection against wraparound */
		data->last_position = *data->position;
		return 0; /* not ready, caller should return and wait another cycle */
	}
	return 1; /* all done  */
}

static void get_speed(struct PIDData *data) {
	/*	if (data->debug && *data->debug)
		data->speed = 1000000 * rateDebug(data->samples);
	 else
	 */
	data->speed = 1000000 * rate(data->samples);
	if ( labs(data->speed) > 20000 && data->debug && *data->debug) {
	    fprintf(data->logfile, "Bad speed: %ld, sample size: %d\n", data->speed, length(data->samples));
	    fflush(data->logfile);
	    usleep(500000);
	}

#ifdef USE_MEASURED_PERIOD
	long pspeed = 0;
	addSample(data->psamples, now_t, *data->grab_period);
	long pp = bufferAverage(data->psamples);
	if (data->grab_period && pp)
		pspeed = 4000 * 10000 / pp;
	else
		pspeed = 0;
#endif
}

static void init_stats(void *statistics_scope) {
	changeState(statistics_scope, "Idle");
	setIntValue(statistics_scope, "Vel", 0);
	setIntValue(statistics_scope, "Pwr", 0);
	setIntValue(statistics_scope, "SetPt", 0);
	setIntValue(statistics_scope, "Err_p", 0);
	setIntValue(statistics_scope, "Err_i", 0);
	setIntValue(statistics_scope, "Err_d", 0);
}

static int within_tolerance(struct PIDData *data) {
	return  ( *data->position < *data->stop_position  
	        && *data->stop_position - *data->position <= *data->fwd_tolerance )
	||
	( *data->position > *data->stop_position 
	        && *data->position - *data->stop_position <= *data->rev_tolerance );
}

static void update_forward_power_offset(struct PIDData *data, void *scope) {

    if (!power_valid( data, data->current_power) ) {
        if (data->debug && *data->debug)
            fprintf(data->logfile, "%s ERROR: calculated power %8.3lf is out of range\n",
                data->conveyor_name, data->current_power);
        data->current_power = 0.0;  
    }    
    else if (data->current_power>0 ) {
    	double mean_offset = bufferAverage(data->fwd_power_offsets);
    	if (*data->position - data->last_position > 6 && data->current_power > 0.5*mean_offset) /* overpowered */
    		addSample(data->fwd_power_offsets, data->now_t, 0.5 * mean_offset);
    	else
    		addSample(data->fwd_power_offsets, data->now_t, data->current_power);
    	long new_start_power = long_range_limited( bufferAverage(data->fwd_power_offsets), 1, 2000);
    	setIntValue(scope, "fwd_settings.PowerOffset", new_start_power);

    	char *tmpstr = doubleArrayToString(length(data->fwd_power_offsets), data->fwd_power_offsets->values);
    	setStringValue(scope,"fwd_settings.StartupPowerOffsets", tmpstr);
    	free(tmpstr);
    	data->fwd_start_delay = data->now_t - data->start_time;
    	if (data->fwd_start_delay > 200000) data->fwd_start_delay = 200000;
    }
}

void update_reverse_power_offset(struct PIDData *data, void *scope) {
    if (!power_valid( data, data->current_power) ) {
        if (data->debug && *data->debug)
            fprintf(data->logfile, "%s ERROR: calculated power %8.3lf is out of range\n",
                data->conveyor_name, data->current_power);
        data->current_power = 0.0;  
    }    
    else if (data->current_power < 0) {
    	double mean_offset = bufferAverage(data->rev_power_offsets);
    	if (*data->position - data->last_position < -6 && data->current_power < 0.5*mean_offset) /* overpowered */
    		addSample(data->rev_power_offsets, data->now_t, 0.5 * mean_offset);
    	else
    		addSample(data->rev_power_offsets, data->now_t, data->current_power);
    	long new_start_power = long_range_limited( bufferAverage(data->rev_power_offsets), -2000, -1);
    	setIntValue(scope, "rev_settings.PowerOffset", new_start_power);


    	char *tmpstr = doubleArrayToString(length(data->rev_power_offsets), data->rev_power_offsets->values);
    	setStringValue(scope,"rev_settings.StartupPowerOffsets", tmpstr);
    	free(tmpstr);

    	data->rev_start_delay = data->now_t - data->start_time;
    	if (data->rev_start_delay > 200000) data->rev_start_delay = 200000;
	}
}

long handle_prestart_calculations(struct PIDData *data, void *scope, double set_point, enum State new_state, long new_power) {
        
	if (data->debug && *data->debug)
		fprintf(data->logfile,"processing prestart calculations\n");

	if (data->current_power != 0.0 && sign(set_point) != sign(data->current_power)) {
		if (data->debug && *data->debug) {
			fprintf(data->logfile , "%s current power is %8.3lf when set point is %lf8.3 starting up. resetting current power\n",
				data->conveyor_name, data->current_power, set_point);
		}
		data->current_power = sign(set_point);
	}
	
	/* have we found forward movement during prestart? */
	if (set_point > 0 && *data->position - data->start_position > 20) { /* fwd motion */
		if (new_state == cs_position || new_state == cs_speed) {
			data->state = new_state; 
			data->sub_state = is_ramping_up;
 			data->ramp_start_time = 0;
			data->last_position = *data->position;
		}
		/* update forward power offset */
		update_forward_power_offset(data, scope);

		if (data->debug && *data->debug)
			fprintf(data->logfile,"%s fwd movement started. time: %ld power: %ld\n",
					data->conveyor_name, (long)data->fwd_start_delay, *data->fwd_start_power);
	}

	/* have we found reverse movement during prestart */
	else if (set_point < 0 && *data->position - data->start_position < -20) { /* reverse motion */
		if (new_state == cs_position || new_state == cs_speed) {
			data->state = new_state;
			data->sub_state = is_ramping_up;
			data->ramp_start_time = 0;
			data->last_position = *data->position;
		}
		update_reverse_power_offset(data, scope);
		if (data->debug && *data->debug)
			fprintf(data->logfile,"%s rev movement started. time: %ld power: %ld\n",
					data->conveyor_name, (long)data->rev_start_delay, *data->rev_start_power);
	}
	else {
		/* waiting for motion during prestart */
		if (data->current_power > 0) {
			if (data->now_t - data->start_time >= data->fwd_start_delay
				&& data->now_t - data->last_prestart_change >= data->prestart_change_delay )  {
				new_power = data->current_power + 20;
				data->last_prestart_change = data->now_t;
			}
		}
		else if (data->current_power < 0) {
			if (data->now_t - data->start_time >= data->rev_start_delay
				&& data->now_t - data->last_prestart_change >= data->prestart_change_delay )  {
				new_power = data->current_power - 20;
				data->last_prestart_change = data->now_t;
			}
		}
		if (data->debug && *data->debug)
		 fprintf(data->logfile,"%s waiting for movement. time: %ld power: %ld\n",
				 data->conveyor_name, (long)(data->now_t - data->start_time), new_power);
	}
	return new_power;
}

PLUGIN_EXPORT
int poll_actions(void *scope) {
	struct PIDData *data = (struct PIDData*)getInstanceData(scope);
	double new_power = 0.0f;
	char *current = 0;

	if (!data) return PLUGIN_COMPLETED; /* not initialised yet; nothing to do */

    uint64_t now_t = microsecs();
	data->now_t = now_t;
	if (data->last_poll == 0) goto done_polling_actions; // priming read
	data->delta_t = now_t - data->last_poll;

	void *statistics_scope = getNamedScope(scope, "stats");

	/* collect position, last_position values */
	if ( !get_position(data) ) return PLUGIN_COMPLETED;

	if (now_t - data->last_sample_time >= 10000) {
		addSample(data->samples, now_t, *data->position);
		data->last_sample_time = now_t;
	}

	if ( data->delta_t/1000 < *data->min_update_time) return PLUGIN_COMPLETED;

	double dt = (double)(data->delta_t)/1000000.0; /* delta_t in secs */
 
    /* compute current speed */
    get_speed(data);

	setIntValue(scope, "Velocity", data->speed);
	setIntValue(scope, "Position", *data->position);

	/* select the Kp/Ki/Kd we are going to use */
	select_kpid(data);

	new_power = data->current_power;
	/*
	if (data->debug && *data->debug) 
    	fprintf(data->logfile,"%s copied current power (%8.3lf) to new power\n", data->conveyor_name, data->current_power);
	*/
	enum State new_state = data->state;

	long next_position = 0;
	data->filtered_set_point = *data->set_point;
	
	/* Check the current state of the clockwork machine to determine what the controller should be doing  */
	current = getState(scope);

	if (strcmp(current, "interlocked") == 0) new_state = cs_interlocked;
	else if (strcmp(current, "restore") == 0) new_state = cs_interlocked;
	else if (strcmp(current, "stopped") == 0) new_state = cs_stopped;
	else if (strcmp(current, "speed") == 0) new_state = cs_speed;
	else if (strcmp(current, "seeking") == 0) new_state = cs_position;
	else if (strcmp(current, "atposition") == 0) new_state = cs_atposition;

	/* if interlocked,  stop if necessary and return */
	if (new_state == cs_interlocked ) {
		if (data->state != cs_interlocked)  {
			stop(data, scope);
			data->state = cs_interlocked;
		}
		goto done_polling_actions;
	}

	if (new_state == cs_stopped) {
	  if (data->state != cs_stopped || data->sub_state != is_stopped) {
        if (data->current_power != 0 ||*data->set_point != 0 
    	        || data->sub_state != is_stopped || data->state != cs_stopped) {
        		data->sub_state = is_stopped;
        		stop(data, scope);
    		}
				new_power = 0;
    		data->state = cs_stopped;
    		data->sub_state = is_stopped;
        	goto calculated_power;
		}
		return PLUGIN_COMPLETED;
	}

	if (data->sub_state == is_stopping || ( new_state == cs_position && data->state == cs_stopping) )
		new_state = cs_stopping;

	/* if this is a new set point we will abort any current process and ramp to the new set point */
	if ( *data->set_point != data->saved_set_point) {
        if ( (data->state == cs_position || data->state == cs_stopping)
                && labs(*data->stop_position - *data->position) < 250) {
		    data->sub_state = is_stopping;
            if (data->debug && *data->debug) 
    		    fprintf(data->logfile,"%s detected change of set point - crawling to position\n", data->conveyor_name);
	    }
		else if ( (data->speed < 2 || data->saved_set_point == 0)  && *data->set_point != 0) {
		    data->sub_state = is_prestart_init;
		    data->start_position = *data->position;
            if (data->debug && *data->debug) 
    		    fprintf(data->logfile,"%s detected change of set point %ld vs %ld - starting up\n",
    		     data->conveyor_name, data->saved_set_point, *data->set_point);
	    }
        else {
		    data->sub_state = is_changing;
		    data->changing_set_point = now_t;
            if (data->debug) 
    		    fprintf(data->logfile,"%s detected change of set point - ramping to it\n", data->conveyor_name);
		}
	    data->saved_set_point = *data->set_point;
	}

	double set_point = data->filtered_set_point;

    /* determine what ramp time configuration we will use (if needed) */
	long ramp_time = *data->fwd_ramp_time;
	long curr_startup_time = *data->fwd_startup_time;
	if (set_point < 0  ||
		(set_point > 0
		 && (new_state == cs_position || new_state == cs_atposition )
		 && *data->stop_position < *data->position) ) {
			ramp_time = *data->rev_ramp_time;
			curr_startup_time = *data->rev_startup_time;
		}


	/* add the correct sign to the set point */
	if (set_point != 0 && (new_state == cs_position || new_state == cs_atposition || new_state == cs_stopping )
		&& sign(set_point) != sign(*data->stop_position - *data->position)) {
		set_point = -set_point;
	}

	/* if we are stopping, just calculate a power ramp */
	if (new_state == cs_stopping || data->sub_state == is_stopping) {
		new_power = perform_stopping(data, scope, statistics_scope);
		if (new_power == 0) {
		    stop(data, scope);    
		}
		display(data, scope, data->delta_t, current);
		goto calculated_power;
	}

	/* if debugging, display what we have found */
	if (new_state == cs_position || new_state == cs_speed)
		display(data, scope, data->delta_t, current);

	/* beyond this point we are either controlling to a speed to seeking a position */

	/* similarly, if we should be stopped stop if necessary and return */
	if (new_state == cs_stopped) {
		if (data->speed != 0) stop(data, scope);
		data->state = cs_stopped;
		data->sub_state = is_stopped;
		init_stats(statistics_scope);
		goto done_polling_actions;
	}

	long dist_to_stop = 0;

	/* has the stop position changed while we are at position or seeking? */
	if (new_state == cs_position || new_state == cs_atposition) {
		if ( data->last_stop_position != *data->stop_position ) {
			if (data->debug) 
			    fprintf(data->logfile,"%s new stop position %ld was %ld\n", 
			        data->conveyor_name, *data->stop_position, data->last_stop_position);
			data->last_stop_position = *data->stop_position;
			data->stop_marker = *data->mark_position;
			if (new_state == cs_position) changeState(scope, "seeking");
		}
		dist_to_stop = *data->stop_position - *data->position;
	}
	
	/* from here on in the positioning states cs_position or cs_atposition it is valid
	 to use dist_to_stop
	 */

	/* check if we need to ramp to a new set_point when we are already moving */
	if (data->sub_state == is_changing)
	    set_point = check_ramp(data, set_point, now_t, ramp_time);

	/* if we are at position check for drift and if necessary seek again */
	if (new_state == cs_atposition ) {
		if ( within_tolerance(data) ) {
			/*changeState(scope, "seeking"); */
			/*data->sub_state = is_stopping; */
			stop(data, scope);
			reset_stop_test(data);
			goto calculated_power;
		}
		else {
			/* if the stop position is close, just creep to it, otherwise start as normal */
			changeState(scope, "seeking");
			if ( labs(dist_to_stop) >250 ) {
				data->sub_state = is_prestart_init;
				data->start_position = *data->position;
				if (data->debug && *data->debug)
					fprintf(data->logfile,"%s entering prestart\n", data->conveyor_name);
			}
			else
    		    data->sub_state = is_stopping;
		}
	}

	/* test for prestart condition where power ramping is done to find a minimum movement power */
	if ( (data->sub_state != is_prestart && data->sub_state != is_prestart_init)
		&& (data->state == cs_stopped || data->state == cs_atposition)  /* currently stopped */
		&& (new_state == cs_position || new_state == cs_speed) )         /* but asked to move */
	{
		if (data->debug && *data->debug)
			fprintf(data->logfile,"%s entering prestart\n", data->conveyor_name);
		data->state = cs_prestart;
		data->sub_state = is_prestart_init;
		data->start_position = *data->position;
		data->have_stopped = 0;
	}

	int close_to_target = 0;

	if (new_state == cs_position) {

		/* once we are very close we no longer calculate a moving target */
		if ( dist_to_stop >= 0 && dist_to_stop <= *data->fwd_tolerance + 0.1 * set_point)  { /* 100ms */
			if (data->debug && *data->debug)
				fprintf(data->logfile,"%s getting close (fwd) data->speed = %ld pos:%ld stop:%ld\n",
						data->conveyor_name, data->speed, *data->position, *data->stop_position);
			data->state = cs_stopping;
			data->sub_state = is_stopping;
			close_to_target = 1;
			if (data->debug && *data->debug) fprintf(data->logfile,"%s stopping(fwd)\n", data->conveyor_name);
			if ( labs(data->speed) < 40 && dist_to_stop <= *data->fwd_tolerance) {
				if (data->debug && *data->debug) fprintf(data->logfile,"%s at position (fwd)\n", data->conveyor_name);
				atposition(data, scope);
				goto calculated_power;
			}
		}
		else if ( dist_to_stop <= 0 &&  0.1 * set_point - dist_to_stop <= *data->rev_tolerance) {
			if (data->debug) fprintf(data->logfile,"%s getting close (rev) data->speed = %ld pos:%ld stop:%ld\n",
									 data->conveyor_name, data->speed, *data->position, *data->stop_position);
			close_to_target = 1;
			data->sub_state = is_stopping;
			data->state = cs_stopping;
			if (data->debug && *data->debug) fprintf(data->logfile,"%s stopping(rev)\n", data->conveyor_name);
			data->curr_seek_power = 20;
			if (labs(data->speed)< 40 && - dist_to_stop <= *data->rev_tolerance) {
				if (data->debug && *data->debug) fprintf(data->logfile,"%s at position (rev)\n", data->conveyor_name);
				atposition(data, scope);
				goto calculated_power;
			}
		}

        if (data->state == cs_position && within_tolerance(data) && data->speed == 0) {
            if (data->debug && *data->debug) fprintf(data->logfile, "at pos\n");
            changeState(scope, "atposition");
        }
        else if (data->state == cs_atposition) {
            if (!within_tolerance(data)) {
    			data->sub_state = is_stopping;
    			data->state = cs_stopping;
            }
        }

		/* within a few tolerance steps of the stop position, constrain the set point
		 we use the tolerance amount as out unit of measure here on the assumption
			that it is an indicator of the accuracy of the equipment we are using so
			is a fair representation of the control we have.
		 We arbitrarily decide that it's ok to limit speed within k tolerances
		 */

		/* in the filter before this we decided we were not 'close_to_target' but
		 we change our mind now so that later filters can use that info if requied */
		if ( dist_to_stop >= 0 && dist_to_stop < 5 * *data->fwd_tolerance) {
			close_to_target = 1;
			double tol = *data->fwd_tolerance;
			if (tol == 0) tol = 1;
			double maxspd = 50 +  (dist_to_stop / tol ) * 50;
			if (set_point > maxspd) set_point = maxspd;
		}
		else if ( dist_to_stop <= 0 && dist_to_stop <= -5 * *data->rev_tolerance) {
			close_to_target = 1;
			double tol = *data->rev_tolerance;
			if (tol == 0) tol = 1;
			double maxspd = -50 +  (dist_to_stop / tol ) * 50;
			if (set_point < maxspd) set_point = maxspd;
		}
		/* at this point the set_poin may bave been constrained due to proximity to the stop point*/
		if (close_to_target && data->state == cs_prestart) data->state = new_state;

		/* check sign of the set point */
		if (sign(set_point) != sign(*data->stop_position - *data->position) ) set_point = -set_point;
	}


	/* when prestarting, we wait for the conveyor to show some movement and keep a record of when it happens.
	 If no movement occurs after a time we gradually increasing power until that happens */

	/* even in prestart, we need to check whether we are at position and stop if necessary */
	if (data->sub_state == is_prestart_init) {
		changeState(statistics_scope, "Busy");
		data->start_time = now_t;
		data->start_position = *data->position;
		if (set_point == 0) new_power = 0;
		else if (set_point>0) new_power = *data->fwd_start_power;
		else new_power = *data->rev_start_power;
		
		if (data->debug && *data->debug)
			fprintf(data->logfile,"%s initial power : %3.1f\n", data->conveyor_name, new_power);
		data->last_prestart_change = now_t;
		data->sub_state = is_prestart;
	}

	if ( data->sub_state == is_prestart ) {
        new_power = handle_prestart_calculations(data, scope, set_point, new_state, new_power);
        goto calculated_power;
	}

	if (new_state == cs_speed && data->state != cs_speed) {
		data->state = cs_speed;
	}

	/* if we got here, we must be moving and we may be ramping up or down, controlling to a speed or
		seeking a position */

	double ramp_down_ratio = 1.0;
	double ramp_up_ratio = 1.0;

	if ( (new_state == cs_position || new_state == cs_speed) && data->sub_state == is_ramping_up ) {
    	if (data->ramp_start_time == 0 && set_point != 0) {
    	    /* prestart will have the conveyor moving already, allow for that and set the start time back a little */
    	    long time_offset = data->speed / set_point;
    	    data->ramp_start_time = now_t - time_offset * curr_startup_time;
    	}
    	//long startup_time = (now_t - data->ramp_start_time + 500)/1000;
	    
		ramp_up_ratio = data->speed / set_point; 	/*(double)startup_time / curr_startup_time;*/
		if ( sign(data->speed) == sign(set_point) && labs(data->speed) >= (long)fabs(set_point)) {
    		ramp_up_ratio = 1.0;
		}
		
		if (ramp_up_ratio < 0.05) ramp_up_ratio = 0.05;
		if (data->debug && *data->debug && ramp_up_ratio >0.0 && ramp_up_ratio<=1.0)
			fprintf(data->logfile,"%s rampup ratio: %5.3f\n", data->conveyor_name, ramp_up_ratio);
		if (ramp_up_ratio >= 1.0 ) {
		    data->sub_state = is_speed;
		    if ( fabs(set_point) > 800.0) {
		        double power = getBufferValueAt(data->power_records, (long)now_t - 80000);
		        data->power_rate = power / set_point;
		        if (fabs(data->power_rate) > 2.0) data->power_rate = 2.0 / data->power_rate;
		        if (data->debug && *data->debug) {
		            fprintf(data->logfile, "%s using power ratio %8.3lf from power %8.3lf", data->conveyor_name, data->power_rate, power);
		        }
		    }
		}
	}

	/* calculate ramp-down adjustments */
	if (new_state == cs_position) {
		/* adjust the set point to cater for whether we are close to the stop position */
		double stopping_time = (double) *data->stopping_time / 1000.0;
		if (*data->stop_position >= *data->position && data->fwd_stopping_time) {
			if (data->debug && *data->debug) fprintf(data->logfile,"using forward stopping time\n");
			stopping_time = ( (double)*data->fwd_stopping_time) / 1000.0;
		}
		else if (*data->stop_position < *data->position && data->rev_stopping_time) {
			if (data->debug && *data->debug) fprintf(data->logfile,"using reverse stopping time\n");
			stopping_time = ( (double)*data->rev_stopping_time) / 1000.0;
		}
		/* The formulae following require that stopping time be non zero,
	        we choose dt at a reasonable minimum.
		 */
		if (stopping_time < dt) stopping_time = dt;
		if (data->debug && *data->debug) fprintf(data->logfile,"stopping time: %8.3lf\n", stopping_time);

		/* calculate a ramp down ratio. later this will be used to modify the current set_point */
		if (stopping_time > 0 && data->ramp_down_start) {
			ramp_down_ratio = ( ( (double)data->ramp_down_start - (double)now_t )/1000000.0 + stopping_time) / stopping_time;
			if (ramp_down_ratio < 0.05) ramp_down_ratio = 0.05;
		}
		/* stopping distance at vel v with an even ramp is s=vt/2 */
		double ramp_dist = fabs(data->speed * stopping_time) / 2.0;
		if (data->debug && *data->debug)
			fprintf(data->logfile,"stopping_time: %8.3lf, speed: %ld, ramp_dist: %8.3lf ramp_down: %8.3lf\n",
					stopping_time, data->speed, ramp_dist, ramp_down_ratio);

		/* we may not have calculated a ramp_down_ratio in which case it will have a value of 1 */
		/* the double test here prevents us from recalculating the ramp_down_ratio unless
		 it looks like we are stopping too fast
		 */
		if (labs(*data->stop_position - *data->position) < ramp_dist * ramp_down_ratio) {
			if ( data->ramp_down_start == 0 && labs(*data->stop_position - *data->position) < ramp_dist) {
				/* start ramp down */
				data->ramp_down_start = now_t;
				if (data->debug && *data->debug)  {
					fprintf(data->logfile,"---Beginning ramp down. Ki and Kd components are disabled");
					fprintf(data->logfile,"%s ramping down at %ld aiming for %ld over %5.3f\n", data->conveyor_name,
							*data->position, *data->stop_position, stopping_time);
				}
			}
		}
		if (set_point != 0 && sign(set_point) != sign(dist_to_stop)) {
			set_point = -set_point;
			if (data->debug && *data->debug) 
			    fprintf(data->logfile,"%s fixing direction: %5.3f\n", data->conveyor_name, set_point);
		}
		if (data->debug && *data->debug)
			fprintf(data->logfile,"%s approaching %s, ratio = %5.2f (%ld) set_point: %5.2f\n", data->conveyor_name,
					( sign(dist_to_stop) >= 0) ? "fwd" : "rev", ramp_down_ratio, dist_to_stop, set_point);
	}
	if (ramp_down_ratio < 1.0) {
	    if (ramp_down_ratio < 0.05 ) ramp_down_ratio = 0.05;
	    set_point *= ramp_down_ratio;
	}
	else	/* if no ramp down was applied but we are within the ramp up time, apply the ramp up */
    	if ( (ramp_down_ratio <= 0.0 || fabs(ramp_down_ratio) >= 1.0) && ramp_up_ratio <1.0) {
    		set_point = data->filtered_set_point * ramp_up_ratio * data->power_rate;
    		data->last_set_point = set_point; /* don't let the set_point change detection get in the way */
    	}

	if (data->state == cs_speed) {
		next_position = data->last_position + set_point;
		if (data->debug && *data->debug)
			fprintf(data->logfile,"%s (speed) next position (%ld)", data->conveyor_name, next_position);
	}
	else if (data->state == cs_position) {
		/*
		if (fabs(ramp_down_ratio ) <= 0.05)
			next_position = *data->stop_position;
		*/
		if (labs(dist_to_stop) <= fabs(set_point) && sign(dist_to_stop) == sign(set_point))
			next_position = *data->stop_position;
		/* next_position = data->last_position + set_point; */
		else if (sign(dist_to_stop) == sign(set_point) ) {

			next_position = data->last_position + set_point;
			if (sign( *data->stop_position - next_position) != sign( *data->stop_position - data->last_position) ) {
				double d1 = labs(*data->stop_position - next_position);
				double d2 = labs(*data->stop_position - data->last_position);
				if (d1 > d2) {
					double ratio = d2/d1;
					set_point *= ratio;
					next_position = *data->stop_position + set_point;
				}
			}

			/* next_position = *data->stop_position; */
			if (ramp_down_ratio <= 0.05) {
				next_position = *data->stop_position;
				fprintf(data->logfile,"adjust next_position to be the stop position\n");
			}
			else if (ramp_down_ratio >=0 && ramp_down_ratio <1.0) {
				if (data->debug && *data->debug) fprintf(data->logfile,"adjusting positional error by %lf%%\n", ramp_down_ratio);
				double ratio = ramp_down_ratio;
				if (ratio<0.01) ratio = 0.01;
				next_position = *data->position + (next_position - data->last_position) * ramp_down_ratio;
			}
		}

	}
	else if (data->state == cs_atposition)
		next_position = *data->stop_position;
	else {
		next_position = *data->position;
	}

	/*double Ep = next_position - data->last_position - (*data->position - data->last_position) / dt; */
	/*double Ep = next_position - *data->position;*/
	double Ep = (set_point - data->speed);

	/*
	 long changed_pos = *data->position - data->start_position;
	 if (changed_pos != *data->position_change)
	 setIntValue(scope, "position", changed_pos);
	 */


	if (data->state == cs_speed || data->state == cs_position) {
		/* data->total_err += (data->last_Ep + Ep)/2 * dt; */
		double error_v = (set_point - data->speed) * dt;
		data->total_err = error_v;

		setIntValue(statistics_scope, "SetPt", set_point);
		setIntValue(statistics_scope, "Vel", data->speed);

		/* cap the total error to limit integrator windup effects */
		if (data->total_err > 6000) data->total_err = 6000;
		else if (data->total_err < -6000) data->total_err = -6000;

		setIntValue(statistics_scope, "Err_i", data->total_err);

		/* add filtering on the error term changes to limit the effect of noise */
		data->last_filtered_Ep = data->filtered_Ep;
		data->filtered_Ep = 0.8 * Ep + 0.2 * data->last_Ep;
		double de = (data->filtered_Ep - data->last_filtered_Ep) / dt / 1000; 
		data->last_Ep = Ep;

		if (de>100) de = 100.0;
		if (de<-100) de = -100.0;

		setIntValue(statistics_scope, "Err_p", Ep);
		setIntValue(statistics_scope, "Err_i", data->total_err);
		setIntValue(statistics_scope, "Err_d", de);

		double Dout = data->power_rate * set_point;

		if ( next_position > *data->position && data->use_Kpidf) { /* forward */
			if (data->ramp_down_start == 0)
				Dout += (int) (data->Kpf * Ep + data->Kif * data->total_err + data->Kdf * de);
			else
				Dout += (int) (data->Kpf * Ep);
		}
		else if (next_position < *data->position && data->use_Kpidr) { /* reverse */
			if (data->ramp_down_start == 0)
				Dout += (int) (data->Kpr * Ep + data->Kir * data->total_err + data->Kdr * de);
			else
				Dout += (int) (data->Kpr * Ep);
		}
		else { /* shared configuration */
			if (data->ramp_down_start == 0)
				Dout += (int) (data->Kp * Ep + data->Ki * data->total_err + data->Kd * de);
			else
				Dout += (int) (data->Kp * Ep);
		}

		setIntValue(statistics_scope, "Pwr", Dout);

		if (data->debug && *data->debug && fabs(Ep)>5)
			fprintf(data->logfile,"%s Set: %5.3f Ep: %5.3f Ierr: %5.3f de/dt: %5.3f\n", data->conveyor_name,
					set_point, Ep, data->total_err, de );

		new_power = Dout;
	}
    display2(data,  Ep, dt, set_point, next_position);

calculated_power:
	if (new_power == 0 || new_power != data->current_power) {
    	if (data->debug && *data->debug)
    		fprintf(data->logfile,"%s new power: %8.3lf curr power %8.3lf\n", data->conveyor_name, new_power, data->current_power);
		setIntValue(statistics_scope, "Pwr", new_power);
		if ( new_power > 0 && new_power > data->current_power + *data->ramp_limit) {
			fprintf(data->logfile,"warning: %s wanted change of %f6.2, limited to %ld\n",
					data->conveyor_name, new_power - data->current_power, *data->ramp_limit);
			new_power = data->current_power + *data->ramp_limit;
		}
		else if ( new_power < 0 && new_power < data->current_power - *data->ramp_limit) {
			fprintf(data->logfile,"warning: %s wanted change of %f6.2, limited to %ld\n",
					data->conveyor_name, new_power - data->current_power, - *data->ramp_limit);
			new_power = data->current_power - *data->ramp_limit;
		}
    	long power = 0;
    	if (new_power>0) {
    	    power = output_scaled(data, (long) new_power + *data->fwd_start_power);
    	    addSample(data->power_records, data->now_t, power);
    	}
    	else if (new_power<0) {
    	    power = output_scaled(data, (long) new_power + *data->rev_start_power);
    	    addSample(data->power_records, data->now_t, power);
    	}
    	else power = output_scaled(data, 0);

    	if (data->debug && *data->debug)
    		fprintf(data->logfile,"%s setting power to %ld (scaled: %ld, fwd offset: %ld, rev offset:%ld)\n",
    				data->conveyor_name, (long)new_power, power, *data->fwd_start_power, *data->rev_start_power);
	
    	setIntValue(scope, "driver.VALUE", power);
    	data->current_power = new_power;
	}
	
done_polling_actions:
	data->last_position = *data->position;
	data->last_poll = now_t;
	if (current) { free(current); current = 0; }
	
	if (data->debug && *data->debug) fflush(data->logfile);
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

  stats PIDSTATISTICS;

	# This module uses the restore state to give the machine 
	# somewhere to go once the driver is no longer interlocked. 
	# Since we are not using stable states for stopped, seeking etc we 
	# the machine would otherwise never leave interlocked.
	interlocked WHEN driver IS interlocked OR abort IS on;
	restore WHEN SELF IS interlocked; # restore state to what was happening before the interlock
	stopped INITIAL;
	seeking STATE;
	speed STATE;
	atposition STATE;
	
	abort FLAG;

	ENTER interlocked {
		SET M_Control TO Unavailable;
	}
	ENTER stopped { 
		SET M_Control TO Ready;
		StopPosition := 0; StopMarker := 0; 
	}
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
