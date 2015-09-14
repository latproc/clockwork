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

/*
#define USE_IOTIME 1
*/

#define DEBUG_MODE ( data->debug && *data->debug )

enum State {
	cs_init,
	cs_interlocked, /* something upstream is interlocked */
	cs_stopped, 		/* sleeping */
	cs_position,
	cs_speed,
	cs_stopping,
	cs_atposition,
	cs_ramp
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
	is_ramp
};

const char *show_state(enum State s) {
    switch(s) {
        case cs_init: return "cs_init";
        case cs_interlocked: return "cs_interlocked";
        case cs_stopped: return "cs_stopped";
        case cs_position: return "cs_position";
        case cs_speed: return "cs_speed";
        case cs_stopping: return "cs_stopping";
        case cs_atposition: return "cs_atposition";
        case cs_ramp: return "cs_ramp";
        default: return "UNKNOWN";
    }
    return "";
}


const char *show_istate(enum InternalState s) {
    switch(s) {
        case is_init: return "is_init";
        case is_stopped: return "is_stopped";
        case is_prestart_init: return "is_prestart_init";
        case is_prestart: return "is_prestart";
        case is_ramping_up: return "is_ramping_up";
        case is_speed: return "is_speed";
        case is_changing: return "is_changing";
        case is_ramping_down: return "is_ramping_down";
        case is_stopping: return "is_stopping";
        case is_holding: return "is_holding";
        case is_ramp: return "is_ramp";
        default: return "UNKNOWN";
    }
    return "";
}


struct ramp_settings {
	long ramp_time;		/* duration of the ramp */
	long now;			/* current time cached value of timer */
	long ramp_start;	/* time the ramp started */
	double start_point; /* initial value */
	double target;		/* target value */
	double set_point;	/* current ramp value */
	double power_rate;
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
	struct CircularBuffer *fwd_power_rates;
	struct CircularBuffer *rev_power_rates;
	struct CircularBuffer *power_records;
	
	struct ramp_settings ramp;
	
	uint64_t now_t;
	uint64_t delta_t;

	/**** clockwork interface ***/
	const long *set_point;
	const long *stop_position;
	const long *mark_position;
	const long *position;
	const long *io_read_time;

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
	const long *fwd_crawl_speed;
	const long *fwd_power_scale;

	const long *rev_tolerance;
	const long *rev_stopping_dist;
	const long *rev_ramp_time;
	const long *rev_stopping_time;
	const long *rev_startup_time;
	const long *rev_crawl_speed;
	const long *rev_power_scale;

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
	long default_fwd_crawl;
	long default_rev_crawl;
	int inverted;
	long start_position;
	double last_Ep;
	double filtered_Ep;
	double last_filtered_Ep;
	uint64_t last_sample_time;
	uint64_t last_sample_time_system;
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
	double fwd_power_rate;
	double rev_power_rate;
	uint64_t fwd_start_delay;
	uint64_t rev_start_delay;

	uint64_t last_prestart_change;
	long prestart_change_delay;
	
	uint64_t last_control_change;

	uint64_t ramp_down_start;
	long curr_seek_power;
	double power_scalar;
	long current_set_point;

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

    double Ep;
    double Ei;
    double Ed;
	double total_err;
	double crawl_adjustment;
	double latency;
	
	FILE *logfile;
	FILE *datafile;
};

#if 1
static void reset_stop_test(struct PIDData *data) {
	if (DEBUG_MODE) fprintf (data->logfile, "@reset stop test\n");
	data->speedcount = 0;
}
#endif

static uint64_t microsecs() {
    struct timeval now;
    gettimeofday(&now, 0);
    return now.tv_sec * 1000000L + now.tv_usec;
}

void show_status(struct PIDData *data, double new_power) {
	if ( DEBUG_MODE && data->last_position != *data->position) 
	{
	    if (data->state == cs_ramp || data->sub_state == is_ramp) {
	        fprintf(data->logfile,"@ramp: %ld started %ldms ago from %.3lf to %.3lf\n",
	            data->ramp.ramp_time,(long) (microsecs() - data->ramp.ramp_start)/1000, data->ramp.start_point, data->ramp.target);
	    }
		fprintf(data->logfile,"%10s\t%10s\t%ld,%ld,%ld,%.3lf, %ld,%ld\n", 
		    show_state(data->state), show_istate(data->sub_state),
		    (long)(microsecs() - data->start_time)/1000, 
		    data->current_set_point, data->speed,
		    new_power,*data->position, *data->stop_position);
	}
}

double adjust_set_point (double current, double start, double target, long ramp_time, long ramp_start) {
        if (ramp_time <= 0) return target;
		long tr = (microsecs() - ramp_start) / 1000;
		double delta = target - start;
		double new_sp = target - delta *  (double)(ramp_time - tr) / ramp_time;
		/* check for overshoot */
		if (target >= start && new_sp > target) new_sp = target;
		else if (target <= start && new_sp < target) new_sp = target;
		//printf("@new set point %.3lf\n", new_sp);
		return new_sp;
}

double adjust_power (double current, double start, double target, long ramp_time, long ramp_start, double power_rate) {
        if (ramp_time <= 0) return target;
		long tr = (microsecs() - ramp_start) / 1000;
		double delta = target - start;
		double new_sp = target - delta *  (double)(ramp_time - tr) / ramp_time;
		/* check for overshoot */
		if (target >= start && new_sp > target) new_sp = target;
		else if (target <= start && new_sp < target) new_sp = target;
		printf("@new power %.3lf\n", new_sp * power_rate);
		return new_sp * power_rate;
}

double adjust_set_point_ramp(struct PIDData *data, struct ramp_settings *rs) {
	rs->set_point = adjust_set_point( rs->set_point, rs->start_point, rs->target, rs->ramp_time, rs->ramp_start);
	return rs->set_point;
}

double adjust_power_ramp(struct PIDData *data, struct ramp_settings *rs) {
	double power = adjust_power( rs->set_point, rs->start_point, rs->target, rs->ramp_time, rs->ramp_start ,rs->power_rate);
	return power;
}

int ramp_done(struct ramp_settings *rs) {
    return microsecs() > rs->ramp_start + rs->ramp_time*1000;
}

void init_ramp(struct PIDData *data, struct ramp_settings *rs, double start, double target, long ramp_time) {
    if (DEBUG_MODE)
        fprintf(data->logfile, "@init_ramp %.3lf %.3lf over %ld\n", start, target, ramp_time);
	rs->ramp_time = ramp_time;
	rs->now = microsecs();
	rs->ramp_start = rs->now;
	rs->start_point = start;
	rs->target = target;
	rs->set_point = rs->start_point;
    if (start < 0) rs->power_rate = data->rev_power_rate;
    else if (start > 0) rs->power_rate = data->fwd_power_rate;
    else if (target < 0) rs->power_rate = data->rev_power_rate;
    else if (target > 0) rs->power_rate = data->fwd_power_rate;
    else rs->power_rate = data->fwd_power_rate;
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
		data->fwd_power_rates = createBuffer(8);
		data->rev_power_rates = createBuffer(8);
		data->power_records = createBuffer(8);
		{
		    int i; for (i=0; i<8; ++i) {
					addSample(data->fwd_power_offsets, i, 0);
					addSample(data->rev_power_offsets, i, 0);
					addSample(data->fwd_power_rates, i, 0.5);
					addSample(data->rev_power_rates, i, 0.5);
			}
		}
#ifdef USE_MEASURED_PERIOD
		ok = ok && getInt(scope, "IA_GrabConveyorPeriod.VALUE", &data->grab_period);
#endif
		ok = ok && getInt(scope, "SetPoint", &data->set_point);
		ok = ok && getInt(scope, "StopMarker", &data->mark_position);
		ok = ok && getInt(scope, "StopPosition", &data->stop_position);
#ifdef USE_IOTIME
		if (getInt(scope, "pos.IOVALUE", &data->position)) 
			ok = ok && getInt(scope, "pos.IOTIME", &data->io_read_time);
		else {
			ok = ok && getInt(scope, "pos.VALUE", &data->position);
			data->io_read_time = 0;
		}
#else
			ok = ok && getInt(scope, "pos.VALUE", &data->position);
			data->io_read_time = 0;
#endif
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
		if (!getInt(scope, "fwd_settings.CrawlSpeed", &data->fwd_crawl_speed))
		    data->fwd_crawl_speed = &data->default_fwd_crawl;

		ok = ok && getInt(scope, "rev_settings.Tolerance", &data->rev_tolerance);
		ok = ok && getInt(scope, "rev_settings.StoppingDistance", &data->rev_stopping_dist);
		ok = ok && getInt(scope, "rev_settings.RampTime", &data->rev_ramp_time);
		ok = ok && getInt(scope, "rev_settings.PowerOffset", &data->rev_start_power);
		ok = ok && getInt(scope, "rev_settings.StoppingTime", &data->rev_stopping_time);
		if (!getInt(scope, "rev_settings.CrawlSpeed", &data->rev_crawl_speed))
		    data->rev_crawl_speed = &data->default_rev_crawl;

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
		
		if (!getInt(scope, "fwd_settings.PowerScale", &data->fwd_power_scale)) {
		    setIntValue(scope, "fwd_settings.PowerScale", 500);
		    getInt(scope, "fwd_settings.PowerScale", &data->fwd_power_scale);
		}
		if (!getInt(scope, "rev_settings.PowerScale", &data->rev_power_scale)) {
		    setIntValue(scope, "rev_settings.PowerScale", 500);
		    getInt(scope, "rev_settings.PowerScale", &data->rev_power_scale);
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
		
		data->default_debug = 1;

		/* load the persistent list of recent power offsets */
		const char *startup_offset_str = getStringValue(scope, "fwd_settings.StartupPowerOffsets");
		if (startup_offset_str) {
			double offsets[8];
			int n = stringToDoubleArray(startup_offset_str, 8, offsets);
			if (DEBUG_MODE) 
		    fprintf(data->logfile,"%s: %d forward startup power offsets found\n", data->conveyor_name, n);
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
			if (DEBUG_MODE) 
		    fprintf(data->logfile,"%s: %d reverse startup power offsets found\n", data->conveyor_name, n);
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
    	data->default_fwd_crawl = 50;
    	data->default_rev_crawl = -50;
		
		{   /* prime the position samples */
		    int i; for (i=0; i<8; ++i) addSample(data->samples, i, data->start_position);
		    rate(data->samples);
		}
		data->fwd_power_rate = (double)*data->fwd_power_scale / 1000.0; /*bufferAverage(data->fwd_power_rates); */		
		data->rev_power_rate = (double)*data->rev_power_scale / 1000.0; /*bufferAverage(data->fwd_power_rates); */

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
		data->Ep = 0.0;
		data->total_err = 0.0;
		data->ramp_start_time = 0;
		data->last_sample_time = 0;
		data->last_sample_time_system = 0;
		data->ramp_down_start = 0;
		data->overshot = 0;
		data->have_stopped = 1;
		data->speed = 0;
		data->speedcount = 0;
		data->latency = 0.1; /* number of seconds before the system responds to control */
		data->last_control_change = 0;

		data->curr_seek_power = 20;
		data->sub_state = is_stopped;
		data->power_scalar = 1.0;

		char buf[200];
		snprintf(buf, 200, "/tmp/%s", data->conveyor_name);
		data->logfile = fopen(buf, "a");
		
		data->crawl_adjustment = 25;

		fprintf(data->logfile,"%s plugin initialised ok: update time %ld\n", data->conveyor_name, *data->min_update_time);
	}

	return PLUGIN_COMPLETED;
}


static void atposition(struct PIDData*data, void *scope) {
	data->last_Ep = 0;
	data->total_err = 0;
	data->overshot = 0;
	data->have_stopped = 1;
	reset_stop_test(data);
	data->state = cs_atposition;
	data->ramp_down_start = 0;
	data->current_set_point = 0;
	changeState(scope, "atposition");
	setIntValue(scope, "StopError", *data->stop_position - *data->position);
}

static void halt(struct PIDData*data, void *scope) {
	if (DEBUG_MODE) fprintf(data->logfile,"%s halt\n", data->conveyor_name);
	data->last_set_point = 0;
	data->saved_set_point = 0;
	data->changing_set_point = 0;
	data->last_Ep = 0;
	data->total_err = 0;
	data->ramp_down_start = 0;
	data->ramp_start_time = 0;
	data->current_power = 0;
	data->current_set_point = 0;
	setIntValue(scope, "driver.VALUE", output_scaled(data, 0) );
	if (DEBUG_MODE) 
	    fprintf(data->logfile, "output driver set to %ld\n", output_scaled(data, 0) );
}

static void stop(struct PIDData*data, void *scope) {
	if (DEBUG_MODE) fprintf(data->logfile,"%s stop command\n", data->conveyor_name);
	halt(data, scope);
	data->stop_marker = 0;
	//data->last_stop_position = 0;
	setIntValue(scope, "SetPoint", 0);
	data->state = cs_stopped;
	data->curr_seek_power = 20;
	if (data->state == cs_interlocked) {
		if (DEBUG_MODE)
			fprintf(data->logfile,"%s interlocked\n", data->conveyor_name);
	}
	else {
		if (DEBUG_MODE)
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

double getStoppingTime(struct PIDData *data) {
	/* work out what stopping time value to use */
	double stopping_time = 0.0;
	if (data->stopping_time) stopping_time = (double) *data->stopping_time / 1000.0;
	if (*data->stop_position >= *data->position && data->fwd_stopping_time) {
		/*if (DEBUG_MODE && data->speed != 0) 
		    fprintf(data->logfile,"using forward stopping time %ldms\n", *data->fwd_stopping_time);
	    */
		stopping_time = ( (double)*data->fwd_stopping_time) / 1000.0;
	}
	else if (*data->stop_position < *data->position && data->rev_stopping_time) {
		/*if (DEBUG_MODE && data->speed != 0) 
		    fprintf(data->logfile,"using reverse stopping time %ldms\n", *data->rev_stopping_time);
		*/
		stopping_time = ( (double)*data->rev_stopping_time) / 1000.0;
	}
	/* we regard a stopping time that is less than one polling cycle to be zero
	    but return the value 1ms to avoid worrying about divide by zero issues */
	if ( stopping_time*1000000 <= data->delta_t) stopping_time = 0.001;
	/*if (DEBUG_MODE) fprintf(data->logfile, "stopping time: %0.3f\n", stopping_time);*/
	return stopping_time;
}

static void get_speed(struct PIDData *data) {
	/*	if (DEBUG_MODE)
		data->speed = 1000000 * rateDebug(data->samples);
	 else
	 */
	data->speed = 1000000 * rate(data->samples);
	if ( labs(data->speed) > 30000 && DEBUG_MODE) {
	    fprintf(data->logfile, "Bad speed: %ld, sample size: %d\n", data->speed, length(data->samples));
	    fflush(data->logfile);
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
	return  ( *data->position < *data->stop_position  && *data->stop_position - *data->position <= *data->fwd_tolerance )
	||
	( *data->position > *data->stop_position && *data->position - *data->stop_position <= *data->rev_tolerance );
}

int overshot(struct PIDData *data) {
    /* overshot the target? */
    if (data->state != cs_position) return 0;
    int res =  data->state == cs_position 
        && (        ( *data->stop_position > *data->position && data->speed < 2 * *data->rev_crawl_speed )
                ||  ( *data->stop_position < *data->position && data->speed > 2 * *data->fwd_crawl_speed ) );
    if (res && DEBUG_MODE) 
	    fprintf(data->logfile,"%s overshot speed: %ld pos: %ld stop: %ld\n", 
	        data->conveyor_name, data->speed, *data->position, *data->stop_position);
	return res;
}

long getStoppingDistance(struct PIDData *data, double stopping_time ) {
    if ( stopping_time <= 0.0) return 0;
    return data->speed * ( stopping_time + data->latency );
}

int position_close(struct PIDData *data, double stopping_time, long stopping_dist) {
    /* note that *data->stopping time is in ms but stopping time passed in is in secs (float) */
    if (data->state != cs_position && data->state != cs_atposition) return 0;
    int res = 
           within_tolerance(data) 
        || /* travelling forwards and running out of time to stop */
	       (*data->stop_position > *data->position 
	            && *data->stop_position <= *data->position + stopping_dist )
	    || /* travelling backwards and running out of time to stop */
	       (*data->stop_position < *data->position 
	            && *data->stop_position >= *data->position + stopping_dist );
	            
	if (DEBUG_MODE) {
	    if (!res) {
	        if ( data->speed == 0)
	            fprintf(data->logfile, "@%s %s not close, not moving\n", show_state(data->state), show_istate(data->sub_state));
	        else
    	        fprintf(data->logfile, "@not close to stopping position. stopping distance: %ld, time: %ldms\n", 
    	        stopping_dist, (long)((double)stopping_dist / (double)data->speed * 1000));
    	}
    	else {
	        if ( data->speed == 0)
	            fprintf(data->logfile, "@%s %s not close, not moving\n", show_state(data->state), show_istate(data->sub_state));
	        else
    	        fprintf(data->logfile, "@needs to stop. stopping time: %.3lfms, stopping distance: %ld, time: %ldms\n", 
    	        stopping_time*1000, stopping_dist, (long)(stopping_time *1000.0));
        }
    } 
	return res;
}

int reposition(struct PIDData *data) {
	return labs(*data->position - *data->stop_position) > 2.0 * (*data->fwd_tolerance + *data->rev_tolerance);
	//return !within_tolerance(data);
}

int stopped(struct PIDData *data) {
    return data->speed == 0;
}

int aiming_forwards(struct PIDData *data) {
    if (data->state == cs_speed ) return *data->set_point > 0;
    if (data->state == cs_position) return *data->stop_position > *data->position;
    if ( (data->state == cs_ramp || data->sub_state == is_ramp) && data->ramp.target > 0) return 1;
    return 0;
}

int aiming_backwards(struct PIDData *data) {
    if (data->state == cs_speed ) return *data->set_point < 0;
    if (data->state == cs_position) return *data->stop_position < *data->position;
    if ( (data->state == cs_ramp || data->sub_state == is_ramp) && data->ramp.target < 0) return 1;
    return 0;
}

int position_homing(struct PIDData *data) {
	if (*data->position <= *data->stop_position)
		return labs(*data->stop_position - *data->position - *data->fwd_tolerance) < 50;
	else
		return labs(*data->position - *data->stop_position + *data->rev_tolerance) < 50;
}

static void update_forward_power_offset(struct PIDData *data, void *scope) {

    if (!power_valid( data, data->current_power) ) {
        if (DEBUG_MODE)
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
        if (DEBUG_MODE)
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

long handle_prestart_calculations(struct PIDData *data, void *scope, enum State new_state, long new_power) {
    double set_point = *data->set_point;
	if (DEBUG_MODE)
		fprintf(data->logfile,"processing prestart calculations\n");

    if (data->current_power == 0.0) {
		data->current_power = sign(set_point);   
    }
	else if (data->current_power != 0.0 && sign(set_point) != sign(data->current_power)) {
		if (DEBUG_MODE) {
			fprintf(data->logfile , "%s current power is %8.3lf when set point is %lf8.3 starting up. resetting current power\n",
				data->conveyor_name, data->current_power, set_point);
		}
		data->current_power = sign(set_point);
	}
	
	/* have we found forward movement during prestart? */
	if (set_point > 0 && *data->position - data->start_position > 20) { /* fwd motion */
		if (data->state == cs_position) {
			data->sub_state = is_ramp;
			if (DEBUG_MODE) fprintf(data->logfile,"ramping from prestart\n");
   			init_ramp(data, &data->ramp, data->current_set_point, *data->set_point, *data->fwd_startup_time);
		}
		else if (data->state == cs_speed) {
			data->state = cs_ramp; 
 			data->ramp_start_time = 0;
			data->last_position = *data->position;
			if (DEBUG_MODE) fprintf(data->logfile,"ramping from prestart\n");
    		init_ramp(data, &data->ramp, data->current_set_point, *data->set_point, *data->fwd_startup_time);
		}
		/* update forward power offset */
		update_forward_power_offset(data, scope);

		if (DEBUG_MODE)
			fprintf(data->logfile,"@fwd movement started. time: %ld power: %ld\n",
					(long)data->fwd_start_delay, *data->fwd_start_power);
	}

	/* have we found reverse movement during prestart */
	else if (set_point < 0 && *data->position - data->start_position < -20) { /* reverse motion */
		if (data->state == cs_position) {
			data->sub_state = is_ramp;
			if (DEBUG_MODE) fprintf(data->logfile,"ramping from prestart\n");
			init_ramp(data, &data->ramp, data->current_set_point, -labs(*data->set_point), *data->rev_startup_time);		
		} else if ( data->state == cs_speed) {
			data->state = cs_ramp;
			data->ramp_start_time = 0;
			data->last_position = *data->position;
			if (DEBUG_MODE) fprintf(data->logfile,"ramping from prestart\n");
			init_ramp(data, &data->ramp, data->current_set_point, - labs(*data->set_point), *data->rev_startup_time);
		}
		update_reverse_power_offset(data, scope);
		if (DEBUG_MODE)
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
		if (DEBUG_MODE)
		 fprintf(data->logfile,"%s waiting for movement. time: %ld power: %ld\n",
				 data->conveyor_name, (long)(data->now_t - data->start_time), new_power);
	}
	return new_power;
}

static void handle_state_change(struct PIDData *data, enum State new_state) {
	/* starting to move from stopped state -> check the prestart values also load the power scale from the user  */
	if ( new_state == cs_speed && (data->state == cs_stopped || data->state == cs_atposition) ) {
	    
	    data->state = cs_speed;
	    data->sub_state = is_prestart;
	    data->start_time = data->now_t;
	    
	    data->fwd_power_rate = (double)*data->fwd_power_scale / 1000.0;
	    data->rev_power_rate = (double)*data->rev_power_scale / 1000.0;
	    
	    if (*data->set_point > 0) {
			if (DEBUG_MODE) fprintf(data->logfile,"@ramping forward because of state change to speed\n");
    	    init_ramp(data, &data->ramp, data->current_set_point, *data->set_point, *data->fwd_startup_time);
	    }
	    else if (*data->set_point < 0) {   
			if (DEBUG_MODE) fprintf(data->logfile,"@ramping reverse due to state change to speed\n");
	        init_ramp(data, &data->ramp, data->current_set_point, *data->set_point, *data->rev_startup_time);
    	}

    }
    else if (new_state != data->state && new_state == cs_atposition) {
        if (data->state == cs_stopped) {
            if (DEBUG_MODE) fprintf(data->logfile, "@atposition");
            data->state = cs_atposition; data->sub_state = is_holding; 
        }
        else new_state = cs_position;
    }
    if (new_state != data->state && new_state == cs_position ) {
        if (DEBUG_MODE) fprintf(data->logfile, "@seeking");
        if (data->state == cs_stopped) data->sub_state = is_stopped;
        data->state = cs_position;
    }
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

	/* the counter provides a sample time and raw data value that we use 
		for estimating the device speed.  The time values are not
		related to the system time. We store the system time of
		the last change so that we can check if input counts are
		still arriving. If they are not, speed is zero.
	*/
#ifndef USE_IOTIME
	if (now_t - data->last_sample_time >= 10000) {
		addSample(data->samples, now_t, *data->position);
		data->last_sample_time = now_t;
		data->last_sample_time_system = now_t;
	}
#else
	{
		/* the simulation doesn't support IOTIME and IOVALUE so check for that here. */
		long t;
		if (data->io_read_time) 
			t = *data->io_read_time;
		else
			t = now_t;
		if ( t != data->last_sample_time ) {
			addSample(data->samples, t, *data->position);
			data->last_sample_time = t;
			data->last_sample_time_system = now_t;
		}
		else {
			/* the io hasn't been updated which means that the value hasn't changed
				so we just add a sample to reflect that 
			*/
			uint64_t t = getIOClock();
			if (t > (uint64_t)data->last_sample_time) {
				addSample(data->samples, t, *data->position);
				data->last_sample_time = t;
				data->last_sample_time_system = now_t;
			}
		}
	}
#endif

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
	if (DEBUG_MODE) 
    	fprintf(data->logfile,"%s copied current power (%8.3lf) to new power\n", data->conveyor_name, data->current_power);
	*/
	enum State new_state = data->state;

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
	    if (DEBUG_MODE) fprintf(data->logfile, "@interlocked\n");
		if (data->state != cs_interlocked)  {
			stop(data, scope);
			data->state = cs_interlocked;
		}
		goto done_polling_actions;
	}

	if (new_state == cs_stopped) {
	  if (data->state != cs_stopped || data->sub_state != is_stopped) {

	    if (DEBUG_MODE) fprintf(data->logfile, "@stopped\n");

        if (data->current_power != 0 ||*data->set_point != 0 
    	        || data->sub_state != is_stopped || data->state != cs_stopped) {
        		data->sub_state = is_stopped;
        		stop(data, scope);
    		}
			new_power = 0;
			if (DEBUG_MODE) init_stats(statistics_scope);
    		data->state = cs_stopped;
    		data->sub_state = is_stopped;
        	goto calculated_power;
		}
		return PLUGIN_COMPLETED;
	}
	
	/* check for other state changes and swith data->state and data->sub_state as necessary */
    if (new_state != data->state) handle_state_change(data, new_state);
    
    /* stop position has changed */
    if ( *data->stop_position != data->last_stop_position ) {
    
        if (DEBUG_MODE) fprintf(data->logfile,"@changed stop position\n");

        data->last_stop_position = *data->stop_position;

        if (data->state == cs_position || data->state == cs_stopping || data->state == cs_atposition ) {
        
            long distance_to_stop = *data->stop_position - *data->position;
    
            /* new position is close */
            if (within_tolerance(data)) {
            	if (DEBUG_MODE) fprintf(data->logfile,"@within tolerance\n");
        		new_power = 0.0;
           		data->current_set_point = 0.0;
           		data->state = cs_position;
           		data->sub_state = is_stopping;
            }
            else if ( labs(distance_to_stop) < 400) {
                if (DEBUG_MODE) fprintf(data->logfile,"stop position has changed a little - creeping");
                data->state = cs_stopping;
                data->sub_state = is_stopping;
            }
            /* new position is foward */
            else if ( distance_to_stop > 0 ) {
    			if (DEBUG_MODE) fprintf(data->logfile,"@ramping forward for stop pos change\n");
            	data->state = cs_position;
				data->sub_state = is_ramp;
                init_ramp(data, &data->ramp, data->speed, labs(*data->set_point), *data->fwd_ramp_time);
            }
            /* new position is behing */
            else if ( distance_to_stop < 0 )  {
    			if (DEBUG_MODE) fprintf(data->logfile,"@ramping reverse for stop pos change\n");
        		data->state = cs_position;
				data->sub_state = is_ramp;
                init_ramp(data, &data->ramp, data->speed, -labs(*data->set_point), *data->rev_ramp_time);
            }
        }
    }
    
    /* moving or seeking but set point has changed.
    
        We only adjust behaviour for the new set point when driving at a speed 
        or when seeking and not already stopping or close. 
    */
    if ( *data->set_point != data->saved_set_point 
            && ( data->state == cs_speed || 
                ( data->state == cs_position && 
                    ( data->sub_state == is_speed /* positioning and moving */
                        /*or positioning and ramping to the old set point */
                    || ( data->sub_state == cs_ramp && data->ramp.target == data->saved_set_point )
                    ) 
                )
            )
     ) {
        if (data->state == cs_stopped) {
            data->state = cs_speed;
            data->sub_state = is_prestart;
            data->start_time = data->now_t;
        }
        else if ( data->state == cs_speed || data->state == cs_ramp){
            data->state = cs_ramp;
            data->sub_state = is_ramp;
        }
        else { /* positioning */
            long distance_to_stop = *data->stop_position - *data->position;
            if ( labs(distance_to_stop) < 400) {
                if (DEBUG_MODE) fprintf(data->logfile,"%s %s set point has changed but too close (%ld) to use it", 
                    show_state(data->state), show_istate(data->sub_state), distance_to_stop);
                data->state = cs_stopping;
                data->sub_state = is_stopping;
            }
            else
                data->sub_state = is_ramp;
        }
        
   	    data->saved_set_point = *data->set_point;   
   	    
   	    /* if we should be ramping, adjust the ramp now */
   	    if (data->sub_state == is_ramp) {
            long set_point = *data->set_point;
        
            /* if positioning, make sure the new set point points in the right direction */
            if (data->state == cs_position && *data->stop_position > *data->position) set_point = labs(set_point);
            else if (data->state == cs_position && *data->stop_position < *data->position) set_point = -labs(set_point);
        
    	    if (set_point > 0) {
    			if (DEBUG_MODE) fprintf(data->logfile,"ramping fwd due to set speed change\n");
        	    init_ramp(data, &data->ramp, data->speed, set_point, *data->fwd_ramp_time);
    	    }
    	    else if (set_point < 0) {   
    			if (DEBUG_MODE) fprintf(data->logfile,"ramping rev due to set speed change\n");
        	    init_ramp(data, &data->ramp, data->speed, set_point, *data->rev_ramp_time);
        	}
    	    else {
    	        if (data->speed > 0) {
    			    if (DEBUG_MODE) fprintf(data->logfile,"ramping to stop from fwd\n");
            	    init_ramp(data, &data->ramp, data->speed, 0, *data->fwd_stopping_time);
            	}
            	else if (data->speed < 0){
    			    if (DEBUG_MODE) fprintf(data->logfile,"ramping to stop from rev\n");
            	    init_ramp(data, &data->ramp, data->speed, 0, *data->rev_stopping_time);    
            	}
        	}
    	}
    }
    
    if (data->sub_state == is_prestart ) {
        new_power = handle_prestart_calculations(data, scope, new_state, new_power);
        if ( data->sub_state == is_prestart ) goto calculated_power;
	}

	
	/* should be at position but we have moved away */
	if (data->state == cs_atposition && !within_tolerance(data)) {
		if (DEBUG_MODE) fprintf(data->logfile,"@at position but not within tolerance\n");
    	data->state = cs_position;
    	changeState(scope, "seeking");
    	if ( labs( *data->position - *data->stop_position) > 400) {
    	    data->sub_state = is_ramp;
    	    if ( *data->position < *data->stop_position) {
			    if (DEBUG_MODE) fprintf(data->logfile,"ramping forward to fix drift\n");
    	        init_ramp(data, &data->ramp, 0, labs(*data->set_point), *data->fwd_startup_time);
    	    }
    	    else if ( *data->position > *data->stop_position) {
     			if (DEBUG_MODE) fprintf(data->logfile,"ramping rev to fix drift\n");
       	        init_ramp(data, &data->ramp, 0, -labs(*data->set_point), *data->rev_startup_time);
    	    }
    	}
    	else {
		    if (DEBUG_MODE) fprintf(data->logfile,"@short drift, crawling a little\n");
    	    data->sub_state = is_stopping;
    	}
	}
	
	if (data->state == cs_position) {
    	if ( overshot(data) )  {
    	    if (DEBUG_MODE)
    	        fprintf (data->logfile, "%s detected overshoot\n", data->conveyor_name);
    	    data->sub_state = is_stopping;
    	    if (*data->position < *data->stop_position) {
    			new_power = data->crawl_adjustment;
    			data->current_set_point = new_power / data->fwd_power_rate;
            }
    	    else {
    			new_power = -data->crawl_adjustment;
    			data->current_set_point = new_power / data->rev_power_rate;
            }
    	}
    	else if (within_tolerance(data)) {
        	if (DEBUG_MODE) 
        	            fprintf(data->logfile,"@within tolerance\n");
    		new_power = 0.0;
       		data->current_set_point = 0.0;
       		data->sub_state = is_stopping;
    	}
    	else /* are we positioning but not moving fast enough? */
    	    if ( data->sub_state != is_ramp && 
        	     ( ( aiming_forwards(data) && data->speed < *data->fwd_crawl_speed )
        	       || ( aiming_backwards(data) && data->speed > *data->rev_crawl_speed ) ) ) 
        	{
                if ( labs(*data->stop_position - *data->position) > 400 && data->sub_state != is_ramp ) {
    		        if (DEBUG_MODE) 
    		            fprintf(data->logfile,"@positioning %s %s but too slow - ramping\n", 
        		            show_state(data->state), show_istate(data->sub_state));
                    data->sub_state = is_ramp;
                    if (aiming_forwards(data))
                        init_ramp(data, &data->ramp,  *data->fwd_crawl_speed, 5 * *data->fwd_crawl_speed, 150);
                    else 
                        init_ramp(data, &data->ramp,  *data->rev_crawl_speed, 5 * *data->rev_crawl_speed, 150);
                }
                else {
        		    if (DEBUG_MODE) fprintf(data->logfile,"@positioning %s %s but too slow - short drift, crawling a little\n",
        		        show_state(data->state), show_istate(data->sub_state));
            	    data->sub_state = is_stopping;
                }
            }
	}
	
	if (data->state == cs_stopping 
			|| (data->state == cs_position
				&& (data->sub_state == is_stopping || data->sub_state == is_stopped)) 
			|| data->state == cs_atposition
			|| overshot(data)
		) {
		/* note that 'stopping_distance' is the distance we will travel before stopping when ramping down from this speed */
    	double stopping_time = getStoppingTime(data);
    	
    	long stopping_distance = getStoppingDistance(data, stopping_time);
    	
		/* within tolerance - stop immediately */
		if (within_tolerance(data) ) {
			if (DEBUG_MODE && new_power != 0) 
		        fprintf(data->logfile,"@stopping within tolerance\n");
			data->current_set_point = 0.0; // 
			new_power = 0.0;
			if (*data->position == data->last_position) {
				if (data->state != cs_atposition) {
    				atposition(data, scope);
    				data->sub_state = is_stopped;
    				if (DEBUG_MODE) 
    		            fprintf(data->logfile,"@stopped at %ld\n",*data->position);
				}
			}
		}
		/* check for crawling backwards and need to adjust driving speed */
		else if (*data->position > *data->stop_position) {
	        if ( data->state == cs_atposition ) data->state = cs_position;
		    data->sub_state = is_stopping;
		    
		    if ( data->speed > *data->rev_crawl_speed 
		        || ( *data->position - 4 * stopping_distance - 100 > *data->stop_position + *data->rev_tolerance) ) {
		        
		        /* do not allow this to ramp too hard */
		        if (data->last_position == *data->position || new_power > -500) {
        		    new_power -= data->crawl_adjustment;
        			data->current_set_point = new_power / data->rev_power_rate;
    			}
    			if (DEBUG_MODE) 
    	            fprintf(data->logfile,"@decel forward %ld to power %.3lf remaining dist: %ld\n", 
    	                data->last_position - *data->position, new_power, *data->position - *data->stop_position);
    	    }
    	    else { 
    	    	if (DEBUG_MODE) 
    	            fprintf(data->logfile,"@crawling back; too close to increase speed\n");
    	            
    	        /* do not allow this to change direction */
    	        if (new_power < -2 * data->crawl_adjustment) new_power += data->crawl_adjustment;
    	    }
		}
		/* check for crawling forwards and need to adjust driving speed */
		else if ( *data->position < *data->stop_position ) {
		    if ( data->state == cs_atposition ) data->state = cs_position;
    		data->sub_state = is_stopping;

		    /* do not allow this to ramp too hard */
		    if ( data->speed < *data->fwd_crawl_speed 
		            || (*data->position + 4 * stopping_distance + 100 < *data->stop_position - *data->fwd_tolerance) ) {
		        if (data->last_position == *data->position || new_power < 500 ) {
        		    new_power += data->crawl_adjustment;
        			data->current_set_point = new_power / data->fwd_power_rate;
    			}
    			if (DEBUG_MODE) 
    	            fprintf(data->logfile,"@accel forward %ld to power %.3lf remaining dist: %ld\n", 
    	                *data->position - data->last_position, new_power, *data->stop_position - *data->position);
    	    }
    	    else { 
    	    	if (DEBUG_MODE) 
    	            fprintf(data->logfile,"@crawling forward; too close to increase speed\n");
    	        if (new_power > 2 * data->crawl_adjustment) new_power -= data->crawl_adjustment;
    	    }
		}
	}
	else {
		if ( data->state == cs_position ) 
		{ 
        	double stopping_time = getStoppingTime(data);
        	long stopping_dist = getStoppingDistance(data, stopping_time);
		    int close_to_target = position_close(data, stopping_time, stopping_dist );
		    
		    /* currently stopped but need to reposition because we are not at the stop position */
    		if (data->sub_state == is_stopped && reposition(data)  )
    		{
    			data->sub_state = is_ramp;
    			if (*data->stop_position > *data->position) {
			        if (DEBUG_MODE) fprintf(data->logfile,"ramping to reposition fwd\n");
    				init_ramp(data,  &data->ramp, 0.0, labs(*data->set_point), *data->fwd_ramp_time );
    			}
    			else {
			        if (DEBUG_MODE) fprintf(data->logfile,"ramping to reposition rev\n");
    				init_ramp(data, &data->ramp,  0.0, -labs(*data->set_point), *data->rev_ramp_time);
    			}
    			if (DEBUG_MODE) 
    		        fprintf(data->logfile,"@seeking using ramp to %ld\n", *data->set_point);
    		}
    		
    		/* ramping up and seeking - abort the ramp and ramp down. */
    		if (data->sub_state == is_ramp  && close_to_target
    		        && fabs(data->ramp.target) > fabs(data->ramp.start_point)) {
    		    if (DEBUG_MODE)  fprintf(data->logfile, "@ramping-up abort and stop\n");
    		    init_ramp(data, &data->ramp, data->current_set_point, 0.0, stopping_time * 1000 );
    		}

    		/* seeking and getting close to the target */
    		if ( data->sub_state != is_stopped && data->sub_state != is_ramp && close_to_target  )
    		{
    			data->sub_state = is_ramp;
    			if ( *data->position < *data->stop_position) {
			        if (DEBUG_MODE) fprintf(data->logfile,"positioning starting a fwd ramp - close to target\n");
        			init_ramp(data, &data->ramp, data->current_set_point, 
    			            2.0 * *data->fwd_crawl_speed, stopping_time * 1000 );
    			}
    			else {
        			if (DEBUG_MODE) fprintf(data->logfile,"positioning starting a rev ramp - close to target\n");
        			init_ramp(data, &data->ramp, data->current_set_point, 
    			            2.0 * *data->rev_crawl_speed, stopping_time * 1000 );
    			}
    			if (DEBUG_MODE) 
    		        fprintf(data->logfile,"@slowing\n");
    		}
    		
    		/* ramping down while seeking.  If we're seeking a position but ramping up, 
    		    it's probably due to a speed change and we ignore it.  If we are ramping 
    		    down to a slow speed (arbitrarily 4*crawl) this section assumes it is because
    		    we want to stop soon and tries to adjust things so that the stop happens
    		    close to the recorded stop position.
    		    
    		    It is more important to stop at the position than to meet the timing requirement.
    		    
    		*/
    		
    		/*
    		if (data->sub_state == is_ramp && *data->stop_position > *data->position 
    		        &&  labs(data->speed) < *data->fwd_crawl_speed
    		        && *data->stop_position - *data->position < 400)
        		) {
    		    
				data->sub_state = is_stopping;
				data->current_set_point = 2 * *data->fwd_crawl_speed;
				new_power = data->fwd_power_rate * data->current_set_point;
			}
			else 
			
    			if (data->sub_state == is_ramp 
    			        && *data->stop_position < *data->position 
    			        &&  labs(data->speed) > *data->rev_crawl_speed) {
    				data->sub_state = is_stopping;
     				data->current_set_point = 2 * *data->rev_crawl_speed;
    				new_power = data->rev_power_rate * data->current_set_point;
    			}
    		else
    		*/ 
    		
    		if ( data->sub_state == is_ramp 
    		    && fabs(data->ramp.target) < 4.0 * *data->fwd_crawl_speed 
    			&&  !close_to_target  ) /* at the current deceleration we will stop before target */
    		{	
				long remaining_dist = labs(*data->stop_position - *data->position);
				double remaining_time = ( labs(data->speed) > *data->fwd_crawl_speed) ? (double)remaining_dist / (double)data->speed : 0.1;

                /* adjust the ramp to compensate for under powering */
			    if (DEBUG_MODE) {
    		        fprintf(data->logfile,"@remaining dist %ld at %ld, time(ms): %ld adjusting ramp\n", 
    		            labs(*data->position - *data->stop_position), data->speed, (long)(remaining_time*1000) ); 
		        }
		        
		        /*
		         If we have started ramping down already but we are quite 
		         a way from home it is  worth aborting the ramp and
		         going back to speed mode. Otherwise we just slow the ramp down by calculating
		         the time it will take to travel the remaining distance.
		        */
		        if (remaining_time - stopping_time > 0.1) {
    		        data->sub_state = is_speed;
    		        if (data->state == cs_ramp) { data->state = cs_speed; data->last_control_change = now_t; }
		        }
		        else
        			init_ramp(data, &data->ramp, data->current_set_point, data->ramp.target, remaining_time * 1000 );

	    		/* or should we begin crawling? if we are travelling too slow already just set ourselves to crawl mode
	    		 */
                if ( ramp_done(&data->ramp) &&  *data->position < *data->stop_position && data->speed < *data->fwd_crawl_speed ) {
                    if (DEBUG_MODE) fprintf(data->logfile, "switching from ramp to stopping fwd since ramp finished");
					data->state = cs_stopping;
					data->sub_state = is_stopping;
					data->current_set_point = *data->fwd_crawl_speed;
					new_power = data->fwd_power_rate * data->current_set_point;
                }
                else if ( ramp_done(&data->ramp) && *data->position > *data->stop_position && data->speed > *data->rev_crawl_speed ) {
                    if (DEBUG_MODE) fprintf(data->logfile, "switching from ramp to stopping rev since ramp finished");
					data->state = cs_stopping;
					data->sub_state = is_stopping;
					data->current_set_point = *data->fwd_crawl_speed;
					new_power = data->rev_power_rate * data->current_set_point;
                }               
    		}
    		if ( data->state == cs_position && data->sub_state != is_stopping && position_homing(data)  ) {
    			data->state = cs_stopping;
    			data->sub_state = is_stopping;
    			data->current_set_point = (*data->stop_position > *data->position) ? *data->fwd_crawl_speed : *data->rev_crawl_speed;
    			new_power = data->fwd_power_rate * data->current_set_point;
    			if (DEBUG_MODE) 
    		        fprintf(data->logfile,"@stopping when close to tolerance window\n");
    		}
    		
    	}
		if (data->state == cs_ramp || (data->state == cs_position && data->sub_state == is_ramp) ) {
			if (DEBUG_MODE) 
		        fprintf(data->logfile,"@ramping \n");
			double set_point = data->current_set_point;
		
		    /* positioning and slowing down */
			if ( data->state == cs_position 
			        && fabs(data->ramp.target) < 100) { /* stopping */
			        
			    /* since we are trying to seek a position, do not allow the ramp down to get too slow. */
			        
			    /* going forwards and in the last cycle we moved quite far */
				if ( *data->position < *data->stop_position 
				        && *data->position - data->last_position > *data->fwd_tolerance/2) {
					set_point = adjust_set_point_ramp(data, &data->ramp);
					data->current_set_point = set_point;
                    new_power = adjust_power_ramp(data, &data->ramp);
				}
				else    /* going backwards and in the last cycle we moved quite far */
				    if ( *data->position > *data->stop_position && data->last_position - *data->position > *data->rev_tolerance/2 ) {
    					set_point = adjust_set_point_ramp(data, &data->ramp);
    					data->current_set_point = set_point;
                        new_power = adjust_power_ramp(data, &data->ramp);
				}
				
			}
			else {
				set_point = adjust_set_point_ramp(data, &data->ramp);
				data->current_set_point = set_point;
                new_power = adjust_power_ramp(data, &data->ramp);
			}
			
            /* when accelerating from a stop, retain the power rate used to get to the target speed */
			if ( fabs(data->ramp.start_point) < 200.0 && fabs(data->ramp.target) >= 2000.0 ) // accelerating
			{
			    //if (DEBUG_MODE) {fprintf(data->logfile, "accelerating\n"); }
			    
			    /* if the current speed is close to the target set point, stop ramping */
			    if ( fabs( (double)data->speed) >= 0.80 * fabs(data->ramp.target) || ramp_done(&data->ramp) ) {
			    
			        if (DEBUG_MODE) { fprintf(data->logfile, "at speed\n"); }
			        
			        /* update the driving state to switch out of the ramp */
    				if ( data->state == cs_ramp ) { data->state = cs_speed; data->last_control_change = now_t; }
    				data->sub_state = is_speed;
    				
                    /* we are at speed, adjust the power rate */
#if 0
                    if ( labs(data->speed) > 800 ) { /* ignore low speeds */
        		        double power = getBufferValueAt(data->power_records, (long)now_t - 100000);
        		        double power_rate = power / data->speed;
        		        if (data->speed > 0) {
        		            if (power_rate > 1.02 * data->fwd_power_rate) power_rate = 1.02 * data->fwd_power_rate;
            		        if (DEBUG_MODE) {
            		            fprintf(data->logfile, "%s calculated forward power ratio %8.3lf from power %8.3lf\n", 
            		                data->conveyor_name, power_rate, power);
            		        }
            		        addSample(data->fwd_power_rates, now_t, data->fwd_power_rate);
    		            }
        		        else {
        		            if (power_rate > 1.02 * data->rev_power_rate) power_rate = 1.02 * data->rev_power_rate;
            		        if (DEBUG_MODE) {
            		            fprintf(data->logfile, "%s calculated reverse power ratio %8.3lf from power %8.3lf\n", 
            		                data->conveyor_name, power_rate, power);
            		        }
            		        addSample(data->rev_power_rates, now_t, data->rev_power_rate);
        		        }
    		        }
#endif
    			    data->current_set_point = data->ramp.target;
        			if (DEBUG_MODE) {
            		    fprintf(data->logfile, "%s end of ramp: set point is now %ld\n", data->conveyor_name, data->current_set_point);
        		    }
			    }
			    else if ( ramp_done(&data->ramp) ) {
			        /* update the driving state to switch out of the ramp */
    				if ( data->state == cs_ramp ) { data->state = cs_speed; data->last_control_change = now_t; }
    				data->sub_state = is_speed;
			    }
			}
			else if ( fabs(data->ramp.target) > fabs(data->ramp.start_point) ) {
			    //if (DEBUG_MODE) {fprintf(data->logfile, "accelerating\n"); }

				/* if the current speed is close to the target set point, stop ramping */
			    if ( fabs( (double)data->speed) >= 0.75 * fabs(data->ramp.target) ) {
    				if ( data->state == cs_ramp ) { data->state = cs_speed; data->last_control_change = now_t; }
    				data->sub_state = is_speed;
    			    data->current_set_point = data->ramp.target;
        			if (DEBUG_MODE) {
            		    fprintf(data->logfile, "%s end of ramp: set point is now %ld\n", data->conveyor_name, data->current_set_point);
        		    }
			    }
			}
			else if ( fabs(data->ramp.target) < fabs(data->ramp.start_point) ) {
			    //if (DEBUG_MODE) {fprintf(data->logfile, "decelerating\n"); }

				/* if the current speed is close to the target set point, stop ramping */
			    if ( fabs( (double)data->speed) <= 1.20 * fabs(data->ramp.target) ) {
    				if ( data->state == cs_ramp ) { data->state = cs_speed; data->last_control_change = now_t; }
    				data->sub_state = is_speed;
    			    data->current_set_point = data->ramp.target;
        			if (DEBUG_MODE) {
            		    fprintf(data->logfile, "%s end of ramp: set point is now %ld\n", data->conveyor_name, data->current_set_point);
        		    }
			    }
			}
#if 0	
            /* new_power is now directly set by a ramp adjustment */
			new_power = data->power_rate * set_point;
#endif
		}
		
		data->Ep = data->current_set_point - data->speed;
		data->Ei = data->total_err + data->Ep * dt * 1000;
		data->last_filtered_Ep = data->filtered_Ep;
		data->filtered_Ep = 0.8*data->Ep + 0.2*data->last_Ep;
		data->Ed = (data->filtered_Ep - data->last_filtered_Ep) / dt / 1000;
		data->last_Ep = data->Ep;
		
		
		/* This is where we try to achieve the requested speed. As long as we are travelling
		    too slowly, the speed is increased (every cycle) by 2 x crawl adjustment. 
		    and we are too fast, it is reduced by the same amount.
		    
		    We have a +/- 5% window around the target speed where no control adjustments are attempted.
		*/
		
		if ( (data->state == cs_speed || ( data->state == cs_position && data->sub_state == is_speed) )
		        && labs(data->speed) >= 300 && labs(*data->set_point) >= 1000) {
		        
		    if (now_t - data->last_control_change > 100000 ) {
		        double ratio = (double)data->speed / (double)*data->set_point;
		        double speed = data->speed;
		        if (ratio < 0.90) {
		            if (speed > 0.0) {
    		            new_power = (double)*data->set_point * data->fwd_power_rate + 2.0 * data->crawl_adjustment;
    		            data->fwd_power_rate = new_power / (double)*data->set_point;
    		            setIntValue(scope, "fwd_settings.PowerScale", (long) (data->fwd_power_rate * 1000.0));
		                if (DEBUG_MODE) { fprintf(data->logfile, "increased power scale to %ld\n", *data->fwd_power_scale); }
    		        }
    		        else {
    		            new_power = (double)*data->set_point * data->rev_power_rate - 2.0 * data->crawl_adjustment;
    		            data->rev_power_rate = new_power / (double)*data->set_point;
    		            setIntValue(scope, "rev_settings.PowerScale", (long) (data->rev_power_rate * 1000));
		                if (DEBUG_MODE) { fprintf(data->logfile, "increased power scale to %ld\n", *data->rev_power_scale); }
       		        }
    		        data->last_control_change = now_t;
		        }
		        else if (ratio > 1.05) {
		            if (data->speed > 0) {
    		            new_power = (double)*data->set_point * data->fwd_power_rate - 2.0 * data->crawl_adjustment;
    		            data->fwd_power_rate = new_power / (double)*data->set_point;
    		            setIntValue(scope, "fwd_settings.PowerScale", (long) (data->fwd_power_rate * 1000));
		                if (DEBUG_MODE) { fprintf(data->logfile, "decreased power scale to %ld\n", *data->fwd_power_scale); }
		            }
    		        else {
    		            new_power = (double)*data->set_point * data->rev_power_rate + 2.0 * data->crawl_adjustment;
    		            data->rev_power_rate = new_power / (double)*data->set_point;
    		            setIntValue(scope, "rev_settings.PowerScale", (long) (data->rev_power_rate * 1000));
		                if (DEBUG_MODE) { fprintf(data->logfile, "decreased power scale to %ld\n", *data->rev_power_scale); }
		            }
    		        data->last_control_change = now_t;
		        }
		    }
		}

		/* calculate PID control if we are ramping, driving or seeking */
		if (data->state == cs_speed || data->state == cs_ramp 
		        || (data->state == cs_position && (data->sub_state == is_ramp || data->sub_state == is_speed ) ) ) {
		     
			double Kp = data->Kp;
			double Ki = data->Ki;
			double Kd = data->Kd;
		     
		     if (data->use_Kpidf && data->speed > 0) { 
		        Kp = data->Kpf; Ki = data->Kif; Kd = data->Kdf;
		     } 
		     else if (data->use_Kpidr && data->speed< 0) {
		        Kp = data->Kpr; Ki = data->Kir; Kd = data->Kdr;
		     }
			
#if 0
			correction = data->Ep * Kp + data->Ei * Ki + data->Ed * Kd;
			base = data->power_rate * data->current_set_point;
            /* Not currently applying PID control */
			new_power = base + correction;
						
			/* do not apply a correction if it  causes a change in direction */
			if (new_power < 0 && data->current_set_point >= 0) new_power = base;
			else if (new_power > 0 && data->current_set_point <= 0 ) new_power = base;
			if (DEBUG_MODE) {
            	setIntValue(statistics_scope, "dT", (data->delta_t + 500)/1000 );
            	setIntValue(statistics_scope, "SetPt", data->current_set_point);
            	setIntValue(statistics_scope, "Err_p", data->filtered_Ep);
            	setIntValue(statistics_scope, "Err_i", data->Ei);
            	setIntValue(statistics_scope, "Err_d", data->Ed);
            	setIntValue(statistics_scope, "Base", (long)base);
            	setIntValue(statistics_scope, "Corr", (long)correction);
			}
#endif
		}
	}
		
	addSample(data->power_records, now_t, new_power);
	
	if (DEBUG_MODE) {
    	setIntValue(statistics_scope, "SetPt", data->current_set_point);
    	setIntValue(statistics_scope, "Vel", data->speed);
    	setIntValue(statistics_scope, "Pwr", new_power);
	}
	
	show_status(data, new_power);

	data->last_position =*data->position;

calculated_power:
	if (new_power == 0 || new_power != data->current_power) {
    
    	if (new_power != data->current_power && DEBUG_MODE)
    		fprintf(data->logfile,"%s new power: %8.3lf curr power %8.3lf\n", 
    		    data->conveyor_name, new_power, data->current_power);
    		    
    	if (DEBUG_MODE) setIntValue(statistics_scope, "Pwr", new_power);
		if ( new_power > 0 && new_power > data->current_power + *data->ramp_limit) {
			if (DEBUG_MODE) 
			    fprintf(data->logfile,"warning: %s wanted change of %f6.2, limited to %ld\n",
					data->conveyor_name, new_power - data->current_power, *data->ramp_limit);
			new_power = data->current_power + *data->ramp_limit;
		}
		else if ( new_power < 0 && new_power < data->current_power - *data->ramp_limit) {
			if (DEBUG_MODE) 
			    fprintf(data->logfile,"warning: %s wanted change of %f6.2, limited to %ld\n",
					data->conveyor_name, new_power - data->current_power, - *data->ramp_limit);
			new_power = data->current_power - *data->ramp_limit;
		}
    	long power = 0;
    	if (new_power>0) {
    	    power = output_scaled(data, (long) new_power + *data->fwd_start_power);
    	    addSample(data->power_records, data->now_t, new_power);
    	}
    	else if (new_power<0) {
    	    power = output_scaled(data, (long) new_power + *data->rev_start_power);
    	    addSample(data->power_records, data->now_t, new_power);
    	}
    	else power = output_scaled(data, 0);
/*
    	if (new_power != data->current_power && DEBUG_MODE)
    		fprintf(data->logfile,"%s setting power to %ld (scaled: %ld, fwd offset: %ld, rev offset:%ld)\n",
    				data->conveyor_name, (long)new_power, power, *data->fwd_start_power, *data->rev_start_power);
*/    				
    	setIntValue(scope, "driver.VALUE", power);
    	data->current_power = new_power;
	}
	
done_polling_actions:
	data->last_position = *data->position;
	data->last_poll = now_t;
	if (current) { free(current); current = 0; }
	
	if (DEBUG_MODE) fflush(data->logfile);
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
