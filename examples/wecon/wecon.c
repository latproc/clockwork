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

static ec_slave_config_t *sc  = NULL;
static ec_slave_config_state_t sc_state = {};
/****************************************************************************/

// process data
static uint8_t *domain_r_pd = NULL;
static uint8_t *domain_w_pd = NULL;
#define wecon		0,0
#define wecon_vd3e 	0x00000eff, 0x10003101

/***************************** 20140224  ************************************/

//signal to turn off servo on state
static unsigned int servo_flag;
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

/*****************************************************************************/

#if PDO_SETTING1
/* Master 0, Slave 0, "Wecon VD3E EtherCAT Servo_v1.05"
 * Vendor ID:       0x00000eff
 * Product code:    0x10003101
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
	
	servo_flag = 1;
	deactive = 10; // do 10 more cycles and exit
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

void show_status(long status) {
	const char *bits[] = {"+ready", "+can-start", "+operation", "+fault",
					"+electrical-ok", "+quick-shutdown", "+not-operational",
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

void cyclic_task()
{
    struct timespec wakeupTime, time;
    // get current time
    clock_gettime(CLOCK_TO_USE, &wakeupTime);

	while(1) 
	{
	  if (deactive > 1) --deactive;
		if(deactive==1)
		{
			printf("stopping\n");
			break;
		}

		wakeupTime = timespec_add(wakeupTime, cycletime);
     		clock_nanosleep(CLOCK_TO_USE, TIMER_ABSTIME, &wakeupTime, NULL);

		ecrt_master_receive(master);
		ecrt_domain_process(domain_r);
		ecrt_domain_process(domain_w);
		
		long prev_status = temp[0];
		long prev_pos;
		long prev_error =temp[2];
		long mode = temp[3];

		temp[0]=EC_READ_U16(domain_w_pd + status_word);
		temp[1]=EC_READ_U32(domain_w_pd + actual_pos);
		temp[2]=EC_READ_U16(domain_w_pd + error_code);
		temp[3]=EC_READ_U8(domain_w_pd + modeofop_display);
	
		static int first_time = 1;

		if (first_time) {
				first_time = 0;
				prev_pos = temp[1];
				printf("status: %04lx ", temp[0]); show_status(temp[0]);
				printf("position: %ld\n", temp[1]);
				printf("error: %ld\n", temp[2]);
				printf("mode: %ld\n", temp[3]);
				//EC_WRITE_U16(domain_r_pd+ctrl_word, 0x0080 );
		}
		else {
				if (prev_status != temp[0]) {
					printf("status change: %04lx to %04lx", prev_status, temp[0]);
					show_status(temp[0]);
				}
				long dist = prev_pos > temp[1] ? prev_pos - temp[1] : temp[1] - prev_pos; 
				if (dist > 20) {
					printf("position change: %ld to %ld\n", prev_pos, temp[1]);
					prev_pos = temp[1];
				}
				else if (dist > 5) printf(".");
				if (prev_error != temp[2]) { printf("error change: %ld to %ld\n", prev_error, temp[2]); }
				if (mode != temp[3]) { printf("mode change: %ld to %ld\n", mode, temp[3]); }
		}
    if (error_code) {
			printf("attempting to reset error\n");

			// reset from failure?
			EC_WRITE_U16(domain_r_pd+ctrl_word, 0x0080 );
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

		// write process data

		 static long last_sent = 0;
		if(servo_flag==1)
		{
			//servo off
			EC_WRITE_U16(domain_r_pd+ctrl_word, 0x0080 );
		}	

		else if( (temp[0]&0x004f) == 0x0000  )
		{
			if (last_sent != 0x0001) {
					printf("setting control word to 0x0001\n");
					EC_WRITE_U16(domain_r_pd+ctrl_word, 0x0006 );
			}
			last_sent = 0x0001;
		}

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
				EC_WRITE_S32(domain_r_pd+target_pos, temp[1] - 10000);
				EC_WRITE_S32(domain_r_pd+modeofoper, 1);
				last_sent = 0x000f;
			}
		}
		
		//operation enabled
		else if( (temp[0]&0x027f) == 0x0237)
		{
			// moving?
			//EC_WRITE_S32(domain_r_pd+interpolateddata,( move_value+=1000 ));
			//EC_WRITE_U16(domain_r_pd+ctrl_word, 0x001f);
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


