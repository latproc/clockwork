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

#ifndef __ECInterface
#define __ECInterface

#include <iostream>
#include <sys/types.h>

#ifndef EC_SIMULATOR
#include <ecrt.h>

/* the entry details structure is used to gather extra data about
  an entry in a module that the Etherlab master structures doesn't
  normally give us. 
*/
class EntryDetails {
public:
	std::string name;
	unsigned int entry_index;
	unsigned int sm_index;
	unsigned int pdo_index;
};

class ECModule {
public:
	ECModule();
	~ECModule();
	bool ecrtMasterSlaveConfig(ec_master_t *master);
	bool ecrtSlaveConfigPdos();
	bool online();
	bool operational();
	std::ostream &operator <<(std::ostream &)const;
public:
	ec_slave_config_t *slave_config;
	ec_slave_config_state_t slave_config_state;
	uint16_t alias;
	uint16_t position;
	uint32_t vendor_id;
	uint32_t product_code;
	unsigned int *offsets;
	unsigned int *bit_positions;
	unsigned int sync_count;
	
	ec_pdo_entry_info_t *pdo_entries;
	ec_pdo_info_t *pdos;
	ec_sync_info_t *syncs;
	std::string name;
	unsigned int num_entries;
	EntryDetails *entry_details;
};

#else
typedef unsigned char uint8_t;
typedef struct ECMaster{
    unsigned int reserved;
    unsigned int config_changed;
    unsigned int slave_count;
} ec_master_t;
typedef struct ECMasterState{
    unsigned int link_up;
} ec_master_state_t;
typedef struct ECDomain{} ec_domain_t;
typedef struct ECDomainState{} ec_domain_state_t;
typedef struct ECSlaveConfig{} ec_slave_config_t;
typedef struct ECSlaveConfigState{} ec_slave_config_state_t;
typedef struct ECPDOEntryReg{} ec_pdo_entry_reg_t;

#endif
#include <time.h>
#include <vector>
#include <string>

class ECInterface {
public:
	static int FREQUENCY;
	static ec_master_t *master;
	static ec_master_state_t master_state;

	static ec_domain_t *domain1;
	static ec_domain_state_t domain1_state;
	static uint8_t *domain1_pd;

	static ec_slave_config_state_t sc_dig_in_state;
	static ec_slave_config_t *sc_dig_in;

	static unsigned int off_dig_out;
	static unsigned int off_dig_in;
    static unsigned int off_multi_out;
    static unsigned int off_multi_in;
	bool initialised;
	bool active;

	static ECInterface *instance();

	// Timer
	static unsigned int sig_alarms;

	void check_domain1_state(void);
	void check_master_state(void);
	void check_slave_config_states(void);
	void receiveState(); // get state from EtherCAT, use collectState() to process it
	int collectState(); // returns non-zero if there are machines that are affected by the new state
	void sendUpdates();
	void updateDomain(uint32_t size, uint8_t *data, uint8_t *mask);
	
	bool start();
	bool stop();
	bool init();
	void add_io_entry(const char *name, unsigned int io_offset, unsigned int bit_offset);
    const ec_master_t *getMaster() { return master; }
    const ec_master_state_t *getMasterState() { return &master_state; }
#ifndef EC_SIMULATOR
	bool activate();
	bool addModule(ECModule *m, bool reset_io);
	bool online();
	bool operational();
	static std::vector<ECModule *>modules;
	//bool configurePDOs();
	static ECModule *findModule(int position);
	uint8_t *getProcessData() { return process_data; }
	uint8_t *getProcessMask();
	void setProcessData (uint8_t *pd);
	void setProcessMask (uint8_t *m);
	uint32_t getProcessDataSize();

	void setUpdateData (uint8_t *ud);
	void setUpdateMask (uint8_t *m);
	uint8_t *getUpdateData();
	uint8_t *getUpdateMask();
#endif
private:
	ECInterface();
	static ECInterface *instance_;
	uint8_t *process_data;
	uint8_t *process_mask;
	uint8_t *update_data;
	uint8_t *update_mask;
};

#endif
