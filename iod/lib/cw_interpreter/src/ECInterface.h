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

#include "IODCommand.h"

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

#ifndef EC_SIMULATOR
#include <ecrt.h>
#include <map>
#include "value.h"

// SlaveInfo holds the details of the slave type and version
class SlaveInfo {
	public:
	SlaveInfo() : position(0), product_code(0), vendor_id(0), revision(0), ec_info(0) {}
	SlaveInfo(const SlaveInfo &other) : position(other.position), product_code(other.product_code), 
		vendor_id(other.vendor_id), revision(other.revision)
	{
#ifndef EC_SIMULATOR
		if (other.ec_info) {
			ec_info = new ec_slave_info_t;
			*ec_info = *other.ec_info;
		}
		else ec_info = 0;
#endif
	}
	~SlaveInfo() { delete ec_info; }

	int position;
	uint32_t product_code;
	uint32_t vendor_id;
	uint32_t revision;
	std::string name;
#ifndef EC_SIMULATOR
	void set_slave_info(ec_slave_info_t &info) {
		ec_info = new ec_slave_info_t;
		memcpy(ec_info, &info, sizeof(ec_slave_info_t));
		position = ec_info->position;
		product_code = ec_info->product_code;
		vendor_id = ec_info->vendor_id;
		revision = ec_info->revision_number;
		name = ec_info->name;
	}
	ec_slave_info_t *ec_info;
#endif
};

#ifdef USE_SDO
class SDOEntry;
#endif //USE_SDO
class ECModule {
public:
	ECModule();
	~ECModule();
	bool ecrtMasterSlaveConfig(ec_master_t *master);
	bool ecrtSlaveConfigPdos();
	bool online();
	bool operational();
	int state();
	std::ostream &operator <<(std::ostream &)const;
	const std::string &getName() const { return name; }
public:
	ec_slave_config_t *slave_config;
	ec_slave_config_state_t slave_config_state;
	uint16_t alias;
	uint16_t position;
	uint32_t vendor_id;
	uint32_t product_code;
	uint32_t revision_no;
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
	unsigned int al_states;
} ec_master_state_t;
typedef struct ECDomain{} ec_domain_t;
typedef struct ECDomainState{} ec_domain_state_t;
typedef struct ECSlaveConfig{} ec_slave_config_t;
typedef struct ECSlaveConfigState{} ec_slave_config_state_t;
typedef struct ECPDOEntryReg{} ec_pdo_entry_reg_t;
typedef struct ECPDOEntryInfo{
    unsigned int index;
    unsigned int subindex;
    uint8_t bit_length;
} ec_pdo_entry_info_t;
typedef struct ECPDOInfo {
    unsigned int index;
    unsigned int n_entries;
    ec_pdo_entry_info_t *entries;
} ec_pdo_info_t;
typedef struct ECSyncInfo{
    uint8_t index;
    uint8_t dir;
    uint8_t watchdog_mode;
    unsigned int n_pdos;
    ec_pdo_info_t *pdos;
} ec_sync_info_t;

const int EC_DIR_INPUT = 0;
const int EC_DIR_OUTPUT = 1;
const int EC_WD_DEFAULT = 0;
#endif
#include <time.h>
#include <vector>
#include <string>

class MachineInstance;

class ECInterface {
public:
	static unsigned int FREQUENCY;
	static ec_master_t *master;
	static uint64_t master_last_checked; // time the master status was last checked
	static uint64_t master_state_changed; // time a last state change was detected in the master

#if 0
	static ec_master_info_t master_info;
	static uint64_t master_info_time; // time the master info was last read
#endif
	static ec_master_state_t master_state;

	static ec_domain_t *domain1;
	static ec_domain_state_t domain1_state;
	static uint8_t *domain1_pd;

	bool initialised;
	static bool active;

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
	void init(); // prepare the master
	static void setup(void *data); // call init and link to the clockwork machine instance for ethercat
	void add_io_entry(const char *name, unsigned int io_offset, unsigned int bit_offset);
	const ec_master_t *getMaster() { return master; }
	const ec_master_state_t *getMasterState() { return &master_state; }
#ifndef EC_SIMULATOR
	void listSlaves( std::list<ec_slave_info_t> &slaves );
	bool prepare();
	bool activate(); // attempt to activate the master
	bool deactivate(); // deactivate the master
	void configureModules();
	void registerModules();
	bool addModule(ECModule *m, bool reset_io);
	bool online();
	bool operational();
	//bool configurePDOs();
	static ECModule *findModule(unsigned int position);

	void setProcessData (uint8_t *pd);
	uint8_t *getProcessData() { return process_data; }

	void setProcessMask( uint8_t *new_mask );
	uint8_t *getProcessMask();
	void setAppProcessMask( uint8_t *new_mask, size_t size );

	void setMaxIOIndex(unsigned int new_max); // min index into user required process data (must be zero)
	void setMinIOIndex(unsigned int new_min); // max index into user required process data
	uint32_t getProcessDataSize(); // returns process data size of user selected data set
	
	uint32_t getReferenceTime();
	void setReferenceTime(uint32_t now);

	void setUpdateData (uint8_t *ud);
	void setUpdateMask (uint8_t *m);
	uint8_t *getUpdateData();
	uint8_t *getUpdateMask();

#ifdef USE_SDO
	void beginModulePreparation(); // load the first SDO initialisation entry
	bool finishedModulePreparation(); // are all the SDO init entries completed
	bool checkSDOInitialisation();
	void checkSDOUpdates();

	void addSDOEntry(SDOEntry *);
	static SDOEntry *createSDORequest(std::string name, ECModule *module, uint16_t index, uint8_t subindex, size_t size);

	void queueInitialisationRequest(SDOEntry *entry, Value val);
	void queueRuntimeRequest(SDOEntry *entry);
#endif //USE_SDO
#endif
private:
	ECInterface();
	static ECInterface *instance_;
	uint8_t *process_data;
	uint8_t *process_mask;
	uint8_t *update_data;
	uint8_t *update_mask;
	uint32_t reference_time;
#ifndef EC_SIMULATOR
	static std::vector<ECModule *>modules;
#ifdef USE_SDO
	std::list< std::pair<SDOEntry*, Value> > initialisation_entries;
	std::list< std::pair<SDOEntry*, Value> >::iterator current_init_entry;
	std::list<SDOEntry*> sdo_update_entries;
	std::list<SDOEntry*>::iterator current_update_entry;
	enum SDOEntryState { e_None, e_Busy_Initialisation, e_Busy_Update };
	SDOEntryState sdo_entry_state;
#endif //USE_SDO
#endif
	MachineInstance *ethercat_status;
	static long default_tolerance;
	const long *failure_tolerance;
	int failure_count;

	unsigned int min_io_index; // first byte of the process data needed by the user (must be zero currently)
	unsigned int max_io_index; // last byte of the process data needed by the user
	uint8_t *app_process_mask; // copy of user provided mask data
};

#ifdef USE_ETHERCAT
char *collectSlaveConfig(bool reconfigure);
#endif

struct IODCommandEtherCATTool : public IODCommand {
    bool run(std::vector<Value> &params);
};

struct IODCommandMasterInfo : public IODCommand {
    bool run(std::vector<Value> &params);
};

struct IODCommandGetSlaveConfig : public IODCommand {
	bool run(std::vector<Value> &params);
};

#endif
