/*
  Copyright (C) 2012 Martin Leadbeater, Michael O'Connor

  This file is part of Latproc

  Latproc is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.
  
  Latproc is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with Latproc; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#include <sys/time.h>
#include <unistd.h>
#include <iostream>
#include <iomanip>
#include <time.h>
#include <stdio.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <boost/thread.hpp>
#include "ECInterface.h"
#include "IOComponent.h"
#include <boost/thread/condition.hpp>
#ifndef EC_SIMULATOR
#include <ecrt.h>
#include "hw_config.h"

struct list_head {
    struct list_head *next, *prev;
};
typedef enum {
    EC_ORIG_INTERNAL, /**< Internal. */
    EC_ORIG_EXTERNAL /**< External. */
} ec_origin_t;

#include "domain.h"
#define EL1008_SYNCS slave_1_syncs
#define EL2008_SYNCS slave_2_syncs
#define EK1814_SYNCS slave_3_syncs
#endif

//extern boost::mutex model_mutex;
//extern boost::condition_variable_any model_updated;

extern boost::mutex ecat_mutex;
extern boost::condition_variable_any ecat_polltime;

void signal_handler(int signum);

int ECInterface::FREQUENCY = 2000;
ec_master_t *ECInterface::master = NULL;
ec_master_state_t ECInterface::master_state = {};

ec_domain_t *ECInterface::domain1 = NULL;
ec_domain_state_t ECInterface::domain1_state = {};
uint8_t *ECInterface::domain1_pd = 0;

ec_slave_config_t *ECInterface::sc_dig_in = NULL;
ec_slave_config_state_t ECInterface::sc_dig_in_state = {};

#if 0
#define BusCouplerPos  0, 0
#define DigInSlavePos 0, 1
#define DigOutSlavePos 0, 2
#define MultiCouplerInOutSlavePos 0, 3

#define Beckhoff_EK1100 0x00000002, 0x044c2c52
#define Beckhoff_EL1008 0x00000002, 0x03f03052
#define Beckhoff_EL2008 0x00000002, 0x07d83052
#define Beckhoff_EK1814 0x00000002, 0x07162c52

// offsets for PDO entries
unsigned int ECInterface::off_dig_out;
unsigned int ECInterface::off_dig_in;
unsigned int ECInterface::off_multi_out;
unsigned int ECInterface::off_multi_in;
#endif

#ifndef EC_SIMULATOR
#if 0
static ec_pdo_entry_reg_t domain1_regs[] =
{
    {DigInSlavePos, Beckhoff_EL1008, 0x6000, 1, &ECInterface::off_dig_in},
    {DigOutSlavePos, Beckhoff_EL2008, 0x7000, 1, &ECInterface::off_dig_out},
     {MultiCouplerInOutSlavePos, Beckhoff_EK1814, 0x6000, 1, &ECInterface::off_multi_in},
     {MultiCouplerInOutSlavePos, Beckhoff_EK1814, 0x7080, 1, &ECInterface::off_multi_out},
    {}
};
#endif

std::list<ECModule *> ECInterface::modules;

ECModule::ECModule() : pdo_entries(0), pdos(0), syncs(0) {
	offsets = new unsigned int[64];
	bit_positions = new unsigned int[64];
    slave_config = 0;
    memset(&slave_config_state, 0, sizeof(ec_slave_config_state_t));
	alias = 0;;
    position = 0;
    vendor_id = 0;
    product_code = 0;
    sync_count = 0;
    
    pdo_entries = 0;
    pdos = 0;
    syncs = 0;
}

bool ECModule::online() {
	return slave_config_state.online;
}

bool ECModule::operational() {
	return slave_config_state.operational;
}

#endif


ECInterface::ECInterface() :initialised(0), active(false) {
	initialised = init();
}

#ifndef EC_SIMULATOR

bool ECModule::ecrtMasterSlaveConfig(ec_master_t *master) {
	std::cout << name << ": " << alias <<", " << position 
		<< std::hex<< vendor_id << ", " << product_code << std::dec <<"\n";
	if (master) slave_config = ecrt_master_slave_config(master, alias, position, vendor_id, product_code);
	return slave_config != 0;
}

bool ECModule::ecrtSlaveConfigPdos() {
	return ecrt_slave_config_pdos(slave_config, sync_count, syncs);
}

std::ostream &ECModule::operator <<(std::ostream & out)const {
	out << "Slave " << name << ": " << alias <<", " << position << ", " 
		<< std::hex<< vendor_id << ", " << product_code << std::dec <<"\n";
	for (unsigned int i=0; i<sync_count; ++i) {
		out << "  SM" << i << ": " << (int)syncs[i].index << ", " << syncs[i].dir <<", "<< syncs[i].n_pdos << "\n";
		for (unsigned int j=0; j<syncs[i].n_pdos; ++j) {
			out << " PDO" << j << ": " << std::hex << syncs[i].pdos[j].index 
				<< ", " << std::dec << syncs[i].pdos[j].n_entries << "\n";
			for (unsigned int k = 0; k<syncs[i].pdos[j].n_entries; ++k) {
				ec_pdo_entry_info_t *e = syncs[i].pdos[j].entries + k;
				out << "       " << k << ", " << std::hex << e->index << std::dec
					<<", "<< (int)e->subindex << ", " << (int)e->bit_length<<"\n";
			}
		}
	}
	return out;
}


ECModule *ECInterface::findModule(int pos) {
	std::list<ECModule *>::iterator iter = modules.begin();
	while (iter != modules.end()){
		ECModule *m = *iter++;
		if (m->position == pos) return m;
	}
	return 0;
}

bool ECInterface::addModule(ECModule *module, bool reset_io) {
	
	if (module) {
		std::list<ECModule *>::iterator iter = modules.begin();
		while (iter != modules.end()){
			ECModule *m = *iter++;
			if (m->alias == module->alias && m->position == module->position) return false;
		}
		modules.push_back(module);
	}
	
	if (!reset_io) 
		return true;

	// reconfigure io
	
	std::list<ECModule *>::iterator iter = modules.begin();
	int idx = 0;
	while (iter != modules.end()){
		ECModule *m = *iter++;
	
		if (!m->ecrtMasterSlaveConfig(master)) {
	        std::cerr << "Failed to get slave configuration.\n";
	        return false;
	    }
	
//	    if (m->syncs && m->ecrtSlaveConfigPdos()) {
//	        std::cerr << "Failed to configure PDOs.\n";
//	        return false;
//	    }
		unsigned int module_offset_idx = 0;
		for(unsigned int i=0; i<m->sync_count; ++i) {
			//ecrt_slave_config_pdo_assign_clear(m->slave_config, i);
			//if (!m->syncs[i].pdos) continue; // careful to skip sm with no pdos
			for (unsigned int j = 0; j<m->syncs[i].n_pdos; ++j) {
				for (unsigned int k = 0; k < m->syncs[i].pdos[j].n_entries; ++k) {
					//if ( m->syncs[i].pdos[j].entries[k].index ) {
						m->offsets[module_offset_idx] 
						= ecrt_slave_config_reg_pdo_entry(
							m->slave_config, m->syncs[i].pdos[j].entries[k].index,
							m->syncs[i].pdos[j].entries[k].subindex,
							domain1, &(m->bit_positions[module_offset_idx]) );
					//}
					++idx;
					++module_offset_idx;
				}
			}
		}
	}

#if 0
// alternative approach, adding modules on the fly
	for (int i=0; i<module->sync_count; ++i) {
		for (int j = 0; j< module->syncs[i].n_pdos; ++j) {
			int res = ecrt_slave_config_reg_pdo_entry(module->slave_config, module->syncs[i].pdos[j].index, 1, domain1, 0);
			if (res <= 0) {
			std::cerr << "Error " << res << "when setting pdo entry\n";
			}
			else {
				module->offsets[i] = res;
				std::cout << "PDO registration for " << module->name << " sm " << i << " done\n";
			}
		}
	}
#endif
    //if (ecrt_master_activate(master)) {
	//	std::cerr << "failed to reactivate master\n";
	//        return false;	
	//}

    //if (!(domain1_pd = ecrt_domain_data(domain1))) {
	//	std::cout << "ecrt_domain_data failure\n";
    //    return false;
    //}

	return true;
}

#if 0
bool ECInterface::configurePDOs() {
    ec_slave_config_t *sc;
	int res;
    
    if (!(sc_dig_in = ecrt_master_slave_config(
                                               master, DigInSlavePos, Beckhoff_EL1008))) {
        std::cerr << "Failed to get slave configuration.\n";
        return false;
    }
    
    std::cout << "Configuring PDOs...\n";
    if ( (res = ecrt_slave_config_pdos(sc_dig_in, EC_END, EL1008_SYNCS)) ) {
        std::cerr << "Failed to configure PDOs." << res << "\n";
        return false;
    }
    
    if (!(sc = ecrt_master_slave_config(
                                        master, DigOutSlavePos, Beckhoff_EL2008))) {
        std::cerr << "Failed to get slave configuration.\n";
        return false;
    }
    
    if ( (res = ecrt_slave_config_pdos(sc, EC_END, EL2008_SYNCS)) ) {
        std::cerr << "Failed to configure PDOs." << res << "\n";
        return false;
    }
    
    
    if (!(sc = ecrt_master_slave_config(
                                        master, MultiCouplerInOutSlavePos, Beckhoff_EK1814))) {
        fprintf(stderr, "Failed to get slave configuration.\n");
        return -1;
    }
    
    if (ecrt_slave_config_pdos(sc, EC_END, EK1814_SYNCS)) {
        fprintf(stderr, "Failed to configure PDOs.\n");
        return -1;
    }
    
    sc = ecrt_master_slave_config(master, MultiCouplerInOutSlavePos, Beckhoff_EK1814);
    if (!sc)
        return -1;
    
    // Create configuration for bus coupler
    sc = ecrt_master_slave_config(master, BusCouplerPos, Beckhoff_EK1814);
    if (!sc)
        return false;
    
    if (ecrt_domain_reg_pdo_entry_list(domain1, domain1_regs)) {
        std::cerr << "PDO entry registration failed!\n";
        return false;
    }
    else {
        std::cerr << "PDO Registration: din offset: " << off_dig_in 
		<< " dout offset " << off_dig_out << "\n";
    }
	return true;
}
#endif

bool ECInterface::activate() {
	int res;
    std::cerr << "Activating master...";
    if ( (res = ecrt_master_activate(master)) ) {
	    std::cerr << "failed: " << res << "\n";
        return false;
	}
	active = true;
    std::cerr << "done\n";
	
    
    if (!(domain1_pd = ecrt_domain_data(domain1))) {
		std::cout << "ecrt_domain_data failure\n";
        return false;
    }
    return true;
}

bool ECInterface::online() {
	std::list<ECModule *>::iterator iter = modules.begin();
	while (iter != modules.end()){
		ECModule *m = *iter++;
		if (!m->online()) return false;
	}
	return true;
}

bool ECInterface::operational() {
	std::list<ECModule *>::iterator iter = modules.begin();
	while (iter != modules.end()){
		ECModule *m = *iter++;
		if (!m->operational()) return false;
	}
	return true;
}

#endif

bool ECInterface::init() {
    if(initialised) return true;
#ifdef EC_SIMULATOR
    master = new ec_master_t;
	return true;
#else
    master = ecrt_request_master(0);
    if (!master)
        return false;

    ecrt_master_deactivate(master);

    domain1 = ecrt_master_create_domain(master);
    if (!domain1) {
	std::cerr << "failed to create domain\n";
        return false;
    }
    else std::cout << "domain " << std::hex << domain1 << std::dec << " successfully created\n";

	return true;
	//return configurePDOs();
#endif
}


// Timer
unsigned int ECInterface::sig_alarms = 0;

void signal_handler(int signum) {
    switch (signum) {
        case SIGALRM:
            ECInterface::sig_alarms++;
            break;
        default:
            std::cerr << "Signal: " << signum << "\n" << std::flush;
    }
}

/*
long get_diff_in_microsecs(struct timeval *now, struct timeval *then) {
	long t = (now->tv_sec - then->tv_sec) % 1000;
	t = t * 1000000 + (now->tv_usec - then->tv_usec);
	return t;
}
*/

ECInterface* ECInterface::instance_ = 0;

ECInterface *ECInterface::instance() {
	if (!instance_) instance_ = new ECInterface();
	//if (!instance_->initialised) std::cerr << "Failed to initialise\n";
	return instance_;
}

void ECInterface::collectState() {
	if (!master || !initialised || !active) {
		//std::cerr << "x" << std::flush;
		return;
	}
#ifndef EC_SIMULATOR
    // receive process data
    ecrt_master_receive(master);
    ecrt_domain_process(domain1);

//    uint8_t *domain1_pd = ecrt_domain_data(domain1) ;
//	for (int i=0; i<100; ++i) 
//		std::cout << std::hex << std::setfill('0') << std::setw(2) << (int)*domain1_pd;
//	std::cout << "\n";

#endif
    //IOComponent::processAll();
    //std::cout << "/" << std::flush;
    check_domain1_state();
    // check for master state (optional)
    check_master_state();

    // check for islave configuration state(s) (optional)
    check_slave_config_states();
}
void ECInterface::sendUpdates() {
	if (!master || !initialised || !active) {
		//std::cerr << "x" << std::flush;
		return;
	}
#ifndef EC_SIMULATOR
    ecrt_domain_queue(domain1);
    ecrt_master_send(master);
#endif
}

/*****************************************************************************/

void ECInterface::check_domain1_state(void)
{
#ifndef EC_SIMULATOR
    ec_domain_state_t ds;
	memset(&ds, 0, sizeof(ec_domain_state_t));

    ecrt_domain_state(domain1, &ds);

#if 0
    if (ds.working_counter != domain1_state.working_counter)
        std::cout << "Domain1: WC " << ds.working_counter << "\n";
    if (ds.wc_state != domain1_state.wc_state)
        std::cout << "Domain1: State " << ds.wc_state << ".\n";
#endif

    domain1_state = ds;
#endif
}

/*****************************************************************************/

void ECInterface::check_master_state(void)
{
#ifndef EC_SIMULATOR
    ec_master_state_t ms;
	memset(&ms, 0, sizeof(ec_master_state_t));

    ecrt_master_state(master, &ms);

    if (ms.slaves_responding != master_state.slaves_responding)
        std::cout << ms.slaves_responding << " slave(s)\n";
    if (ms.al_states != master_state.al_states)
        std::cout << "AL states: 0x" << std::ios::hex << ms.al_states << "\n";
    if (ms.link_up != master_state.link_up)
        std::cout << "Link is " << (ms.link_up ? "up" : "down") << "\n";

    master_state = ms;
#endif
}

/*****************************************************************************/

void ECInterface::check_slave_config_states(void)
{
	return;
#ifndef EC_SIMULATOR
    ec_slave_config_state_t s;

	std::list<ECModule *>::iterator iter = modules.begin();
	while (iter != modules.end()){
		ECModule *m = *iter++;
	    ecrt_slave_config_state(m->slave_config, &s);
	
	    if (s.al_state != m->slave_config_state.al_state)
	        std::cout << m->name << ": State 0x" << std::ios::hex <<  s.al_state << ".\n";
	    if (s.online != m->slave_config_state.online)
	        std::cout << m->name << ": " << (s.online ? "online" : "offline") << "\n";
	    if (s.operational != m->slave_config_state.operational)
	        std::cout << m->name << ": " << (s.operational ? "" : "Not ") << "operational\n";

	    m->slave_config_state = s;
	}
#endif
}

bool ECInterface::start() {
    struct sigaction sa;
    struct itimerval tv;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGALRM, &sa, 0)) {
		std::cerr<< "Failed to install signal handler!\n";
        return false;
    }

    if (FREQUENCY > 1) {
        tv.it_interval.tv_sec = 0;
        tv.it_interval.tv_usec = 1000000 / FREQUENCY;
    }
    else {
        tv.it_interval.tv_sec = 1;
        tv.it_interval.tv_usec = 0;
    }
    tv.it_value.tv_sec = 0;
    tv.it_value.tv_usec = 1000;
    if (setitimer(ITIMER_REAL, &tv, NULL)) {
		std::cerr << "Failed to start timer: " << strerror(errno) << "\n";
        return false;
    }
	return true;
}

bool ECInterface::stop() {
    struct itimerval tv;
    tv.it_interval.tv_sec = 0;
    tv.it_interval.tv_usec = 0;
    tv.it_value.tv_sec = 0;
    tv.it_value.tv_usec = 0;
    if (setitimer(ITIMER_REAL, &tv, NULL)) {
		std::cerr << "Failed to stop timer: " << strerror(errno) << "\n";
        return false;
    }
	return true;
}
