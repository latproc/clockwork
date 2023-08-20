//DC-mode wecon vd3e control

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <time.h>
#include <sys/mman.h>
#include <malloc.h>
#include "ecrt.h"
#include <assert.h>



// Application parameters
#define FREQUENCY 500
#define CLOCK_TO_USE CLOCK_REALTIME
#define CONFIGURE_PDOS 1

// Optional features
#define PDO_SETTING1	1

#define NSEC_PER_SEC (1000000000L)
#define PERIOD_NS (NSEC_PER_SEC / FREQUENCY)

#define DIFF_NS(A, B) (((B).tv_sec - (A).tv_sec) * NSEC_PER_SEC + \
	(B).tv_nsec - (A).tv_nsec)

#define TIMESPEC2NS(T) ((uint64_t) (T).tv_sec * NSEC_PER_SEC + (T).tv_nsec)

/****************************************************************************/

// EtherCAT
static ec_master_t *master = NULL;
static ec_master_state_t master_state = {};

static ec_domain_t *domain_r = NULL;
static ec_domain_state_t domain_r_state = {};
static ec_domain_t *domain_w = NULL;
static ec_domain_state_t domain_w_state = {};

static ec_slave_config_t *sc	= NULL;
static ec_slave_config_state_t sc_state = {};
/****************************************************************************/

// process data
static uint8_t *domain_r_pd = NULL;
static uint8_t *domain_w_pd = NULL;
#define wecon		0,0
#define wecon_vd3e 	0x00000eff, 0x10003101

/***************************** 20140224	************************************/

//signal to turn off servo on state
static unsigned int user_interrupt;
static unsigned int deactive;

// offsets for PDO entries
static unsigned int ctrl_word;			
static unsigned int target_pos;	
static unsigned int touch_probe;	
static unsigned int modeofoper;			
static unsigned int interpolateddata;

static unsigned int error_code;			
static unsigned int status_word;			
static unsigned int actual_pos;	
static unsigned int torq_actu_val;		
static unsigned int following_actu_val;	
static unsigned int modeofop_display;	

static signed long temp[8]={};

//tx pdo entry
const static ec_pdo_entry_reg_t domain_r_regs[] = {
 	{wecon,wecon_vd3e,0x6040,00,&ctrl_word},
	{wecon,wecon_vd3e,0x607a,00,&target_pos},
	{wecon,wecon_vd3e,0x60b8,00,&touch_probe},
	{wecon,wecon_vd3e,0x6060,00,&modeofoper},
	{}
};

//rx pdo entry
const static ec_pdo_entry_reg_t domain_w_regs[] = {
 	{wecon,wecon_vd3e,0x603f,00,	&error_code},
 	{wecon,wecon_vd3e,0x6041,00,	&status_word},
	{wecon,wecon_vd3e,0x6064,00,	&actual_pos},
	{wecon,wecon_vd3e,0x6077,00,	&torq_actu_val},
	{wecon,wecon_vd3e,0x60f4,00,	&following_actu_val},
	{wecon,wecon_vd3e,0x6061,00,	&modeofop_display},
	{}
};

float move_value = 0;
static unsigned int counter = 0;
static unsigned int blink = 0;
static unsigned int sync_ref_counter = 0;
const struct timespec cycletime = {0, PERIOD_NS};
long prev_target_pos = 0;

/*****************************************************************************/

#if PDO_SETTING1
/* Master 0, Slave 0, "Wecon VD3E EtherCAT Servo_v1.05"
 * Vendor ID:			 0x00000eff
 * Product code:		0x10003101
 * Revision number: 0x00000069
 */

ec_pdo_entry_info_t slave_0_pdo_entries[] = {
		{0x6040, 0x00, 16}, /* ControlWord */
		{0x607a, 0x00, 32}, /* Target position */
		{0x60b8, 0x00, 16}, /* Touch probe function */
		{0x6060, 0x00, 8}, /* Modes of operation */
		{0x603f, 0x00, 16}, /* Error Code */
		{0x6041, 0x00, 16}, /* statusWord */
		{0x6064, 0x00, 32}, /* Position actual value */
		{0x6077, 0x00, 16}, /* Torque actual value */
		{0x60f4, 0x00, 32}, /* Following error actual value */
		{0x6061, 0x00, 8}, /* Modes of operation display */
};//{index,subindex,lenth}

ec_pdo_info_t slave_0_pdos[] = {
		{0x1701, 4, slave_0_pdo_entries + 0}, /* RxPDO */
		{0x1b01, 6, slave_0_pdo_entries + 4}, /* TxPDO */
};

ec_sync_info_t slave_0_syncs[] = {
		{0, EC_DIR_OUTPUT, 0, NULL, EC_WD_DISABLE},
		{1, EC_DIR_INPUT, 0, NULL, EC_WD_DISABLE},
		{2, EC_DIR_OUTPUT, 1, slave_0_pdos + 0, EC_WD_ENABLE},
		{3, EC_DIR_INPUT, 1, slave_0_pdos + 1, EC_WD_DISABLE},
		{0xff}
};
#endif

/*****************************************************************************/


/*****************************************************************************/

struct timespec timespec_add(struct timespec time1, struct timespec time2)
{
	struct timespec result;

	if ((time1.tv_nsec + time2.tv_nsec) >= NSEC_PER_SEC) 
	{
		result.tv_sec = time1.tv_sec + time2.tv_sec + 1;
		result.tv_nsec = time1.tv_nsec + time2.tv_nsec - NSEC_PER_SEC;
	} 

	else 
	{
		result.tv_sec = time1.tv_sec + time2.tv_sec;
		result.tv_nsec = time1.tv_nsec + time2.tv_nsec;
	}

	return result;
}

/*****************************************************************************/

void endsignal(int sig)
{
	
  user_interrupt = 1;
	deactive = 5000; // do 5000 more cycles and exit
	signal( SIGINT , SIG_DFL );
	signal( SIGTERM , SIG_DFL );
}

/*****************************************************************************/


void check_domain_r_state(void)
{
		ec_domain_state_t ds;
		ecrt_domain_state(domain_r, &ds);

	//struct timespec time_wc1,time_wc2;
	if (ds.working_counter != domain_r_state.working_counter)
		printf("domain_r: WC %u.\n", ds.working_counter);
	if (ds.wc_state != domain_r_state.wc_state)
					printf("domain_r: State %u.\n", ds.wc_state);

		domain_r_state = ds;
}

void check_domain_w_state(void)
{
		ec_domain_state_t ds2;
		ecrt_domain_state(domain_w, &ds2);

	if (ds2.working_counter != domain_w_state.working_counter)
		printf("domain_w: WC %u.\n", ds2.working_counter);
	if (ds2.wc_state != domain_w_state.wc_state)
			 	 	printf("domain_w: State %u.\n", ds2.wc_state);
	
		domain_w_state = ds2;
}



void check_master_state(void)
{
		ec_master_state_t ms;

		ecrt_master_state(master, &ms);

		if (ms.slaves_responding != master_state.slaves_responding)
				printf("%u slave(s).\n", ms.slaves_responding);
		if (ms.al_states != master_state.al_states)
				printf("AL states: 0x%02X.\n", ms.al_states);
		if (ms.link_up != master_state.link_up)
				printf("Link is %s.\n", ms.link_up ? "up" : "down");

		master_state = ms;
}

/****************************************************************************/

enum StatusBits {
				ready           = 0b000000000001,
				can_start       = 0b000000000010,
				operation       = 0b000000000100,
				fault           = 0b000000001000,
				electrical_ok   = 0b000000010000, 
				quick_shutdown  = 0b000000100000,
				not_operational = 0b000001000000,
				warning         = 0b000010000000,
				unknown         = 0b000100000000,
				remote          = 0b001000000000,
				arrived         = 0b010000000000
};

// status bits set exactly match the query
uint16_t status_is(uint16_t query) {
		assert(query & 0xf800 == 0); // bits after bit 11 are reserved
		uint16_t status = temp[0] & 0x06f; // mask out the unknown bit
		return status == query;
}

// status bits include the bits set in the query
// but may also have other bits set.
uint16_t status_includes(uint16_t query) {
		assert( (query & 0xf800) == 0); // bits after bit 11 are reserved
		uint16_t status = (temp[0] & 0x07ef); // mask out the unknown bit
		return (status & query) == query;
}


int is_not_ready() {
	 return (temp[0] & 0x4f) == 0;
}

int is_startup_failure() {
		return status_includes(fault);
		//return status_includes(not_operational) && ((temp[0] & 0x000F) == 0);
}

int is_servo_ready() {
	return (!status_includes(not_operational) && status_includes(quick_shutdown))
					&& ((temp[0] & 0x0f) == 0b0001);
}

int is_startup() {
							return (!status_includes(not_operational) && status_includes(quick_shutdown))
										&& ((temp[0] & 0x0f) == 0b0011);
}

int is_servo_enabled() {
							return (!status_includes(not_operational) && status_includes(quick_shutdown))
										&& ((temp[0] & 0x0f) == 0b0111);
}

void show_status(long status) {
	const char *bits[] = {"+ready", "+can_start", "+operation", "+fault",
					"+electrical_ok", "+quick_shutdown", "+not_operational",
					"+warning", "+unknown", "+remote", "+arrived" };
	printf("%s%s%s%s%s%s%s%s%s%s%s\n",
			(status & 0b000000000001) ? bits[0] : "",
			(status & 0b000000000010) ? bits[1] : "",
			(status & 0b000000000100) ? bits[2] : "",
			(status & 0b000000001000) ? bits[3] : "",
			(status & 0b000000010000) ? bits[4] : "",
			(status & 0b000000100000) ? bits[5] : "",
			(status & 0b000001000000) ? bits[6] : "",
			(status & 0b000010000000) ? bits[7] : "",
			(status & 0b000100000000) ? bits[8] : "",
			(status & 0b001000000000) ? bits[9] : "",
			(status & 0b010000000000) ? bits[10] : ""
	);

}

/******************************************************/

enum ControlBits {
		enable_start           = 0b0000000000000001, 
		enable_main_circuit    = 0b0000000000000010, 
		enable_quick_shutdown  = 0b0000000000000100, 
		enable_servo_operation = 0b0000000000001000,
	  reset_fault            = 0b0000000010000000	 // 0x80
};

uint8_t control_reg = 0; // this is copied to the control register

// Preparation functions setup the control register but
// do not send it

void prepare_turn_on(uint16_t control_bits) {
	  control_reg |= control_bits;
}

void prepare_turn_off(uint16_t control_bits) {
		uint16_t mask = 0xffff ^ control_bits;
	  control_reg &= mask;
}

void set_mode(uint16_t mode) {
		assert(mode <= 10 && mode != 2 && mode != 5); // invalid mode value
		EC_WRITE_U16(domain_r_pd+modeofoper, mode);
}

// Setting functions prepare and send the control command

void send_control() {
		static uint16_t last_sent = 0;
		if (last_sent != control_reg) {
				printf("sending control: %04X\n", control_reg); 
		}
		EC_WRITE_U16(domain_r_pd+ctrl_word, control_reg );
		last_sent = control_reg;
}

void set_control(uint16_t control_bits) {
		control_reg = control_bits;
		send_control();
}

void turn_on(uint16_t control_bits) {
	  prepare_turn_on(control_bits);
		send_control();
}

void turn_off(uint16_t control_bits) {
	  prepare_turn_off(control_bits);
		send_control();
}

/**************************************/
enum ControlState { off, enabled_start,
				enabled_ready,
				enabled_servo_operation,
				has_fault,
				operational,
				moving};

int control_state = off;

const char *states[] = {
		"OFF",
		"ENABLED_START",
		"ENABLED_READY",
		"ENABLED_SERVO_OPERATION",
		"HAS_FAULT",
		"OPERATIONAL",
		"MOVING"
};
const char * control_state_str(int state) {
		return states[state];
}

void stop_servo() {
	printf("stopping servo\n");
	//servo off
	set_mode(0);
	if (is_servo_ready()) {
			turn_off(enable_start 
					| enable_servo_operation
					| enable_quick_shutdown
					| reset_fault
					| enable_main_circuit
					);
	}
  else	{
			prepare_turn_off(enable_start 
					| enable_servo_operation
					| enable_quick_shutdown
					| reset_fault);
			turn_on(enable_main_circuit);
	}
}
/***************************************************/
int collect(int control_state) {
		int returned_state = control_state;
		long prev_status = temp[0];
		static long prev_pos = 0;
		long prev_error = temp[2];
		long mode = temp[3];

		temp[0]=EC_READ_U16(domain_w_pd + status_word);
		temp[1]=EC_READ_U32(domain_w_pd + actual_pos);
		temp[2]=EC_READ_U16(domain_w_pd + error_code);
		temp[3]=EC_READ_U8(domain_w_pd + modeofop_display);
		temp[4]=EC_READ_U32(domain_w_pd + following_actu_val);
	
		static int first_time = 1;

		if (first_time) {
				first_time = 0;
				prev_pos = temp[1];
				printf("status: %04lx ", temp[0]); show_status(temp[0]);
				printf("position: %ld\n", temp[1]);
				printf("error: %ld\n", temp[2]);
				printf("mode: %ld\n", temp[3]);
				//EC_WRITE_U16(domain_r_pd+ctrl_word, 0x80);
		}
		else {
				if (prev_status != temp[0]) {
					printf("status change: %04lx to %04lx", prev_status, temp[0]);
					show_status(temp[0]);
					printf("\n");

					// Refer operating manual p79.
					if (is_not_ready()) {
							printf("servo is not ready\n");
							returned_state = off;
					}
					else if (is_startup_failure()) {
							printf("startup failure (ignored)\n");
							//returned_state = has_fault;
					}
					else if (is_servo_ready()) {
							printf("servo ready\n");
					}
					else if (is_startup()) {
							printf("start up\n");
					}
					else if (is_servo_enabled()) {
							printf("servo enabled\n");
					}
					else if (!status_includes(not_operational) && !status_includes(quick_shutdown)) {
							printf("malfunction shutdown valid\n");
							returned_state = fault;
					}
					else if (!status_includes(not_operational) && status_includes(fault)) {
							if ((temp[0] & 0x0f) == 0b0111) {
									printf("fault response valid\n");
									returned_state = fault;
							}
							else if (temp[0] & 0x0f == 0b0000) {
									printf("fault\n");
									returned_state = fault;
							}
					}
					else printf("unknown state\n");
		
				}
				long dist = prev_pos > temp[1] ? prev_pos - temp[1] : temp[1] - prev_pos; 
				if (dist > 20) {
					printf("position change: %ld to %ld\n", prev_pos, temp[1]);
					prev_pos = temp[1];
				}
				else if (dist > 5) printf(".");
				if (prev_error != temp[2]) {
					printf("error change: %ld to %ld\n", prev_error, temp[2]); 
#if 0
					// what do we do about error states?
					if (temp[2]) { control_state = has_fault; }
					else {
						// What state is the servo in after a fault resets?
						printf("error cleared\n");
						control_state = off;
						set_control(0);
					} 
#endif
				}
				if (mode != temp[3]) {
					printf("mode change: %ld to %ld\n", mode, temp[3]); 
				}
		}
    if (temp[2] || status_includes(fault)) {
				set_control(reset_fault);
		}
		else {
				turn_off(reset_fault);
		}
		return returned_state;
}

void cyclic_task()
{
    struct timespec wakeupTime, time;
    // get current time
    clock_gettime(CLOCK_TO_USE, &wakeupTime);

		unsigned long timer = 0;

	while(1) 
	{
	  if (deactive > 1) --deactive;
		if(deactive==1)
		{
			break;
		}

		wakeupTime = timespec_add(wakeupTime, cycletime);
     		clock_nanosleep(CLOCK_TO_USE, TIMER_ABSTIME, &wakeupTime, NULL);

		ecrt_master_receive(master);
		ecrt_domain_process(domain_r);
		ecrt_domain_process(domain_w);

		int new_state = collect(control_state);
		if (new_state != control_state) {
				printf("control state changed from %s to %s\n", control_state_str(control_state), control_state_str(new_state));
				control_state = new_state;
		}
		
		if (counter) 
		{
			counter--;
		} 
		
		else 
		{ 
			// do this at 1 Hz
			counter = FREQUENCY;
			blink = !blink;
		}

		++timer;
		if (timer > 1000) {

		// control

		int saved_control_state = control_state;
		static long sent_stop = 0;
		static int reported_waiting = 0;
		if (user_interrupt == 1)
		{
			if (sent_stop == 0) {
				sent_stop = 1;
				printf("stopping\n");
				stop_servo();
			}
#if 0
			if (deactive == 4000) {
				turn_off(enable_start + enable_main_circuit
					+ enable_quick_shutdown + enable_servo_operation);
			}
#endif
		}	
		else if (control_state == off) {
			set_mode(0);
			turn_on(enable_main_circuit + enable_quick_shutdown);
			timer=800;
			if (!is_not_ready()) {
				control_state = enabled_start;
				reported_waiting = 0;
			}
			else {
				if (!reported_waiting) printf("waiting for the servo to become ready\n");
				reported_waiting = 1;
			}
		}
		else if (control_state == enabled_start) {
			//prepare_turn_off(enabled_start);
			turn_on(ready);
			timer=800;
			if (is_startup()) {
				printf("enabling ready for start\n");
				control_state = enabled_ready;
				reported_waiting = 0;
			}
			else {
				if (!reported_waiting) printf("waiting for servo startup\n");
				reported_waiting = 1;
			}
		}
		else if (control_state == enabled_ready) {
			turn_on(enable_servo_operation);
			timer=800;
			if (is_servo_enabled()) {
				control_state = operational;
				EC_WRITE_S32(domain_r_pd+target_pos, temp[1]);
				prev_target_pos = temp[1];
				reported_waiting = 0;
				printf("setting mode to 8\n");
				set_mode(8); // set position mode
			}
			else {
				if (!reported_waiting)
					printf("waiting for the servo to become operational\n");
				reported_waiting = 1;
			}
		}
		else if (control_state == operational) {
				int error = (temp[1] > prev_target_pos) ? temp[1] - prev_target_pos : prev_target_pos - temp[1];
				if (status_includes(arrived) && error < 100 ) {
						EC_WRITE_S32(domain_r_pd+target_pos, temp[1] + 1000);
						printf("setting target %ld to %ld\n", prev_target_pos , temp[1] + 1000);
						prev_target_pos += 1000;
						//control_state = moving;
				}
		}
		if (saved_control_state != control_state) {
				printf("control state changed from %s to %s\n", 
								control_state_str(saved_control_state), control_state_str(control_state));
		}

		} // timer

#if 0
		else if( (temp[0]&0x004f) == 0x0040  )
		{
			if (last_sent != 0x0006) {
				printf("setting control word to 0x0006\n");
				EC_WRITE_U16(domain_r_pd+ctrl_word, 0x0006 );
				last_sent = 0x0006;
			}
		}

		else if( (temp[0]&0x006f) == 0x0021)
		{
			if (last_sent != 0x0007) {
				printf("setting control word to 0x0007\n");
				EC_WRITE_U16(domain_r_pd+ctrl_word, 0x0007 );
				last_sent = 0x0007;
			}
		}

		else if( (temp[0]&0x027f) == 0x0233)
		{
			if (last_sent != 0x000f) {
				printf("setting control word to 0x000f\n");
				EC_WRITE_U16(domain_r_pd+ctrl_word, 0x000f);
				EC_WRITE_S32(domain_r_pd+modeofoper, 1);
				last_sent = 0x000f;
			}
		}
		
		//operation enabled
		else if( (temp[0]&0x027f) == 0x0237)
		{
			// moving?
			EC_WRITE_S32(domain_r_pd+interpolateddata,( move_value+=1000 ));
			EC_WRITE_U16(domain_r_pd+ctrl_word, 0x001f);
		}

		if((temp[0]&0x067f) == 0x637)
		{
			if(prev_target_pos == 0) {
				prev_target_pos = 1000000;
				EC_WRITE_S32(domain_r_pd+target_pos, prev_target_pos);
				printf("set initial target %ld to %ld\n", temp[1], prev_target_pos);
			}
		/*	else if((temp[0]&0b010000000000) )
			{
				prev_target_pos = prev_target_pos + 10000;
				EC_WRITE_S32(domain_r_pd+target_pos, prev_target_pos);
				printf("setting target %ld to %ld\n", temp[1], prev_target_pos);
			} */
		}
		else if( (temp[0]&0x067f) == 0x0637)
		{
			if (last_sent != 0x0007) {
				printf("setting control word to 0x0007\n");
				//EC_WRITE_U16(domain_r_pd+ctrl_word, 0x0007);
				EC_WRITE_S32(domain_r_pd+modeofoper, 0);
				last_sent = 0x0007;
			}
		}
#endif

		// write application time to master
		clock_gettime(CLOCK_TO_USE, &time);
		ecrt_master_application_time(master, TIMESPEC2NS(time));

		
		if (sync_ref_counter) 
		{
			sync_ref_counter--;
		} 

		else 
		{
			sync_ref_counter = 1; // sync every cycle
			ecrt_master_sync_reference_clock(master);
		}

		ecrt_master_sync_slave_clocks(master);

		
		
		// send process data
		ecrt_domain_queue(domain_r);
		ecrt_domain_queue(domain_w);
		
		ecrt_master_send(master);
	}

}

#ifdef TESTING

void can_test_arrived() {
		temp[0] = arrived + electrical_ok + remote;
		assert(status_includes(arrived));
		temp[0] = ready+can_start+operation+electrical_ok+quick_shutdown+remote+arrived;
		assert(status_includes(arrived));
}

void can_detect_bit() {
		temp[0] = 0x250;
		assert(status_includes(not_operational));
}

int main() {
		can_detect_bit();
		can_test_arrived();
}

#else

int main(int argc, char **argv)
{
		ec_slave_config_t *sc;
	
	
		if (mlockall(MCL_CURRENT | MCL_FUTURE) == -1) 
		{
		perror("mlockall failed");
		return -1;
		}

		master = ecrt_request_master(0);
		if (!master)
					return -1;

		domain_r = ecrt_master_create_domain(master);
		if (!domain_r)
					return -1;
	
		domain_w = ecrt_master_create_domain(master);
		if (!domain_w)
					return -1;
			
	 	
			
		if (!(sc = ecrt_master_slave_config(master, wecon, wecon_vd3e))) 
		{
	fprintf(stderr, "Failed to get slave1 configuration.\n");
				return -1;
		}
		
#if CONFIGURE_PDOS

		printf("Configuring PDOs...\n");
	
		if (ecrt_slave_config_pdos(sc, EC_END, slave_0_syncs)) 
		{
				fprintf(stderr, "Failed to configure 1st PDOs.\n");
				return -1;
		}
	

#endif

	
		if (ecrt_domain_reg_pdo_entry_list(domain_r, domain_r_regs)) 
		{
				fprintf(stderr, "1st motor RX_PDO entry registration failed!\n");
				return -1;
		}	
	
		if (ecrt_domain_reg_pdo_entry_list(domain_w, domain_w_regs)) 
		{
				fprintf(stderr, "1st motor TX_PDO entry registration failed!\n");
				return -1;
		}
		
	
		ecrt_slave_config_dc(sc,0x0300,4000000,125000,0,0);	

		printf("Activating master...\n");
	
		if (ecrt_master_activate(master))
				return -1;

		if (!(domain_r_pd = ecrt_domain_data(domain_r))) 
		{
				return -1;
		}

		if (!(domain_w_pd = ecrt_domain_data(domain_w))) 
		{
				return -1;
		}
	
		pid_t pid = getpid();
		if (setpriority(PRIO_PROCESS, pid, -20))
			fprintf(stderr, "Warning: Failed to set priority: %s\n",
								strerror(errno));

		signal( SIGINT , endsignal ); //interrupt program		
		signal( SIGTERM , endsignal ); //interrupt program		
		printf("Starting cyclic function.\n");
		cyclic_task();
		ecrt_release_master(master);
	
		return 0;
}

#endif
