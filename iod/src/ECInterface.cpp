
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
#include <list>
#include <fstream>
#include "cJSON.h"
#include <boost/thread.hpp>
#include "ECInterface.h"
#include "IOComponent.h"
#include <boost/thread/condition.hpp>
#include "Statistic.h"
#include "Statistics.h"
#include "MachineInstance.h"
#ifndef EC_SIMULATOR
#include <ecrt.h>
#include <tool/MasterDevice.h>
#include "hw_config.h"
#include "MessageLog.h"
#include "SDOEntry.h"
#include "symboltable.h"
#include "SetStateAction.h"

struct list_head {
    struct list_head *next, *prev;
};

#include "domain.h"
#endif

extern boost::mutex ecat_mutex;
extern boost::condition_variable_any ecat_polltime;

extern Statistics *statistics;
void signal_handler(int signum);

unsigned int ECInterface::FREQUENCY = 2000;
ec_master_t *ECInterface::master = NULL;
ec_master_state_t ECInterface::master_state = {};

ec_domain_t *ECInterface::domain1 = NULL;
ec_domain_state_t ECInterface::domain1_state = {};
uint8_t *ECInterface::domain1_pd = 0;

static unsigned int expected_slaves = 0;
bool all_ok = false;
static bool link_was_up = false;
static bool master_was_running = false;

#ifndef EC_SIMULATOR

std::vector<ECModule *> ECInterface::modules;

static int slaves_not_operational = 1; // initialise to nonzero until we know for sure
static int slaves_offline = 1;
static bool ec_offline = true;

static std::list<SDOEntry *>prepared_sdo_entries;
static std::list<SDOEntry *>new_sdo_entries;

ECModule::ECModule() : pdo_entries(0), pdos(0), syncs(0), num_entries(0), entry_details(0) {
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

ECModule::~ECModule() {
	if (pdo_entries) { free(pdo_entries); pdo_entries = 0; }
	if (pdos) { free(pdos); pdos = 0; }
	if (syncs) { free(syncs); syncs = 0; }
	if (entry_details && num_entries) delete[] entry_details; entry_details = 0;
}

bool ECModule::online() {
	return slave_config_state.online && slaves_not_operational == 0 && slaves_offline == 0;
}

bool ECModule::operational() {
	return slave_config_state.operational;
}

SDOEntry::SDOEntry( std::string nam, uint16_t index, uint8_t subindex, const uint8_t *data, size_t size, uint8_t offset)
		: name(nam), module_(0), index_(index), subindex_(subindex), offset_(offset), data_(0), size_(size), 
			realtime_request(0), sync_done(false), error_count(0), op(READ), machine_instance(0) {
	if (data && size != 0) {
		data_ = new uint8_t[size];
		assert(data_);
		memcpy(data_, data, size);
	}
	new_sdo_entries.push_back(this);
}

#if 0
SDOEntry::SDOEntry( std::string nam, ec_sdo_request_t *sdo_req) 
	: name(nam), module_(0), index_(0), subindex_(0), offset_(0), data_(0), size_(0), 
			realtime_request(sdo_req), sync_done(false), error_count(0)
{
	new_sdo_entries.push_back(this);
}
#endif


SDOEntry::~SDOEntry() { if (data_) { delete[] data_; data_=0; }}

ec_sdo_request_t *SDOEntry::getRequest() { return realtime_request; }

void SDOEntry::setData(bool val) {
	uint8_t *data = ecrt_sdo_request_data(realtime_request);
	EC_WRITE_BIT(data, offset_, ((val) ? 1 : 0));
}

void SDOEntry::setData(uint8_t val) {
   EC_WRITE_U8(ecrt_sdo_request_data(realtime_request), val);
}

void SDOEntry::setData(int8_t val) {
   EC_WRITE_S8(ecrt_sdo_request_data(realtime_request), val);
}

void SDOEntry::setData(uint16_t val) {
   EC_WRITE_U16(ecrt_sdo_request_data(realtime_request), val);
}

void SDOEntry::setData(int16_t val) {
   EC_WRITE_S16(ecrt_sdo_request_data(realtime_request), val);
}

void SDOEntry::setData(uint32_t val) {
   EC_WRITE_U32(ecrt_sdo_request_data(realtime_request), val);
}

void SDOEntry::setData(int32_t val) {
   EC_WRITE_S32(ecrt_sdo_request_data(realtime_request), val);
}
ECModule *SDOEntry::getModule() {
	return module_;
}

void SDOEntry::setModule( ECModule *m ) { module_ = m; }

void SDOEntry::failure() { 
	++error_count; 
}
void SDOEntry::success() { 
	error_count = 0;
	sync_done = true; 
	if (op == WRITE) op = READ; // value has been successfully written, switch back to reading
}

bool SDOEntry::ok() { return error_count == 0; }

SDOEntry *SDOEntry::find(std::string name) {
	std::list<SDOEntry *>::iterator iter = prepared_sdo_entries.begin();
	while (iter != prepared_sdo_entries.end()) {
		SDOEntry *entry = *iter++;
		if (name == entry->getName())
			return entry;
	}
	return 0;
}

void SDOEntry::resolveSDOModules() {
	std::list<SDOEntry *>::iterator iter = new_sdo_entries.begin();
	while (iter != new_sdo_entries.end()) {
		SDOEntry *entry = *iter;
		if (entry->getModule()) {
			// this occurs when an entry has been automatically setup in the code 
			// (only done for EL2535 modules as a temporary measure to be removed)
			std::cout << "Module already linked to SDO entry " << entry->getName() << "\n";
			iter = new_sdo_entries.erase(iter);
			continue;
		}
		std::cout << "Attempting to prepare SDO entry: " << entry->getName() << "\n";
		MachineInstance *mi = MachineInstance::find( entry->getModuleName().c_str() );
		if (mi) {
			int module_position = mi->properties.lookup("position").iValue;
			ECModule *module = 0;
			if (module_position >= 0)
				module = ECInterface::findModule(module_position);
			if (module && entry->prepareRequest(module) ) {
				std::cout << "Prepared SDO entry: " << entry->getName() << "\n";
				iter = new_sdo_entries.erase(iter);
				if (entry->machineInstance() && entry->machineInstance()->properties.exists("default")) {
					const Value &val = entry->machineInstance()->properties.lookup("default");
					std::cout << "setting default value of " << entry->getName() << " to " << val << "\n";
					ECInterface::instance()->queueInitialisationRequest(entry, val);
				}
				else {
					std::cout << "no default value for " << entry->getName() << "\n";
				}
				ECInterface::instance()->queueRuntimeRequest(entry);
			}
			else {
				std::cout << "Warning: failed to prepare SDO entry: " << entry->getName() << "\n";
				iter++;
			}
		}
		else iter++;
	}
}

#endif


ECInterface::ECInterface() :initialised(0), active(false), process_data(0), process_mask(0),
		update_data(0), update_mask(0), reference_time(0)
#ifndef EC_SIMULATOR
		,current_init_entry(initialisation_entries.begin()), 
		current_update_entry( sdo_update_entries.begin() ),
		sdo_entry_state(e_None)
#endif
		, ethercat_status(0), failure_tolerance(0), failure_count(0)
{
	initialised = init();
	ethercat_status = MachineInstance::find("ETHERCAT");
	if (ethercat_status) {
		SetStateActionTemplate ssat = SetStateActionTemplate("SELF", "DISCONNECTED");
		SetStateAction *ssa = dynamic_cast<SetStateAction*>(ssat.factory(ethercat_status));
		ethercat_status->enqueueAction(ssa);

		const Value &tolerance_v = ethercat_status->getValue("tolerance");
		if (tolerance_v.kind != Value::t_integer)
			failure_tolerance = &default_tolerance;
		else
			failure_tolerance = &tolerance_v.iValue;

		std::cerr << "EtherCAT interface using " << *failure_tolerance
			<< " tries before marking the master state as bad\n";
	}
	else
		std::cerr << "EtherCAT interface could not find a clockwork bridge object\n";
}

#ifndef EC_SIMULATOR

void ECInterface::setReferenceTime(uint32_t now) {
	reference_time = now;
}

uint32_t ECInterface::getReferenceTime() {
	return reference_time;
}


bool ECModule::ecrtMasterSlaveConfig(ec_master_t *master) {
	//std::cout << name << ": " << alias <<", " << position 
	//	<< std::hex<< vendor_id << ", " << product_code << std::dec <<"\n";
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

ec_sdo_request_t *SDOEntry::prepareRequest(ECModule *module ) {
	assert(module);
	assert(ECInterface::instance()->active == false);
	module_ = module;
	prepared_sdo_entries.remove(this);
	ec_slave_config_t *x = ecrt_master_slave_config(ECInterface::master, 0, module->position, 
								 module->vendor_id, module->product_code);
	ec_slave_config_state_t s;
	ecrt_slave_config_state(x, &s);
	// the request field size must be big enough to hold the offset
	// the EtherLab interface only provides a byte-sized interface to SDO so we convert
	// our bit-sized fields before creating the sdo request
	size_t sz = ((size_ + offset_ -1) / 8) + 1;

	std::cerr << "Creating SDO request " << module->getName() << " 0x" << std::hex << index_ 
		<<":" << subindex_ << std::dec << " (" << sz << ")" << "\n";
	realtime_request = ecrt_slave_config_create_sdo_request(x, index_, subindex_, sz);
	prepared_sdo_entries.push_back(this);
	return realtime_request;
}

#if 0
SDOEntry *ECInterface::createSDORequest(std::string name, ECModule *module, uint16_t index, uint8_t subindex, size_t size) {
	assert(module);
	assert(ECInterface::instance()->active == false);
	ec_slave_config_t *x = ecrt_master_slave_config(ECInterface::master, 0, module->position, 
								 module->vendor_id, module->product_code);
	ec_slave_config_state_t s;
	ecrt_slave_config_state(x, &s);

	ec_sdo_request_t *sdo = ecrt_slave_config_create_sdo_request(x, index, subindex, size / 8);
	SDOEntry *entry = new SDOEntry(name, sdo);
	entry->setModuleName("unknown"); // we don't have the clockwork name for this object yet
	entry->setModule(module);
	prepared_sdo_entries.push_back(entry);
	return entry;
}
#endif

void ECInterface::queueInitialisationRequest(SDOEntry *entry, Value val) {
	initialisation_entries.push_back( std::make_pair(entry, val) );
}

void ECInterface::queueRuntimeRequest(SDOEntry *entry){
	sdo_update_entries.push_back(entry);
}

void ECInterface::beginModulePreparation() {
	std::cerr << "beginning module preparation\n";
	current_init_entry = initialisation_entries.begin();
	sdo_entry_state = e_None;
}

void readValue(ec_sdo_request_t *sdo, unsigned int size, int offset = 0) {
	if (size == 32) {
		fprintf(stderr, "SDO value: 0x%08X\n",
			EC_READ_U32(ecrt_sdo_request_data(sdo)));
	}
	else if (size == 16) {
		fprintf(stderr, "SDO value: 0x%04X\n",
			EC_READ_U16(ecrt_sdo_request_data(sdo)));
	}
	else if (size == 8) {
		fprintf(stderr, "SDO value: 0x%02X\n",
			EC_READ_U8(ecrt_sdo_request_data(sdo)));
	}
	else if (size == 1) {
		fprintf(stderr, "SDO value: 0x%01X\n",
			EC_READ_BIT(ecrt_sdo_request_data(sdo), offset));
	}
}

void SDOEntry::syncValue() {
	if (size_ == 32) {
		if (machine_instance)
			machine_instance->setValue("VALUE", EC_READ_U32(ecrt_sdo_request_data(realtime_request)));
	}
	else if (size_ == 16) {
		if (machine_instance)
			machine_instance->setValue("VALUE", EC_READ_U16(ecrt_sdo_request_data(realtime_request)));
	}
	else if (size_ == 8) {
		if (machine_instance)
			machine_instance->setValue("VALUE", EC_READ_U8(ecrt_sdo_request_data(realtime_request)));
	}
	else if (size_ == 1) {
		if (machine_instance)
			machine_instance->setValue("VALUE", EC_READ_BIT(ecrt_sdo_request_data(realtime_request),offset_));
	}
}

Value SDOEntry::readValue() {
	if (size_ == 32) {
		return EC_READ_U32(ecrt_sdo_request_data(realtime_request));
	}
	else if (size_ == 16) {
		return EC_READ_U16(ecrt_sdo_request_data(realtime_request));
	}
	else if (size_ == 8) {
		return EC_READ_U8(ecrt_sdo_request_data(realtime_request));
	}
	else if (size_ == 1) {
		return EC_READ_BIT(ecrt_sdo_request_data(realtime_request),offset_);
	}
	return SymbolTable::Null;
}

void ECInterface::checkSDOUpdates()  {
	if (current_update_entry == sdo_update_entries.end()) {
		if (sdo_update_entries.size() == 0) return;
		current_update_entry = sdo_update_entries.begin();
		sdo_entry_state = e_None; // no active entry, next 
	}
	if (current_update_entry != sdo_update_entries.end()) {
		SDOEntry *entry = *current_update_entry;
		if (!entry) { 
			//std::cerr << "Skipping null entry when checking SDO updates\n";
			current_update_entry++; return; 
		} // odd: no entry at this position

		// disabled entries are not automatically polled for changes unless they were already
		// in the middle of a poll when they were disabled
		if (sdo_entry_state == e_None && entry->machineInstance() && !entry->machineInstance()->enabled()) {
			//std::cerr << "Skipping disabled entry when checking SDO updates\n";
			current_update_entry++; return; 
	}
		ec_sdo_request_t *sdo = entry->getRequest();

		//std::cerr << "checking SDO entry " << entry->getName() << " for updates\n";
		if (sdo_entry_state == e_None) {
			if (entry->operation() == SDOEntry::WRITE) {
			assert( !initialisation_entries.empty() );
			return; // let the initialisation process deal with this update
		}

			switch (entry->operation()) {
				case SDOEntry::READ:
					//std::cerr << "SDO entry updates - trigger read\n";
					ecrt_sdo_request_read(sdo); // trigger first read
					sdo_entry_state = e_Busy_Update;
					break;
				case SDOEntry::WRITE:
					assert(false); // this should not be active
					std::cerr << "SDO entry updates- trigger write\n";
					readValue(sdo, entry->getSize(), entry->getOffset());
					ecrt_sdo_request_write(sdo); // trigger first read
					sdo_entry_state = e_Busy_Update;
					break;
				default: assert(false);
			}
			return;
		}

		int state = 0;
	    switch ( (state=ecrt_sdo_request_state(sdo)) ) {
	        case EC_REQUEST_UNUSED: // request was not used yet
				sdo_entry_state = e_None;
	            break;
	        case EC_REQUEST_BUSY:
				//std::cerr << "SDO entry in progress\n";
	            break;
	        case EC_REQUEST_SUCCESS:
				//std::cerr << "SDO entry written\n";

				// before updating the value of the object check whether a new value is about to
				// be written to the io
				if (entry->operation() == SDOEntry::READ) {
					entry->syncValue();
					entry->success();
				}
				// prepare to get the next entry
				current_update_entry++;
				sdo_entry_state = e_None;
	            break;
	        case EC_REQUEST_ERROR:
	            std::cerr << "Failed to read SDO!" << std::hex << "0x" << entry->getIndex() 
					<< ":" << (int)entry->getSubindex() << std::dec << "\n";
				entry->failure();
	            //ecrt_sdo_request_write(sdo); // retry reading
				current_update_entry++; // move on to the next item and retry soon
				sdo_entry_state = e_None;
	            break;
			default:
				std::cerr << "unexpected sdo request state: " << state << "\n";
	    }
	}
}

bool ECInterface::checkSDOInitialisation() // returns true when no more initialisation is required
{
	if (sdo_entry_state == e_Busy_Update) return true;
	if (current_init_entry == initialisation_entries.end()) {
		if (initialisation_entries.size() == 0) return true;
		current_init_entry = initialisation_entries.begin();
		sdo_entry_state = e_None;
	}
	if (current_init_entry != initialisation_entries.end()) {
		std::pair<SDOEntry *, Value> curr = *current_init_entry;
		SDOEntry *entry = curr.first;
		if (!entry) { 
			std::cerr << "Skipping null entry when checking SDO\n";
			current_init_entry++; return false; 
		} // odd: no entry at this position

		ec_sdo_request_t *sdo = entry->getRequest();

		//std::cerr << "checking SDO entry " << entry->getName() << "\n";
		if (sdo_entry_state == e_None) {
			entry->setOperation(SDOEntry::WRITE);
			if (entry->getSize() == 1)
				entry->setData( (bool)curr.second.iValue );
			else if (entry->getSize() == 8)
				entry->setData( (uint8_t)curr.second.iValue );
			else if (entry->getSize() == 16)
				entry->setData( (uint16_t)curr.second.iValue );
			else if (entry->getSize() == 32)
				entry->setData( (uint32_t)curr.second.iValue );
			std::cerr << "SDO entry - trigger write " << curr.second << "\n";
			readValue(sdo, entry->getSize());
			ecrt_sdo_request_write(sdo);
			sdo_entry_state = e_Busy_Initialisation;
			return false;
		}

		int state = 0;
	    switch ( (state=ecrt_sdo_request_state(sdo)) ) {
	        case EC_REQUEST_UNUSED: // request was not used yet
				sdo_entry_state = e_None;
	            break;
	        case EC_REQUEST_BUSY:
				//std::cerr << "SDO entry in progress\n";
	            break;
	        case EC_REQUEST_SUCCESS:
				if (entry->operation() == SDOEntry::READ)
					std::cerr << "SDO entry read\n";
				else
					std::cerr << "SDO entry written\n";
				entry->syncValue();
				entry->success();
				// prepare to get the next entry
				current_init_entry = initialisation_entries.erase(current_init_entry);
				sdo_entry_state = e_None;
	            break;
	        case EC_REQUEST_ERROR:
				if (entry->operation() == SDOEntry::READ)
					std::cerr << "Failed to read SDO entry ";
				else
					std::cerr << "Failed to write SDO entry ";
	            std::cerr << std::hex << "0x" << entry->getIndex() 
					<< ":" << (int)entry->getSubindex() << std::dec << "\n";
				entry->failure();
	            //ecrt_sdo_request_write(sdo); // retry reading
				if (entry->getErrorCount() < 4) 
					current_init_entry++; // move on to the next item and retry soon
				else {
					current_init_entry = initialisation_entries.erase(current_init_entry);
					entry->setOperation(SDOEntry::READ);
				}
				sdo_entry_state = e_None;
	            break;
			default:
				std::cerr << "unexpected sdo request state: " << state << "\n";
	    }
	}
	return false;
}

ECModule *ECInterface::findModule(int pos) {
	if (pos < 0 || (unsigned int)pos >= modules.size()) return 0;
	ECModule *m = modules.at(pos);
	if (m->position != pos) std::cerr << "ERROR: " << m->product_code << " is not at position " << pos <<"\n";
	assert(m->position == pos);
	return m;
#if 0
	std::vector<ECModule *>::iterator iter = modules.begin();
	while (iter != modules.end()){
		ECModule *m = *iter++;
		if (m->position == pos) return m;
	}
	return 0;
#endif
}

bool ECInterface::addModule(ECModule *module, bool reset_io) {
	
	if (module) {
		std::vector<ECModule *>::iterator iter = modules.begin();
		while (iter != modules.end()){
			ECModule *m = *iter++;
			if (m->alias == module->alias && m->position == module->position) return false;
		}
		modules.push_back(module);
#if 0
		if (module->name.substr(0,6) == "EL2535") {
			std::cerr << "queueing a request to change EL2535 MaxCurrent\n";
			SDOEntry *entry = createSDORequest("EL2535 PortA MaxCurrent", module, 0x8000, 0x10, 8);
			entry->setData( (uint8_t)50);
			ECInterface::instance()->queueInitialisationRequest(entry);
			entry = createSDORequest("EL2535 PortB MaxCurrent", module, 0x8010, 0x10, 8);
			entry->setData( (uint8_t)50);
			ECInterface::instance()->queueInitialisationRequest(entry);
		}
		else if (module->name.length() > 6)
			std::cerr << "No match " << module->name.substr(6) << " checking for EL2535\n";
		else
			std::cerr << "No match " << module->name << " checking for EL2535\n";
#endif
	}
	
	if (!reset_io) 
		return true;

	// reconfigure io
	
	std::vector<ECModule *>::iterator iter = modules.begin();
	int idx = 0;
	while (iter != modules.end()){
		ECModule *m = *iter++;
	
		if (!m->ecrtMasterSlaveConfig(master)) {
	        std::cerr << "Failed to get slave configuration.\n";
	        return false;
	    }
	
		unsigned int module_offset_idx = 0;
		for(unsigned int i=0; i<m->sync_count; ++i) {
			for (unsigned int j = 0; j<m->syncs[i].n_pdos; ++j) {
				for (unsigned int k = 0; k < m->syncs[i].pdos[j].n_entries; ++k) {
					m->offsets[module_offset_idx] 
						= ecrt_slave_config_reg_pdo_entry(
							m->slave_config, m->syncs[i].pdos[j].entries[k].index,
							m->syncs[i].pdos[j].entries[k].subindex,
							domain1, &(m->bit_positions[module_offset_idx]) );
					++idx;
					++module_offset_idx;
				}
			}
		}
	}

	return true;
}

bool ECInterface::prepare() {
    std::cerr << "Preparing IO...";
    return false;
}

bool ECInterface::activate() {
	int res;
    std::cerr << "Activating master...";
    if ( (res = ecrt_master_activate(master)) ) {
	    std::cerr << "failed: " << res << "\n";
        return false;
	}
	active = true;
    std::cerr << "done\n";
	if (ethercat_status) {
		SetStateActionTemplate ssat = SetStateActionTemplate("SELF", "ACTIVE");
		SetStateAction *ssa = dynamic_cast<SetStateAction*>(ssat.factory(ethercat_status));
		ethercat_status->enqueueAction(ssa);
	}
	else
		std::cerr << "No EtherCAT clockwork bridge object defined to report status to\n";
	
    
    if (!(domain1_pd = ecrt_domain_data(domain1))) {
		std::cout << "ecrt_domain_data failure\n";
        return false;
    }
    return true;
}

bool ECInterface::online() {
	std::vector<ECModule *>::iterator iter = modules.begin();
	while (iter != modules.end()){
		ECModule *m = *iter++;
		if (!m->online()) return false;
	}
	return true;
}

bool ECInterface::operational() {
	std::vector<ECModule *>::iterator iter = modules.begin();
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
    else std::cout << "domain " << std::hex << domain1 << std::dec << " successfully created"
		<< " with size " << ecrt_domain_size(domain1) << "\n";

    all_ok = true; // ok to try to start processing
	failure_count = 0;

#if 0
	IODCommandThread::registerCommand("EC", new IODCommandEtherCATTool);
	IODCommandThread::registerCommand("MASTER", new IODCommandMasterInfo);
	IODCommandThread::registerCommand("SLAVES", new IODCommandGetSlaveConfig);
#endif

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

ECInterface* ECInterface::instance_ = 0;

ECInterface *ECInterface::instance() {
	if (!instance_) instance_ = new ECInterface();
	//if (!instance_->initialised) std::cerr << "Failed to initialise\n";
	return instance_;
}

#ifndef EC_SIMULATOR

uint32_t ECInterface::getProcessDataSize() {
	int max = IOComponent::getMaxIOOffset();
	int min = IOComponent::getMinIOOffset();
	int result = max - min +1;
	return result;
}

void ECInterface::setProcessData (uint8_t *pd) { 
	if (process_data) delete[] process_data; process_data = pd; 
}
uint8_t *ECInterface::getProcessMask() { return IOComponent::getProcessMask(); }

void ECInterface::setProcessMask (uint8_t *m) { 
	if (process_mask) delete[] process_mask; process_mask = m; 
}

void ECInterface::setUpdateData (uint8_t *ud) {
	if (update_data) delete[] update_data;
	update_data = ud;
}
void ECInterface::setUpdateMask (uint8_t *m){
	if (update_mask) delete[] update_mask;
	update_mask = m;
}
uint8_t *ECInterface::getUpdateData() { return update_data; }
uint8_t *ECInterface::getUpdateMask() { return update_mask; }

// copy interesting bits that have changed from the supplied
// data into the process data and the saved copy of the process data.
// the latter is because we want to properly detect changes in the
// next read cycle

#if 0
static void display(uint8_t *p) {
	int max = IOComponent::getMaxIOOffset();
	int min = IOComponent::getMinIOOffset();
    for (int i=min; i<=max; ++i) 
			std::cout << std::setw(2) << std::setfill('0') << std::hex << (unsigned int)p[i];
		std::cout << std::dec;
}
#endif

void ECInterface::updateDomain(uint32_t size, uint8_t *data, uint8_t *mask) {
	int max = IOComponent::getMaxIOOffset();
	int min = IOComponent::getMinIOOffset();
	uint8_t *domain1_pd = ecrt_domain_data(domain1) ;
	uint8_t *pd = domain1_pd;
	uint8_t *saved_pd = process_data;

/* 
	std::cerr << "updating domain (size = " << size << ")\n";
	std::cerr << "process: "; display(pd); std::cout << "\n";
	std::cerr << "   mask: "; display(mask); std::cout << "\n";
	std::cerr << "   data: "; display(data); std::cout << "\n";

	if (!all_ok || master_state.al_states != 0x8) {
		std::cerr << "refusing to update the domain since all is not ok\n";
	}
*/
	for (unsigned int i=0; i<size; ++i) {
		if (*mask && *data != *pd){
/*
			std::cout << "at " << i << " data (" 
				<< (unsigned int)(*data) << ") different to domain ("
				<< (unsigned int)(*pd) << ")\n";
*/
			uint8_t bitmask = 0x01;
			int count = 0;
			while (bitmask) {
				if ( *mask & bitmask ) { // we care about this bit
					uint8_t pdb = *pd & bitmask;
					uint8_t db = *data & bitmask;
					if ( pdb != db ) { // changed
						//std::cout << "bit " << i << ":" << count << " changed to ";
						if ( db ) { 
							*pd |= bitmask; 
							//std::cout << "on";
						}
						else {
							*pd &= (uint8_t)(0xff - bitmask);
							//std::cout << "off";
						}
					}
				}
				bitmask = bitmask << 1;
				++count;
			}
		}
		++pd; ++mask; ++data;
	}
}

void ECInterface::receiveState() {
	if (!master || !initialised || !active) {
		//std::cerr << "master not ready to collect state\n" << std::flush;
		return;
	}
	// receive process data
	//struct timeval now;
	//gettimeofday(&now, 0);
	//reference_time = ((uint64_t)now.tv_sec*1000000L + now.tv_usec) % 0x100000000;
	ecrt_master_receive(master);
#ifdef USE_DC
	int err = ecrt_master_reference_clock_time(master, &reference_time);
	if (err == -ENXIO) { reference_time = -1; } // no reference clocks
#endif
	ecrt_domain_process(domain1);
	check_domain1_state();
	// check for master state (optional)
	check_master_state();
	// check for islave configuration state(s) (optional)
	check_slave_config_states();

	if (checkSDOInitialisation())
		checkSDOUpdates();
}

int ECInterface::collectState() {
	if (!master || !initialised || !active) {
		std::cerr << "master not ready to collect state\n" << std::flush;
		return 0;
	}
#ifndef EC_SIMULATOR

//    uint8_t *domain1_pd = ecrt_domain_data(domain1) ;
//	for (int i=0; i<100; ++i) 
//		std::cout << std::hex << std::setfill('0') << std::setw(2) << (int)*domain1_pd;
//	std::cout << "\n";

	// workout what io components need to process updates
	uint8_t *domain1_pd = ecrt_domain_data(domain1) ;
	if (!domain1_pd) return 0;
	uint8_t *pd = domain1_pd;
	int max = IOComponent::getMaxIOOffset();
	int min = IOComponent::getMinIOOffset();
	int affected_bits = 0;

	// the result of this is a list of data bits to be changed and
	// a mask indicating which bits are important
	size_t domain_size = ecrt_domain_size(domain1);
	if ((long)domain_size < 0) {
		return 0;
	}
	assert(domain_size >= (size_t)max - min + 1);
	if (!update_data) update_data = new uint8_t[domain_size]; 
	if (!update_mask) update_mask = new uint8_t[domain_size]; 
	memset(update_data, 0, domain_size);
	memset(update_mask, 0, domain_size);

	// first time through, copy the domain process data to our local copy
	// and set the process mask to include every bit we care about

	// after that, look at all bits we care about and if the bit has changed
	// copy its new value to the update data and include the bit in the
	// update mask
	uint8_t *last_pd = instance()->getProcessData();
	uint8_t *pm = IOComponent::getProcessMask(); // these are the important bits
	uint8_t *q = update_data; // convenience pointer

	for (int i=min; i<=max; ++i) {
		update_mask[i] = 0; // assume no updates in this octet
		if (!last_pd) { // first time through, copy all the domain data and mask
			update_data[i] = domain1_pd[i]; //TBD & *pm;
			update_mask[i] = *pm;
			affected_bits++;
		}
		else if (*last_pd != domain1_pd[i]){
			uint8_t bitmask = 0x01;
			int count = 0;
			while (bitmask) {
				if (*pm & bitmask ) { // we care about this bit
					if ( (*pd & bitmask) != (*last_pd & bitmask) ) { // changed
#if 0
						//if (i != 47 ) // ignore analog changes on our machine
						std::cout << "incoming bit " << i << ":" << count 
							<< " changed to " << ((*pd & bitmask)?1:0) << "\n";
#endif
						if ( *pd & bitmask ) *q |= bitmask;
						else *q &= (uint8_t)(0xff - bitmask);
						update_mask[i] |= bitmask;
						++affected_bits;
					}
				}
				bitmask = bitmask << 1;
				++count;
			}
		}
		++pd; ++q; ++pm; if (last_pd)++last_pd;
	}
#if 0
	if (affected_bits) {
		std::cout << "data: "; display(update_data); 
		std::cout << "\nmask: "; display(update_mask);
		std::cout << " " << affected_bits << " bits changed (size=" << domain_size << ")\n";
	}
#endif

	// save the domain data for the next check
	pd = new uint8_t[max - min + 1];
	memcpy(pd, domain1_pd+min, max - min + 1);
	instance()->setProcessData(pd);
	memcpy(update_data, domain1_pd, max - min + 1);
#endif

	return affected_bits;
}
void ECInterface::sendUpdates() {
	static time_t last_warning = 0;
	struct timeval now;
	gettimeofday(&now, 0);
	if (!master || !initialised || !active || !all_ok /*|| master_state.al_states != 0x88*/) {
		if (now.tv_sec + 5 < last_warning) {
			std::cerr << "master not ready to send updates\n" << std::flush;
			char buf[100];
			snprintf(buf, 100, "EtherCAT master is not ready to send updates\n");
			MessageLog::instance()->add(buf);
		}
		return;
	}
#ifndef EC_SIMULATOR
#ifdef USE_DC
	ecrt_master_application_time(master, EC_TIMEVAL2NANO(now));
	ecrt_master_sync_reference_clock(master);
	ecrt_master_sync_slave_clocks(master);
#endif
    ecrt_domain_queue(domain1);
    ecrt_master_send(master);
#endif
}
#endif

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

	if (ms.slaves_responding != master_state.slaves_responding) {
		std::cout << ms.slaves_responding << " slave(s)\n";
		char buf[100];
		if (ms.slaves_responding >= expected_slaves)
			snprintf(buf, 100, "Number of slaves has changed from %d to %d", master_state.slaves_responding, ms.slaves_responding);
		else 
			snprintf(buf, 100, "Number of slaves %d less than expected %d", ms.slaves_responding, expected_slaves);
		MessageLog::instance()->add(buf);
	}
	if (ms.slaves_responding != master_state.slaves_responding || ms.slaves_responding < expected_slaves) {
		if (ms.slaves_responding > expected_slaves)
			expected_slaves = ms.slaves_responding;
		else {
			if (++failure_count > *failure_tolerance) {
				all_ok = false; // lost a slave
				if (ethercat_status) {
					SetStateActionTemplate ssat = SetStateActionTemplate("SELF", "ERROR");
					SetStateAction *ssa = dynamic_cast<SetStateAction*>(ssat.factory(ethercat_status));
					ethercat_status->enqueueAction(ssa);
				}
			}
		}
	}
	if (ms.al_states != master_state.al_states) {
		std::cout << "AL states: 0x" << std::ios::hex << ms.al_states << std::ios::dec<< "\n";
		char buf[100];
		snprintf(buf, 100, "EtherCAT state change: was 0x%04x now 0x%04x", master_state.al_states, ms.al_states);
		MessageLog::instance()->add(buf);
	}
	if (master_was_running && ms.al_states != 0x8) {
		if (all_ok && ++failure_count > *failure_tolerance) {
			all_ok = false;
			if (ethercat_status) {
				SetStateActionTemplate ssat = SetStateActionTemplate("SELF", "ERROR");
				SetStateAction *ssa = dynamic_cast<SetStateAction*>(ssat.factory(ethercat_status));
				ethercat_status->enqueueAction(ssa);
			}
		}
	}
	else if (ms.al_states == 0x8)
		master_was_running = true;

	// log all changes of link state
	if (ms.link_up != master_state.link_up) {
		std::cout << "Link is " << (ms.link_up ? "up" : "down") << "\n";
		char buf[100];
		snprintf(buf, 100, "EtherCAT link state change was %s now %s", master_state.link_up ?"up":"down", ms.link_up?"up":"down");
		MessageLog::instance()->add(buf);

		// copy link state change through to clockwork
		MachineInstance *link_status = MachineInstance::find("ETHERCAT_LS");
		if (link_status) {
			if (!link_status->enabled()) link_status->enabled();
			const char *state = ms.link_up ? "UP" : "DOWN";
			SetStateActionTemplate ssat = SetStateActionTemplate("SELF", state);
			SetStateAction *ssa = dynamic_cast<SetStateAction*>(ssat.factory(link_status));
			link_status->enqueueAction(ssa);

			char buf[100];
			snprintf(buf, 100, "Info: asked link status machine to change to state %s", state);
			MessageLog::instance()->add(buf);
		}
		else {
			char buf[100];
			snprintf(buf, 100, "Warning: no machine found to track EtherCAT link status");
			MessageLog::instance()->add(buf);
		}
	}
	// if the link changes state or if it was once up but is now down, check state 
	if (ms.link_up != master_state.link_up || (link_was_up && !ms.link_up) ) {
		if (!ms.link_up) {
			if (all_ok && ++failure_count > *failure_tolerance) {
				all_ok = false;
				if (ethercat_status) {
					SetStateActionTemplate ssat = SetStateActionTemplate("SELF", "ERROR");
					SetStateAction *ssa = dynamic_cast<SetStateAction*>(ssat.factory(ethercat_status));
					ethercat_status->enqueueAction(ssa);
				}
			}
		}
		else { // assume that now the link is backup we can reset the errors 
			failure_count = 0;
			link_was_up = true;
		}
	}

	master_state = ms;
#endif
}

/*****************************************************************************/

void ECInterface::check_slave_config_states(void)
{
	return;
#ifndef EC_SIMULATOR
    ec_slave_config_state_t s;

	std::vector<ECModule *>::iterator iter = modules.begin();
	int i = 0;
	slaves_not_operational = 0;
	while (iter != modules.end()){
		ECModule *m = *iter++;
	    ecrt_slave_config_state(m->slave_config, &s);
			if (!s.online) ++slaves_not_operational;
			if (!s.online) ++slaves_offline;
	
			char buf[100];
	    if (s.al_state != m->slave_config_state.al_state) {
	        std::cout << m->name << ": State 0x" << std::ios::hex <<  s.al_state << ".\n";
					snprintf(buf, 100, "Slave %d (%s) changed state was 02x%x now 02x%x", i, m->name.c_str(), 
						m->slave_config_state.al_state, s.al_state);
				MessageLog::instance()->add(buf);
			}
	    if (s.online != m->slave_config_state.online) {
	        std::cout << m->name << ": " << (s.online ? "online" : "offline") << "\n";
				snprintf(buf, 100, "Slave %d (%s) changed online state: was %s, now %s", i, m->name.c_str(), 
					m->slave_config_state.online?"online":"offline", s.online?"online":"offline");
				MessageLog::instance()->add(buf);
			}
	    if (s.operational != m->slave_config_state.operational) {
	        std::cout << m->name << ": " << (s.operational ? "" : "Not ") << "operational\n";
				snprintf(buf, 100, "Slave %d (%s) changed operational state: was %s operational, now %s operational", i, m->name.c_str(), 
					m->slave_config_state.operational?"":"not ", s.operational?"":"not ");
				MessageLog::instance()->add(buf);
			}

	    m->slave_config_state = s;
		++i;
	}
#endif
}

bool ECInterface::start() {
#ifdef ETHERCATD
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
#endif
	return true;
}

bool ECInterface::stop() {
#ifdef ETHERCATD
    struct itimerval tv;
    tv.it_interval.tv_sec = 0;
    tv.it_interval.tv_usec = 0;
    tv.it_value.tv_sec = 0;
    tv.it_value.tv_usec = 0;
    if (setitimer(ITIMER_REAL, &tv, NULL)) {
		std::cerr << "Failed to stop timer: " << strerror(errno) << "\n";
        return false;
    }
#endif
	return true;
}

#ifdef USE_ETHERCAT

#include <Command.h>
int tool_main(int argc, char **argv);
typedef list<Command *> CommandList;
extern CommandList commandList;


// in a real environment we can look for devices on the bus

ec_pdo_entry_info_t *c_entries = 0;
ec_pdo_info_t *c_pdos = 0;
ec_sync_info_t *c_syncs = 0;
EntryDetails *c_entry_details = 0;


cJSON *generateSlaveCStruct(MasterDevice &m, const ec_ioctl_slave_t &slave, bool reconfigure)
{
    ec_ioctl_slave_sync_t sync;
    ec_ioctl_slave_sync_pdo_t pdo;
    ec_ioctl_slave_sync_pdo_entry_t entry;
    unsigned int i, j, k, pdo_pos = 0, entry_pos = 0;

	const unsigned int estimated_max_entries = 128;
	const unsigned int estimated_max_pdos = 32;
	const unsigned int estimated_max_syncs = 32;
    unsigned int total_entries = 0, total_pdos = 0, total_syncs = 0;
    
	cJSON *root = cJSON_CreateObject();
	cJSON_AddNumberToObject(root, "position", slave.position);
	cJSON_AddNumberToObject(root, "vendor_id", slave.vendor_id);
	cJSON_AddNumberToObject(root, "revision_number", slave.revision_number);
	cJSON_AddNumberToObject(root, "alias", slave.alias);
	cJSON_AddNumberToObject(root, "drawn_current", slave.current_on_ebus);
	cJSON_AddStringToObject(root, "tab", "Modules");
	cJSON_AddStringToObject(root, "class", "MODULE");
	char *name = strdup(slave.name);
	int name_len = strlen(name);
	for (int i=0; i<name_len; ++i) name[i] &= 127;
	cJSON_AddStringToObject(root, "name", name);
	free(name);
    
    if (slave.sync_count) {
		// add pdo entries for this slave
		// note the assumptions here about the maximum number of entries, pdos and syncs we expect
		const int c_entries_size = sizeof(ec_pdo_entry_info_t) * estimated_max_entries;
		c_entries = (ec_pdo_entry_info_t *) malloc(c_entries_size);
		memset(c_entries, 0, c_entries_size);
        
		c_entry_details = new EntryDetails[estimated_max_entries];
        
		const int c_pdos_size = sizeof(ec_pdo_info_t) * estimated_max_pdos;
		c_pdos = (ec_pdo_info_t *) malloc(c_pdos_size);
		memset(c_pdos, 0, c_pdos_size);
        
		const int c_syncs_size = sizeof(ec_sync_info_t) * estimated_max_syncs;
		c_syncs = (ec_sync_info_t *) malloc(c_syncs_size);
		memset(c_syncs, 0, c_syncs_size);
        
		total_syncs += slave.sync_count;
		assert(total_syncs < estimated_max_syncs);
		cJSON *json_syncs = cJSON_CreateArray();
	    for (i = 0; i < slave.sync_count; i++) {
			cJSON *json_sync = cJSON_CreateObject();
	        m.getSync(&sync, slave.position, i);
			c_syncs[i].index = sync.sync_index;
			c_syncs[i].dir = EC_READ_BIT(&sync.control_register, 2) ? EC_DIR_OUTPUT : EC_DIR_INPUT;
			c_syncs[i].n_pdos = (unsigned int) sync.pdo_count;
			cJSON_AddNumberToObject(json_sync, "index", c_syncs[i].index);
			cJSON_AddStringToObject(json_sync, "direction", (c_syncs[i].dir == EC_DIR_OUTPUT) ? "Output" : "Input");
			if (sync.pdo_count)
				c_syncs[i].pdos = c_pdos + pdo_pos;
			else
				c_syncs[i].pdos = 0;
			c_syncs[i].watchdog_mode 
				= EC_READ_BIT(&sync.control_register, 6) ? EC_WD_ENABLE : EC_WD_DISABLE;
            
			total_pdos += sync.pdo_count;
			assert(total_pdos < estimated_max_pdos);
			if (sync.pdo_count) {
				cJSON* json_pdos = cJSON_CreateArray();
		        for (j = 0; j < sync.pdo_count; j++) {
		            m.getPdo(&pdo, slave.position, i, j);
					cJSON* json_pdo = cJSON_CreateObject();
					cJSON_AddNumberToObject(json_pdo, "index", pdo.index);
					cJSON_AddNumberToObject(json_pdo, "entry_count", pdo.entry_count);
					cJSON_AddStringToObject(json_pdo, "name", (const char *)pdo.name);
	       //std::cout << "sync: " << i << " pdo: " << j << " " <<pdo.name << ": ";
					c_pdos[j + pdo_pos].index = pdo.index;
					c_pdos[j + pdo_pos].n_entries = (unsigned int) pdo.entry_count;
					if (pdo.entry_count)
						c_pdos[j + pdo_pos].entries = c_entries + entry_pos;
					else
						c_pdos[j + pdo_pos].entries = 0;
	                
					if (pdo.entry_count) {
						cJSON *json_entries = cJSON_CreateArray();
						total_entries += pdo.entry_count;
						assert(total_entries < estimated_max_entries);
		            	for (k = 0; k < pdo.entry_count; k++) {
							cJSON *json_entry = cJSON_CreateObject();
		            	    m.getPdoEntry(&entry, slave.position, i, j, k);
#if 0
							std::cout << " entry: " << k 
								<< "{" 
								<< entry_pos << ", "
								<< std::hex << (int)entry.index <<", " 
								<< (int)entry.subindex<<", " 
								<< (int)entry.bit_length <<", "
								<<'"' << entry.name<<"\"}";
#endif
							c_entries[entry_pos].index = entry.index;
							c_entries[entry_pos].subindex = entry.subindex;
							c_entries[entry_pos].bit_length = entry.bit_length;
							c_entry_details[entry_pos].name = (const char *)pdo.name;
							c_entry_details[entry_pos].name += " ";
							c_entry_details[entry_pos].name += (const char *)entry.name;
							c_entry_details[entry_pos].entry_index = entry_pos;
							c_entry_details[entry_pos].pdo_index = j + pdo_pos;
							c_entry_details[entry_pos].sm_index = i;

							cJSON_AddNumberToObject(json_entry, "pos", entry_pos);
							cJSON_AddNumberToObject(json_entry, "index", entry.index);
							cJSON_AddStringToObject(json_entry, "name", c_entry_details[entry_pos].name.c_str());
							cJSON_AddNumberToObject(json_entry, "subindex", entry.subindex);
							cJSON_AddNumberToObject(json_entry, "bit_length", entry.bit_length);
		            		++entry_pos;
	
							cJSON_AddItemToArray(json_entries, json_entry);
		            	}
						cJSON_AddItemToObject(json_pdo, "entries", json_entries);
					}
			    	//std::cout << "\n";
					cJSON_AddItemToArray(json_pdos, json_pdo);
		        }
				cJSON_AddItemToObject(json_sync, "pdos", json_pdos);
    		}        
	        pdo_pos += sync.pdo_count;
			cJSON_AddItemToArray(json_syncs, json_sync);
	    }
		cJSON_AddItemToObject(root, "sync_managers", json_syncs);
		c_syncs[slave.sync_count].index = 0xff;
	}
	else {
		c_syncs = 0;
		c_pdos = 0;
		c_entries = 0;
	}
	if (reconfigure) {
		ECModule *module = new ECModule();
		module->name = slave.name;
		module->alias = 0;
		module->position = slave.position;
		module->vendor_id = slave.vendor_id;
		module->product_code = slave.product_code;
		module->syncs = c_syncs;
		module->pdos = c_pdos;
		module->pdo_entries = c_entries;
		module->sync_count = slave.sync_count;
		module->entry_details = c_entry_details;
		module->num_entries = total_entries;
		if (!ECInterface::instance()->addModule(module, reconfigure)) delete module; // module may be already registered
	}
	else {
		if (c_entries) free(c_entries);
		if (c_pdos) free(c_pdos);
		if (c_syncs) free(c_syncs);
		delete[] c_entry_details;
	}
	
	//std::stringstream result;
	//result << "slave: " << slave.position << "\t"
	//	<< "syncs: " << i << " pdos: " << pdo_pos << " entries: " << entry_pos << "\n";
	//result << cJSON_Print(root);
	//return result.str();

	return root;
}

char *collectSlaveConfig(bool reconfigure)
{
	cJSON *root = cJSON_CreateArray();
    MasterDevice m(0);
    m.open(MasterDevice::Read);
    
    ec_ioctl_master_t master;
    ec_ioctl_slave_t slave;

	memset(&master, 0, sizeof(ec_ioctl_master_t));
	memset(&slave, 0, sizeof(ec_ioctl_slave_t));
    m.getMaster(&master);
    
	for (unsigned int i=0; i<master.slave_count; i++) {
		m.getSlave(&slave, i);
        cJSON_AddItemToArray(root, generateSlaveCStruct(m, slave, true));
    }
	if (reconfigure)
		ECInterface::instance()->addModule(0, true);
	char *res = cJSON_Print(root);
	cJSON_Delete(root);

	/* save a description of the bus configuration */
	std::ofstream logfile;
	logfile.open("/tmp/ecat.log", std::ofstream::out /* | std::ofstream::app */);
	logfile << res << "\n";
    logfile.close();

	return res;
}


    bool IODCommandEtherCATTool::run(std::vector<Value> &params) {
        if (params.size() > 1) {
            int argc = params.size();
            char **argv = (char**)malloc((argc+1) * sizeof(char*));
            for (int i=0; i<argc; ++i) {
                argv[i] = strdup(params[i].asString().c_str());
            }
            argv[argc] = 0;
            std::stringstream tool_output;
            std::stringstream tool_err;
            std::streambuf *old_cout = std::cout.rdbuf(tool_output.rdbuf());
            std::streambuf *old_cerr = std::cerr.rdbuf(tool_err.rdbuf());
            int res = tool_main(argc, argv);
			std::cout.rdbuf(old_cout);
			std::cerr.rdbuf(old_cerr);
            std::cout << tool_output.str() << "\n";
            std::cerr << tool_err.str() << "\n";
            for (int i=0; i<argc; ++i) {
                free(argv[i]);
            }
            free(argv);
            result_str = tool_output.str();
			//cleanup the command list left over from the ethercat tool
			CommandList::iterator iter = commandList.begin();
			while (iter != commandList.end()) {
				Command *cmd = *iter;
				iter = commandList.erase(iter);
				delete cmd;
			}
		
            return true;
        }
        else {
            error_str = "Usage: EC command [params]";
            return false;
        }
    }

    bool IODCommandGetSlaveConfig::run(std::vector<Value> &params) {
        char *res = collectSlaveConfig(false);
        if (res) {
            result_str = res;
			free(res);
            return true;
        }
        else {
            error_str = "JSON Error";
            return false;
        }
    }

bool IODCommandMasterInfo::run(std::vector<Value> &params) {
    //const ec_master_t *master = ECInterface::instance()->getMaster();
    const ec_master_state_t *master_state = ECInterface::instance()->getMasterState();
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "slave_count", master_state->slaves_responding);
    cJSON_AddNumberToObject(root, "link_up", master_state->link_up);
    std::stringstream ss;
    statistics->io_scan_time.report(ss);
    statistics->points_processing.report(ss);
    statistics->machine_processing.report(ss);
    statistics->dispatch_processing.report(ss);
    statistics->auto_states.report(ss);
    Statistic::reportAll(ss);
    ss << std::flush;
    cJSON_AddStringToObject(root, "statistics", ss.str().c_str());
    
    char *res = cJSON_Print(root);
    bool done;
    if (res) {
        result_str = res;
        free(res);
        done = true;
    }
    else {
        error_str = "JSON error";
        done = false;
    }
    cJSON_Delete(root);
    return done;
}

#else

bool IODCommandEtherCATTool::run(std::vector<Value> &params) {
    error_str = "EtherCAT Tool is not available";
    return false;
}

bool IODCommandGetSlaveConfig::run(std::vector<Value> &params) {
    cJSON *root = cJSON_CreateObject();
    char *res = cJSON_Print(root);
    cJSON_Delete(root);
    if (res) {
        result_str = res;
        free(res);
        return true;
    }
    else {
        error_str = "JSON Error";
        return false;
    }
}

bool IODCommandMasterInfo::run(std::vector<Value> &params) {
    //const ec_master_t *master = ECInterface::instance()->getMaster();
    //const ec_master_state_t *master_state = ECInterface::instance()->getMasterState();
    extern Statistics *statistics;
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "slave_count", 0);
    cJSON_AddNumberToObject(root, "link_up", 0);
    std::stringstream ss;
    statistics->io_scan_time.report(ss);
    statistics->points_processing.report(ss);
    statistics->machine_processing.report(ss);
    statistics->dispatch_processing.report(ss);
    statistics->auto_states.report(ss);
    Statistic::reportAll(ss);
    ss << std::flush;
    cJSON_AddStringToObject(root, "statistics", ss.str().c_str());
    
    char *res = cJSON_Print(root);
    cJSON_Delete(root);
    bool done;
    if (res) {
        result_str = res;
        free(res);
        done = true;
    }
    else {
        error_str = "JSON error";
        done = false;
    }
    return done;
}

#endif

