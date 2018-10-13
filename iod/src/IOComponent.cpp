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

#include "IOComponent.h"
#include <string>
#include <iostream>
#include <iomanip>
#include <boost/thread/mutex.hpp>
#include "MachineInstance.h"
#include "MessagingInterface.h"
#include <netinet/in.h>
#include "Logger.h"
#include <string.h>
#include <algorithm>
#ifndef EC_SIMULATOR
#include <ecrt.h>
#endif
#include "buffering.c"
#include "ProcessingThread.h"

#define VERBOSE_DEBUG 0
//static void MEMCHECK() { char *x = new char[12358]; memset(x,0,12358); delete[] x; }

/* byte swapping macros using either custom code or the network byte order std functions */
#if __BIGENDIAN
#if 0
#define toU16(m,v) \
    *((uint16_t*)m) = \
        ((((uint16_t)v)&0xff00) >> 8) \
      | ((((uint16_t)v)&0x00ff) << 8)
#define fromU16(m) \
    ( ((*(uint16_t*)m) & 0xff00) >> 8) \
      | ( (*((uint16_t*)m) & 0x00ff) << 8)
#define toU32(m,v) \
    *((uint32_t*)m) = \
        ((((uint32_t)v)&0xff000000) >> 24) \
      | ((((uint32_t)v)&0x00ff0000) >>  8) \
      | ((((uint32_t)v)&0x0000ff00) <<  8) \
      | ((((uint32_t)v)&0x000000ff) << 24)
#define fromU32(m) \
    ( ((*(uint32_t*)m) & 0xff000000) >> 24) \
  | ( (*((uint32_t*)m) & 0x00ff0000) >> 8) \
  | ( ((*(uint32_t*)m) & 0x0000ff00) << 8) \
  | ( (*((uint32_t*)m) & 0x000000ff) << 24)

#else
#include <netinet/in.h>
#define toU16(m,v) *(uint16_t*)(m) = htons( (v) )
#define fromU16(m) ntohs( *(uint16_t*)m)
#define toU32(m,v) *(uint32_t*)(m) = htonl( (v) )
#define fromU32(m) ntohl( *(uint32_t*)m)
#endif
#else
#define toU16(m,v) EC_WRITE_U16(m,v)
#define fromU16(m) EC_READ_U16(m)
#define toU32(m,v) EC_WRITE_U32(m,v)
#define fromU32(m) EC_READ_U32(m)
#endif

std::list<IOComponent *> IOComponent::processing_queue;
std::map<std::string, IOAddress> IOComponent::io_names;
static uint64_t current_time;
uint64_t IOComponent::io_clock;	// clock value when ProcessAll is called
uint64_t IOComponent::global_clock; // clock value last received
size_t IOComponent::process_data_size = 0;
uint8_t *IOComponent::io_process_data = 0;
uint8_t *IOComponent::io_process_mask = 0;
uint8_t *IOComponent::update_data = 0;
uint8_t *IOComponent::default_data = 0;
uint8_t *IOComponent::default_mask = 0;
static uint8_t *last_process_data = 0;
int IOComponent::outputs_waiting = 0;

boost::recursive_mutex processing_queue_mutex;
boost::recursive_mutex IOComponent::io_names_mutex;
boost::unique_lock<boost::recursive_mutex> io_lock(processing_queue_mutex, boost::defer_lock);

void IOComponent::lock() {
	io_lock.lock();
}

void IOComponent::unlock() {
	io_lock.unlock();
}

unsigned int IOComponent::max_offset = 0;
unsigned int IOComponent::min_offset = 1000000L;
static std::vector<IOComponent*> *indexed_components = 0;
IOComponent::HardwareState IOComponent::hardware_state = s_hardware_preinit;

/* these items define a process for polling certain IOComponents on a regular basis */

std::set<IOComponent*>regular_polls;

static uint64_t last_sample = 0; // retains a timestamp for the last sample

// as long as there has been a sufficient delay, run the filter for each
// of the nominated components.

void handle_io_sampling(uint64_t io_clock) {
	uint64_t now = microsecs();
	if (now - last_sample < 10000) return;
	last_sample = now;
	std::set <IOComponent*>::iterator iter = regular_polls.begin();
	while (iter != regular_polls.end() ) {
		IOComponent *ioc = *iter++;
		ioc->read_time = io_clock;
		ioc->filter(ioc->address.value);
	}
}

#if VERBOSE_DEBUG
static void display(uint8_t *p, unsigned int count = 0);
#endif

void set_bit(uint8_t *q, unsigned int bitpos, unsigned int val) {
	uint8_t bitmask = 1<<bitpos;
	if (val) *q |= bitmask; else *q &= (uint8_t)(0xff - bitmask);
}

void copyMaskedBits(uint8_t *dest, uint8_t*src, uint8_t *mask, size_t len) {

	uint8_t*result = dest;
#if VERBOSE_DEBUG 
	std::cout << "copying masked bits: \n";
	display(dest, len); std::cout << "\n";
	display(src, len); std::cout << "\n";
	display(mask, len); std::cout << "\n";
#endif
	size_t count = len;
	while (count--) {
		uint8_t bitmask = 0x80;
		for (int i=0; i<8; ++i) {
			if ( *mask & bitmask ) {
				if (*src & bitmask)
					*dest |= bitmask;
				else
					*dest &= (uint8_t)(0xff - bitmask);
			}
			bitmask = bitmask >> 1;
		}
		++src; ++dest; ++mask;
	}
#if VERBOSE_DEBUG
	display(result, len); std::cout << "\n";
#endif
}

IOUpdate::~IOUpdate() {
	assert(mask_);
//	delete mask_;
}

// these components need to synchronise with clockwork
std::set<IOComponent*> updatedComponentsIn;
std::set<IOComponent*> updatedComponentsOut;
bool IOComponent::updates_sent = false;

IOComponent::HardwareState IOComponent::getHardwareState() { return hardware_state; }
void IOComponent::setHardwareState(IOComponent::HardwareState state) { 
	const char *hw_state_str = "Hardware preinitialisatio";
	if (state == s_hardware_init)
		hw_state_str = "Hardware Initialisation";
	else if (state == s_operational)
		hw_state_str = "Operational";
	NB_MSG << "Hardware state  set to " << hw_state_str << "\n";
	hardware_state = state; 
}

IOComponent::DeviceList IOComponent::devices;

uint8_t *IOComponent::getProcessData() { 
	boost::recursive_mutex::scoped_lock lock(processing_queue_mutex);
  return io_process_data; 
}
uint8_t *IOComponent::getProcessMask() { 
	boost::recursive_mutex::scoped_lock lock(processing_queue_mutex);
  return io_process_mask; 
}
uint8_t *IOComponent::getDefaultData() { 
	boost::recursive_mutex::scoped_lock lock(processing_queue_mutex);
  return default_data; 
}
uint8_t *IOComponent::getDefaultMask() { 
	boost::recursive_mutex::scoped_lock lock(processing_queue_mutex);
  return default_mask; 
}

void IOComponent::reset() {
	boost::recursive_mutex::scoped_lock lock(processing_queue_mutex);
	processing_queue.clear();
	regular_polls.clear();
	std::list<MachineInstance*>::iterator iter = MachineInstance::begin();
	while (iter != MachineInstance::end()) {
		MachineInstance *m = *iter++;
		if (m->io_interface) { delete m->io_interface; m->io_interface = 0; }
	}
	if (io_process_data) delete[] io_process_data; io_process_data = 0;
	if (io_process_mask) delete[] io_process_mask; io_process_mask = 0;
	if (update_data) delete[] update_data; update_data = 0;
	if (default_data) delete[] default_data; default_data = 0;
	if (default_mask) delete[] default_mask; default_mask = 0;
	if (last_process_data) delete[] last_process_data; last_process_data = 0;
}

IOComponent::IOComponent(IOAddress addr) 
		: last_event(e_none), address(addr), io_index(-1), raw_value(0) { 
	boost::recursive_mutex::scoped_lock lock(processing_queue_mutex);
	processing_queue.push_back(this); 
	// the io_index is the bit offset of the first bit in this objects address space
	io_index = addr.io_offset*8 + addr.io_bitpos;
}

IOComponent::IOComponent() : last_event(e_none), io_index(-1), raw_value(0), direction_(DirBidirectional) { 
	boost::recursive_mutex::scoped_lock lock(processing_queue_mutex);
	processing_queue.push_back(this); 
	// use the same io-updated index as the processing queue position
}

// most io devices set their initial state to reflect the 
// current hardware state
// output devices reset to an 'off' state
void IOComponent::setInitialState() {
}

void IOComponent::updatesSent(bool which){
	//if (which) std::cout << "updates sent\n";
	updates_sent = which;
}

bool IOComponent::updatesToSend() { return updates_sent == false; }

IOComponent* IOComponent::lookup_device(const std::string name) {
    IOComponent::DeviceList::iterator device_iter = IOComponent::devices.find(name);
    if (device_iter != IOComponent::devices.end())
        return (*device_iter).second;
    return 0;
}

#if VERBOSE_DEBUG
static void display(uint8_t *p, unsigned int count) {
	int max = IOComponent::getMaxIOOffset();
	int min = IOComponent::getMinIOOffset();
	if (count == 0)
		for (int i=min; i<=max; ++i) 
			std::cout << std::setw(2) << std::setfill('0') << std::hex << (unsigned int)p[i];
	else
		for (unsigned int i=0; i<count; ++i) 
			std::cout << std::setw(2) << std::setfill('0') << std::hex << (unsigned int)p[i];
	std::cout << std::dec;
}
#endif

uint8_t* IOComponent::getUpdateData() {
	assert(io_process_data);
	if (!update_data){ 
		update_data = new uint8_t[process_data_size];
		memcpy(update_data, io_process_data, process_data_size);
	}
	return update_data;
}

void IOComponent::processAll(uint64_t clock, size_t data_size, uint8_t *mask, uint8_t *data, 
			std::set<IOComponent *> &updated_machines) {
	io_clock = clock;
	// receive process data updates and mask to yield updated components
    struct timeval now;
    gettimeofday(&now, NULL);
    current_time = now.tv_sec * 1000000 + now.tv_usec;

	assert(data != io_process_data);

#if VERBOSE_DEBUG
	for (size_t ii=0; ii<data_size; ++ii) if (mask[ii]) {
		std::cout << "IOComponent::processAll()\n";
		std::cout << "size: " << data_size << "\n";
		std::cout << "pdta: "; display( io_process_data, data_size); std::cout << "\n";
		std::cout << "pmsk: "; display( io_process_mask, data_size); std::cout << "\n";
		std::cout << "data: "; display( data, data_size); std::cout << "\n";
		std::cout << "mask: "; display( mask, data_size); std::cout << "\n";
		break;
	}
#endif

	assert(data_size == process_data_size);

	if (hardware_state == s_hardware_preinit) {
		// the initial process data has arrived from EtherCAT. keep the previous data as the defaults
		// so they can be applied asap
		memcpy(io_process_data, data, process_data_size);
		//setHardwareState(s_hardware_init);
		return;
	}

	// step through the incoming mask and update bits in process data
	uint8_t *p = data;
	uint8_t *m = mask;
	uint8_t *q = io_process_data;
	IOComponent *just_added = 0;
	for (unsigned int i=0; i<process_data_size; ++i) {
		if (!last_process_data) {
			if (*m) notifyComponentsAt(i);
		}
//		if (*m && *p==*q) {
//			std::cout<<"warning: incoming_data == process_data but mask indicates a change at byte "
//			<< (int)(m-mask) << std::setw(2) <<  std::hex << " value: 0x" << (int)(*m) << std::dec << "\n";
//		}
		if (*p != *q && *m) { // copy masked bits if any
			uint8_t bitmask = 0x01;
			int j = 0;
			// check each bit against the mask and if the mask if
			// set, check if the bit has changed. If the bit has
			// changed, notify components that use this bit and 
			// update the bit
			while (bitmask) {
				if ( *m & bitmask) {
					//std::cout << "looking up " << i << ":" << j << "\n";
					IOComponent *ioc = (*indexed_components)[ i*8+j ];
					if (ioc && ioc != just_added) {
						just_added = ioc;
						//if (!ioc) std::cout << "no component at " << i << ":" << j << " found\n"; 
						//else std::cout << "found " << ioc->io_name << "\n";
#if 0
						if (ioc && ioc->last_event != e_none) { 
							// pending locally sourced change on this io
							std::cout << " adding " << ioc->io_name << " due to event " << ioc->last_event << "\n";
							updatedComponentsIn.insert(ioc);
						}
#endif
						if ( (*p & bitmask) != (*q & bitmask) ) {
							// remotely source change on this io
							if (ioc) {
								//std::cout << " adding " << ioc->io_name << " due to bit change\n";
								boost::recursive_mutex::scoped_lock lock(processing_queue_mutex);
								updatedComponentsIn.insert(ioc);
							}

							if (*p & bitmask) *q |= bitmask; 
							else *q &= (uint8_t)(0xff - bitmask);
						}
						//else { 
						//	std::cout << "no change " << (unsigned int)*p << " vs " << 
						//		(unsigned int)*q << "\n";}
					}
					else {
						if (!ioc) std::cout << "IOComponent::processAll(): no io component at " << i <<":" <<j <<" but mask bit is set\n";
						if ( (*p & bitmask) != (*q & bitmask) ) {
							if (*p & bitmask) *q |= bitmask; 
							else *q &= (uint8_t)(0xff - bitmask);
						}
					}
				}
				bitmask = bitmask << 1;
				++j;
			}
		}
		++p; ++q; ++m;
	}
	
	if (hardware_state == s_operational) {
		// save the domain data for the next check
		if (!last_process_data) {
			last_process_data = new uint8_t[process_data_size];
		}
		memcpy(last_process_data, io_process_data, process_data_size);
	}
    
	{
		boost::recursive_mutex::scoped_lock lock(processing_queue_mutex);
	if (!updatedComponentsIn.size())
		return;
//	std::cout << updatedComponentsIn.size() << " component updates from hardware\n";
#ifdef USE_EXPERIMENTAL_IDLE_LOOP
	// look at the components that changed and remove them from the outgoing queue as long as the 
	// outputs have been sent to the hardware
	std::set<IOComponent*>::iterator iter = updatedComponentsIn.begin();
	while (iter != updatedComponentsIn.end()) {
		IOComponent *ioc = *iter++;
		ioc->read_time = io_clock;
		//std::cerr << "processing " << ioc->io_name << " time: " << ioc->read_time << "\n";
		updatedComponentsIn.erase(ioc); 
		if (updates_sent && updatedComponentsOut.count(ioc)) {
			//std::cout << "output request for " << ioc->io_name << " resolved\n";
			updatedComponentsOut.erase(ioc);
		}
		//else std::cout << "still waiting for " << ioc->io_name << " event: " << ioc->last_event << "\n";
		updated_machines.insert(ioc);
	}
	// for machines with updates to send, if these machines already have the same value
	// as the hardware (and updates have been sent) we also remove them from the
	// outgoing queue
	if (updates_sent) {
		iter = updatedComponentsOut.begin();
		while (iter != updatedComponentsOut.end()) {
			IOComponent *ioc = *iter++;
			if (ioc->pending_value == (uint32_t)ioc->address.value) {
				//std::cout << "output request for " << ioc->io_name << " cleared as hardware value matches\n";
				updatedComponentsOut.erase(ioc);
			}
		}
	}
#else
	std::list<IOComponent *>::iterator iter = processing_queue.begin();
	while (iter != processing_queue.end()) {
		IOComponent *ioc = *iter++;
		ioc->read_time = io_clock;
		ioc->idle();
	}
#endif
	}
	outputs_waiting = updatedComponentsOut.size();
}

IOAddress IOComponent::add_io_entry(const char *name, unsigned int module_pos, 
		unsigned int io_offset, unsigned int bit_offset, 
		unsigned int entry_pos, unsigned int bit_len, bool signed_value){
	IOAddress addr(module_pos, io_offset, bit_offset, entry_pos, bit_len);
	addr.module_position = module_pos;
	addr.io_offset = io_offset;
	addr.io_bitpos = bit_offset;
    addr.entry_position = entry_pos;
	addr.description = name;
	addr.is_signed = signed_value;
	char buf[80];
	snprintf(buf, 80, "io:%s_%d", name, module_pos);

	{
	boost::recursive_mutex::scoped_lock lock(io_names_mutex);
	if (io_names.find(std::string(buf)) != io_names.end())  {
		std::cerr << "IOComponent::add_io_entry: warning - an IO component named " 
			<< name << " already existed on module " << module_pos << "\n";
	}
	io_names[buf] = addr;
	}
	return addr;
}

void IOComponent::remove_io_module(int pos) {
	//TBD add a mutex on io_names and code to remove entries for modules at this position
	boost::recursive_mutex::scoped_lock lock(io_names_mutex);
	io_names.clear();
}

void IOComponent::add_publisher(const char *name, const char *topic, const char *message) {
    MQTTTopic mqt(topic, message);
    mqt.publisher = true;
//    pubs[name] = mqt;
}

void IOComponent::add_subscriber(const char *name, const char *topic) {
    MQTTTopic mqt(topic, "");
    mqt.publisher = false;
//    subs[name] = mqt;
}

void IOComponent::setupProperties(MachineInstance *m) {
}


std::ostream &IOComponent::operator<<(std::ostream &out) const{
	out << "readtime: " << (io_clock-read_time) << " [" << address.description<<", "
		<<address.module_position << " "
		<< address.io_offset << ':' 
		<< address.io_bitpos << "." 
		<< address.bitlen << "]=" 
		<< address.value;
  if (address.bitlen == 1 && io_process_data)  {
    out << " (" << (bool)(io_process_data[address.io_offset] & (1<<address.io_bitpos)) << ")";
  }
	return out;
}

std::ostream &operator<<(std::ostream&out, const IOComponent &ioc) {
	return ioc.operator<<(out);
}

#ifdef EC_SIMULATOR
unsigned char mem[1000];
#define EC_READ_BIT(offset, bitpos) ( (*offset) & (1 << (bitpos)) )
#define EC_WRITE_BIT(offset, bitpos, val) *(offset) |=  ( 1<< (bitpos))
#define EC_READ_S8(offset) 0
#define EC_READ_S16(offset) 0
#define EC_READ_S32(offset) 0
#define EC_READ_U8(offset) 0
#define EC_READ_U16(offset) 0
#define EC_READ_U32(offset) 0
#define EC_WRITE_U8(offset, val) 0
#define EC_WRITE_U16(offset, val) 0
#define EC_WRITE_U32(offset, val) 0
#endif

int32_t IOComponent::filter(int32_t val) {
    return val;
}


class InputFilterSettings {
public:
    bool property_changed;
    CircularBuffer *positions;
    int32_t last_sent;			// this is the value to send unless the read value moves away from the mean
    int32_t prev_sent;			// this is previous value of last_sent
	uint64_t last_time;			// the last time we calculated speed;_
    uint16_t buffer_len;		// the maximum length of the circular buffer
	const long *tolerance;		// some filters use a tolerance settable by the user in the "tolerance" property
	double *filter_coeff;		// the Butterworth filter uses these coefficients
	const long *filter_len;		// the user can adjust the filter length of some filters via a "filter_len" property
	const long *filter_type;	// the user can select the filter using a "filter" property
	const long *position_history; // the amount of position history to use in determining movement
	const long *speed_tolerance; // the tolerance used in determining movement
	unsigned int butterworth_len;	// the number of coefficients in the Butterworth filter
	long speed;					// the current estimated speed
	static long default_tolerance;	// a default value for filter_len
	static long default_filter_len;	// a default value for filter_len
	static long default_speed_filter_len;	// a default value for speed_filter_len
	static long default_position_history;	// a default value for position_history
	static long default_speed_tolerance;	// a default value for speed_tolerance
	FloatBuffer speeds;
	int rate_len;
    
    InputFilterSettings() :property_changed(true), positions(0), 
			last_sent(0), prev_sent(0), last_time(0),
			buffer_len(200), tolerance(&default_tolerance), filter_coeff(0),filter_len(&default_filter_len), 
			filter_type(0),
			position_history(&default_position_history), speed_tolerance(&default_speed_tolerance),
			speed(0), speeds(4), rate_len(4) {

		double c[] = {0.081,0.215,0.541,0.865,1,0.865,0.541,0.215,0.081};
		butterworth_len = sizeof(c) / sizeof(double);
		filter_coeff = new double[butterworth_len];
		memmove(filter_coeff, c, sizeof(c));
		positions = createBuffer(buffer_len);
	}

	void update(uint64_t read_time) {
		if (prev_sent == 0) prev_sent = last_sent;
		if (last_time == 0) {
			last_time = read_time;
			prev_sent = last_sent;
			speed = 0.0;
			speeds.append(speed);
		}
		else if (read_time - last_time >= 10000) {
	/*
			double dt = (double)(read_time - last_time);
			double dv = (double)(last_sent - prev_sent);
			speed = (long)( dv / dt * 1000000.0 );
	*/
			rate_len = findMovement(positions, 20, *position_history);
			if (rate_len < *position_history) {
				speed = 1000000.0 * rate(positions, (rate_len<4)? 4 : rate_len);
			}
			else speed = 0.0;
			speeds.append(speed);
			last_time = read_time;
			prev_sent = last_sent;
		}
	}

	double filter() {
		unsigned int filter_length = *filter_len;
		if ((unsigned int)length(positions) < filter_length) return getBufferValue(positions, 0);
		double c[] = {0.081,0.215,0.541,0.865,1,0.865,0.541,0.215,0.081};
		double res = 0;
		for (unsigned int i=0; i < filter_length; ++i) {
			double f = (double)getBufferValue(positions, i);
			//printf(" %.3f,%.3f ",f, f*c[i]); 
			res += f * c[i];
		}
		//printf(" %.3f\n",res); 

		return res;
	}
};

long InputFilterSettings::default_tolerance = 8;	// a default value for filter_len
long InputFilterSettings::default_filter_len = 12;	// a default value for filter_len
long InputFilterSettings::default_speed_filter_len = 4;	// a default value for speed_filter_len
long InputFilterSettings::default_position_history = 20;	// a default value for position_history
long InputFilterSettings::default_speed_tolerance = 20;	// a default value for speed_tolerance

AnalogueInput::AnalogueInput(IOAddress addr) : IOComponent(addr) { 
	config = new InputFilterSettings();
	direction_ = DirInput;
	regular_polls.insert(this);
}

void AnalogueInput::setupProperties(MachineInstance *m) {
	const Value &v = m->getValue("tolerance");
	if (v.kind == Value::t_integer) {
		config->tolerance = &v.iValue;
	}
	const Value &v2 = m->getValue("filter");
	if (v2.kind == Value::t_integer) {
		config->filter_type = &v2.iValue;
	}
	const Value &v3 = m->getValue("filter_len");
	if (v3.kind == Value::t_integer) {
		config->filter_len = &v3.iValue;
	}
	const Value &v4 = m->getValue("speed_tolerance");
	if (v4.kind == Value::t_integer) {
		config->speed_tolerance= &v4.iValue;
	}
	const Value &v5 = m->getValue("position_history");
	if (v5.kind == Value::t_integer) {
		config->position_history = &v5.iValue;
	}
}

int32_t AnalogueInput::filter(int32_t raw) {
    if (config->property_changed) {
        config->property_changed = false;
    }
	addSample(config->positions, (long)read_time, (double)raw);

	if (config->filter_type && *config->filter_type == 0 ) {
		config->last_sent = raw;
	}
	else if ( !config->filter_type || (config->filter_type && *config->filter_type == 1))  {
		int32_t mean = (bufferAverage(config->positions, *config->filter_len) + 0.5f);
		long delta = abs(mean - config->last_sent);
		if ( delta >= *config->tolerance) {
			config->last_sent = mean;
		}
	}
	else if (config->filter_type && *config->filter_type == 2) {
		long res = (long)config->filter();
		config->last_sent = (int32_t)(res / config->butterworth_len *2);
	}
	
	config->update(read_time);
	
#if 1
	/* most machines reading sensor values will be prompted when teh
		sensor value changes, depending on whether this filter yields a
		changed value. Some systems such as plugins that operate on 
		their own clock may wish to ignore the filtered value and 
		access the raw io value and read time but note that these 
		values do not cause notifications when they change
	*/

	

	std::list<MachineInstance*>::iterator owners_iter = owners.begin();
	while (owners_iter != owners.end()) {
		MachineInstance *o = *owners_iter++;
		o->properties.add("IOTIME", (long)read_time, SymbolTable::ST_REPLACE);
		o->properties.add("DurationTolerance", config->rate_len, SymbolTable::ST_REPLACE);
		o->properties.add("VALUE", (long)raw, SymbolTable::ST_REPLACE);
		o->properties.add("Position", (long)config->last_sent, SymbolTable::ST_REPLACE);
		double v = config->speeds.average(config->speeds.length());
		if (fabs(v)<1.0) v = 0.0;
		o->properties.add("Velocity", (long)v, SymbolTable::ST_REPLACE);
	}
#endif
	return config->last_sent;
}

void AnalogueInput::update() {
    config->property_changed = false;
}

class CounterInternals {
public:
	CircularBuffer *positions;
	const long *tolerance;		// some filters use a tolerance settable by the user in the "tolerance" property
	const long *filter_len;		// the user can adjust the filter length of some filters via a "filter_len" property
	const long *position_history; // the amount of position history to use in determining movement
	const long *speed_tolerance; // the tolerance used in determining movement
	const long *input_scale; // input readings are divided by this amount
	int32_t last_sent; // this is the value to send unless the read value moves away from the mean
    int32_t prev_sent; // this is the value to send unless the read value moves away from the mean
	uint64_t last_time; // the last time we calculated speed;_
	static long default_tolerance;
	static long default_filter_len;
	static long default_position_history;
	static long default_speed_tolerance;
	static long default_input_scale;
	long speed;
	uint16_t buffer_len;
	FloatBuffer speeds;
	int rate_len;

	CounterInternals() : positions(0), 
	tolerance(&default_tolerance), filter_len(&default_filter_len), 
	position_history(&default_position_history), 
	speed_tolerance(&default_speed_tolerance),
	input_scale(&default_input_scale),
	last_sent(0), 
	prev_sent(0), last_time(0), speed(0), buffer_len(200),speeds(4), rate_len(4) {
		positions = createBuffer(buffer_len);
	}

void update(uint64_t read_time) {
	if (prev_sent == 0) prev_sent = last_sent;
	if (last_time == 0) {
		last_time = read_time;
		prev_sent = last_sent;
		speed = 0.0;
		speeds.append(speed);
	}
	else if (read_time - last_time >= 10000) {
/*
		double dt = (double)(read_time - last_time);
		double dv = (double)(last_sent - prev_sent);
		speed = (long)( dv / dt * 1000000.0) ;
*/
		rate_len = findMovement(positions, 20, *position_history);
		if (rate_len < *position_history) {
			speed = 1000000.0 * rate(positions, (rate_len<4)? 4 : rate_len);
		}
		else speed = 0.0;
		speeds.append(speed);
		last_time = read_time;
		prev_sent = last_sent;
	}
}
	
double filter() {
	if ((unsigned int)length(positions) < 9) return getBufferValue(positions,0);
	double c[] = {0.081,0.215,0.541,0.865,1,0.865,0.541,0.215,0.081};
	double res = 0;
	for (unsigned int i=0; i<9; ++i) {
		double f = (double)getBufferValue(positions, i);
		//printf(" %.3f,%.3f ",f, f*c[i]); 
		res += f * c[i];
	}
	//printf(" %.3f\n",res); 

	return res;
}
};

long CounterInternals::default_tolerance = 1;
long CounterInternals::default_filter_len = 8;
long CounterInternals::default_position_history = 20;
long CounterInternals::default_speed_tolerance = 10;
long CounterInternals::default_input_scale = 1;

Counter::Counter(IOAddress addr) : IOComponent(addr),internals(0) { 
	internals = new CounterInternals;
	regular_polls.insert(this);
}

void Counter::setupProperties(MachineInstance *m) {
	const Value &v = m->getValue("tolerance");
	if (v.kind == Value::t_integer) {
		internals->tolerance = &v.iValue;
		printf("Counter tolerance: %ld\n", *internals->tolerance);
	}
	const Value &v3 = m->getValue("filter_len");
	if (v3.kind == Value::t_integer) {
		internals->filter_len = &v3.iValue;
	}
	const Value &v4 = m->getValue("speed_tolerance");
	if (v4.kind == Value::t_integer) {
		internals->speed_tolerance= &v4.iValue;
		printf("Counter speed tolerance: %ld\n", *internals->speed_tolerance);
	}
	const Value &v5 = m->getValue("position_history");
	if (v5.kind == Value::t_integer) {
		internals->position_history = &v5.iValue;
		printf("Counter position history: %ld\n", *internals->position_history);
	}
	const Value &v6 = m->getValue("input_scale");
	if (v6.kind == Value::t_integer) {
		internals->input_scale = &v6.iValue;
		printf("Counter input scale: %ld\n", *internals->input_scale);
	}
}


int32_t Counter::filter(int32_t val) {
	double scaled_val = (double)val / (double)*internals->input_scale;
	addSample(internals->positions, (long)read_time, scaled_val);

#if 0
	if (internals->filter_type && *internals->filter_type == 0) {
		internals->last_sent = val;
	}
	else if (internals->filter_type && *internals->filter_type == 1) {
		int32_t mean = (internals->positions.average(internals->buffer_len) + 0.5f);
		if (internals->tolerance) internals->noise_tolerance = *internals->tolerance;
		if ( (uint32_t)abs(mean - internals->last_sent) >= internals->noise_tolerance) {
			internals->last_sent = mean;
		}
	}
	else if (internals->filter_type && *internals->filter_type == 2) {
		long res = (long)internals->filter();
		internals->last_sent = (int32_t)(res / internals->filter_len *2);
	}
#endif
	if (*internals->tolerance>1) {
		int32_t mean = (bufferAverage(internals->positions, *internals->filter_len) + 0.5f);
		long delta = (uint32_t)abs(mean - internals->last_sent);
		if ( delta >= *internals->tolerance) {
			internals->last_sent = mean;
		}
	}
	else
		internals->last_sent = (*internals->input_scale == 1) ? val : (uint32_t)( scaled_val + 0.5 );
	internals->update(read_time);

#if 1
	/* most machines reading sensor values will be prompted when teh
		sensor value changes, depending on whether this filter yields a
		changed value. Some systems such as plugins that operate on 
		their own clock may wish to ignore the filtered value and 
		access the raw io value and read time but note that these 
		values do not cause notifications when they change
	*/

	

	std::list<MachineInstance*>::iterator owners_iter = owners.begin();
	while (owners_iter != owners.end()) {
		MachineInstance *o = *owners_iter++;
		o->properties.add("IOTIME", (long)read_time, SymbolTable::ST_REPLACE);
		o->properties.add("DurationTolerance", internals->rate_len, SymbolTable::ST_REPLACE);
		o->properties.add("VALUE", (long)scaled_val, SymbolTable::ST_REPLACE);
		o->properties.add("Position", (long)internals->last_sent, SymbolTable::ST_REPLACE);
		o->properties.add("Velocity", (long)internals->speeds.average(internals->speeds.length()), SymbolTable::ST_REPLACE);
	}
#endif
	return internals->last_sent;
}

CounterRate::CounterRate(IOAddress addr) : IOComponent(addr), times(16), positions(0) {
    struct timeval now;
    gettimeofday(&now, 0);
    start_t = now.tv_sec * 1000000 + now.tv_usec;
}

int32_t CounterRate::filter(int32_t val) {
#if 0
	/* as for the AnalogueInput, note that these 'IO' properties do not 
		cause value change notifications throughout clockwork 
	*/
	std::list<MachineInstance*>::iterator owners_iter = owners.begin();
	while (owners_iter != owners.end()) {
		MachineInstance *o = *owners_iter++;
		o->properties.add("IOTIME", (long)read_time, SymbolTable::ST_REPLACE);
		o->properties.add("IOVALUE", (long)val, SymbolTable::ST_REPLACE);
	}
#endif
	return IOComponent::filter(val);
}

/* The Speed Controller class */

/*
    the user provides settings that the controller uses in its
    calculations. The main input from the user is a speed set point and
    this is set via the setValue() method used by other IOComponents.
    Clockwork has a spefic test for this class and after updating properties
    on this class an update() is called.
 */

class PID_Settings {
public:
    bool property_changed;
    uint32_t max_forward;
    uint32_t max_reverse;
    float Kp;
    uint32_t set_point;
    
	float estimated_speed;
	float Pe; 		// used for process error: SetPoint - EstimatedSpeed;
	float current; 	// current power level this is our PV (control variable)
	float last_pos;
	uint64_t position;
	uint64_t measure_time;
	uint64_t last_time;
	float last_power;
    uint32_t slow_speed;
    uint32_t full_speed;
    uint32_t stopping_time;
    uint32_t stopping_distance;
    uint32_t tolerance;
    uint32_t min_update_time;
    uint32_t deceleration_allowance;
    float deceleration_rate;

    PID_Settings() : property_changed(true),
        max_forward(16383),
        max_reverse(-16383),
        Kp(20.0),
        set_point(0),
        estimated_speed(0.0f),
        Pe(0.0f),
        current(0),
        last_pos(0.0f),
        position(0),
        measure_time(0),
        last_time(0),
        last_power(0),
        slow_speed(200),
        full_speed(1000),
        stopping_time(300),
        stopping_distance(2000),
        tolerance(10),
        min_update_time(20),
        deceleration_allowance(20),
        deceleration_rate(0.8f)
    { }
};

int32_t PIDController::filter(int32_t raw) {
    return raw;
}

PIDController::PIDController(IOAddress addr) : Output(addr), config(0) {
    config = new PID_Settings();
}

PIDController::~PIDController() {
    delete config;
}

void PIDController::update() {
    config->property_changed = true;
}

void PIDController::handleChange(std::list<Package*>&work_queue) {
    //calculate..
    
    if (config->property_changed) {
        config->property_changed = false;
    }
    
    if (last_event == e_change) {
        //config->
    }
    
    IOComponent::handleChange(work_queue);
}

/* ---------- */


const char *IOComponent::getStateString() {
	if (last_event == e_change) return "unstable";
	else if (address.bitlen > 1) return "stable";
	else if (last_event == e_on) return "turning_on";
	else if (last_event == e_off) return "turning_off";
	else if (address.value == 0) return "off"; else return "on";
}

std::vector< std::list<IOComponent*> *>io_map;

int IOComponent::getMinIOOffset() {
	return min_offset;
}

int IOComponent::getMaxIOOffset() {
	return max_offset;
}

int IOComponent::notifyComponentsAt(unsigned int offset) {
	assert(offset <= max_offset && offset >= min_offset);
	int count = 0;
	std::list<IOComponent *> *cl = io_map[offset];
	if (cl) {
		//std::cout << "component list at offset " << offset << " size: " << cl->size() << "\n";
		std::list<IOComponent*>::iterator items = cl->begin();
		//std::cout << "items: \n";
		//while (items != cl->end()) {
		//	IOComponent *c = *items++;
			//std::cout << c->io_name << "\n";
		//}
		//items = cl->begin();
		while (items != cl->end()) {
			IOComponent *c = *items++;
			if (c) {
			//std::cout << "notifying " << c->io_name << "\n";
			boost::recursive_mutex::scoped_lock lock(processing_queue_mutex);
			updatedComponentsIn.insert(c);
			++count;
			}
			//else { std::cout << "Warning: null item detected in io list at offset " << offset << "\n"; }
		}
	}
	return count;
}

bool IOComponent::hasUpdates() {
	boost::recursive_mutex::scoped_lock lock(processing_queue_mutex);
	return !updatedComponentsOut.empty();
}

uint8_t *generateProcessMask(uint8_t *res, size_t len) {
	unsigned int max = IOComponent::getMaxIOOffset();
	// process data size
	if (res && len != max+1) { delete[] res; res = 0; }
	if (!res) res = new uint8_t[max+1];
	memset(res, 0, max+1);

	IOComponent::Iterator iter = IOComponent::begin();
	while (iter != IOComponent::end()) {
		IOComponent *ioc = *iter++;
		unsigned int offset = ioc->address.io_offset;
		unsigned int bitpos = ioc->address.io_bitpos;
		offset += bitpos/8;
		bitpos = bitpos % 8;
		uint8_t mask = 0x01 << bitpos;
		// set  a bit in the mask for each bit of this value
		for (unsigned int i=0; i<ioc->address.bitlen; ++i) {
			res[offset] |= mask;
			mask = mask << 1;
			if (!mask) {mask = 0x01; ++offset; }
		}
	}
#if VERBOSE_DEBUG
	std::cout << " Process Mask: "; display(res, len); std::cout << "\n";
#endif
	return res;
}

// copy the provied data to the default data block
void IOComponent::setDefaultData(uint8_t *data){
#if VERBOSE_DEBUG
	std::cout << "Setting default data to : \n";
	display(data, process_data_size);
	std::cout << "\n";
#endif
	if (!default_data) 	
		default_data = new uint8_t[process_data_size];
	memcpy(default_data, data, process_data_size);
}

// copy the provided mask to the default data mask
void IOComponent::setDefaultMask(uint8_t *mask){
#if VERBOSE_DEBUG
	std::cout << "Setting default mask to : \n";
	display(mask, process_data_size);
	std::cout << "\n";
#endif
	if (!default_mask) 
		default_mask = new uint8_t[process_data_size];
	memcpy(default_mask, mask, process_data_size);
}

uint8_t *IOComponent::generateMask(std::list<MachineInstance*> &outputs) {
	unsigned int max = IOComponent::getMaxIOOffset();
	// process data size
	uint8_t *res = new uint8_t[max+1];
	memset(res, 0, max+1);

	std::list<MachineInstance*>::iterator iter = outputs.begin();
	while (iter != outputs.end()) {
		MachineInstance *m = *iter++;
		IOComponent *ioc = m->io_interface;
		if (ioc) {
			unsigned int offset = ioc->address.io_offset;
			unsigned int bitpos = ioc->address.io_bitpos;
			offset += bitpos/8;
			bitpos = bitpos % 8;
			uint8_t mask = 0x01 << bitpos;
			// set  a bit in the mask for each bit of this value
			for (unsigned int i=0; i<ioc->address.bitlen; ++i) {
				res[offset] |= mask;
				mask = mask << 1;
				if (!mask) {mask = 0x01; ++offset; }
			}
		}
	}
	return res;
}

bool IOComponent::ownersEnabled() const {
	if (owners.empty()) return true;
	else {
		std::list<MachineInstance*>::const_iterator owners_iter = owners.begin();
		while (owners_iter != owners.end()) {
			MachineInstance *o = *owners_iter++;
			if (o->enabled()) return true;
		}
	}
	return false;
}

static uint8_t *generateUpdateMask() {
	//  returns null if there are no updates, otherwise returns
	// a mask for the update data

	//std::cout << "generating mask\n";
	if (updatedComponentsOut.empty()) return 0;

	unsigned int min = IOComponent::getMinIOOffset();
	unsigned int max = IOComponent::getMaxIOOffset();
	uint8_t *res = new uint8_t[max+1];
	memset(res, 0, max+1);
	//std::cout << "mask is " << (max+1) << " bytes\n";

	std::set<IOComponent*>::iterator iter = updatedComponentsOut.begin();
	while (iter != updatedComponentsOut.end()) {
		IOComponent *ioc = *iter; //TBD this can be null
		if (ioc->ownersEnabled()) iter++; else iter = updatedComponentsOut.erase(iter);

		if (ioc->direction() != IOComponent::DirOutput && ioc->direction() != IOComponent::DirBidirectional) 
			continue;
		unsigned int offset = ioc->address.io_offset;
		unsigned int bitpos = ioc->address.io_bitpos;
		offset += bitpos/8;
		bitpos = bitpos % 8;
		uint8_t mask = 0x01 << bitpos;
		// set  a bit in the mask for each bit of this value
		for (unsigned int i=0; i<ioc->address.bitlen; ++i) {
			res[offset] |= mask;
			mask = mask << 1;
			if (!mask) {mask = 0x01; ++offset; }
		}
	}
#if VERBOSE_DEBUG
	std::cout << "generated mask: "; display(res, max-min+1); std::cout << "\n";
#endif
	return res;
}

IOUpdate *IOComponent::getUpdates() {
	//outputs_waiting = 0; // reset work indicator flag
	uint8_t *mask = ::generateUpdateMask();
	if (!mask) {
		return 0;
	}
	//MEMCHECK();
	IOUpdate *res = new IOUpdate;
	//MEMCHECK();
	res->setSize(max_offset - min_offset + 1);
	res->setData(getUpdateData());
	//MEMCHECK();
	res->setMask(mask);
	//MEMCHECK();
#if VERBOSE_DEBUG
	std::cout << std::flush 
		<< "IOComponent::getUpdates preparing to send " << res->size() << " d:"; 
	display(res->data(), process_data_size); 
	std::cout << " m:"; 
	display(res->mask(), process_data_size);
	std::cout << "\n" << std::flush;
#endif
	//MEMCHECK();
	return res;
}

IOUpdate *IOComponent::getDefaults() {
	if (min_offset > max_offset)
		return 0;
	IOUpdate *res = new IOUpdate;
	res->setSize(max_offset - min_offset + 1);
	assert(io_process_data);
	assert(process_data_size);
	assert(default_data);
	assert(default_mask);
	copyMaskedBits(io_process_data, default_data, default_mask, process_data_size);
	res->setData(getProcessData());
	res->setMask(default_mask);

#if VERBOSE_DEBUG
	std::cout << "preparing to send defaults " << res->size() << " bytes\n"; 
	display(res->data(), res->size()); std::cout << "\n"; 
	display(res->mask(), res->size()); std::cout << "\n";
#endif
	return res;
}


void IOComponent::setupIOMap() {
	boost::recursive_mutex::scoped_lock lock(processing_queue_mutex);
	max_offset = 0;
	min_offset = 1000000L;
	io_map.clear();

	if (indexed_components) delete indexed_components;
	std::cout << "\n\n setupIOMap\n";
	// find the highest and lowest offset location within the process data
	std::list<IOComponent *>::iterator iter = processing_queue.begin();
	while (iter != processing_queue.end()) {
		IOComponent *ioc = *iter++;
		std::cout << ioc->getName() << " " << ioc->address << "\n";
		unsigned int offset = ioc->address.io_offset;
		unsigned int bitpos = ioc->address.io_bitpos;
		offset += bitpos/8;
		//bitpos = bitpos % 8;
		if (ioc->address.bitlen>=8) offset += ioc->address.bitlen/8 - 1;
		if (offset > max_offset) max_offset = offset;
		assert(max_offset < 10000);
		if (offset < min_offset) min_offset = offset;
	}
	if (min_offset > max_offset) min_offset = max_offset;
	std::cout << "min io offset: " << min_offset << "\n";
	std::cout << "max io offset: " << max_offset << "\n";
	std::cout << ( (max_offset+1)*sizeof(IOComponent*)) << " bytes reserved for index io\n";

	indexed_components = new std::vector<IOComponent*>( (max_offset+1) *8);

	if (io_process_data) delete[] io_process_data;
	process_data_size = max_offset+1;
	io_process_data = new uint8_t[process_data_size];
	memset(io_process_data, 0, process_data_size);

	io_map.resize(max_offset+1);
	for (unsigned int i=0; i<=max_offset; ++i) io_map[i] = 0;
	iter = processing_queue.begin();
	while (iter != processing_queue.end()) {
		IOComponent *ioc = *iter++;
		if (ioc->io_name == "") continue;
		unsigned int offset = ioc->address.io_offset;
		unsigned int bitpos = ioc->address.io_bitpos;
		offset += bitpos/8;
		int bytes = 1;
		for (unsigned int i=0; i<ioc->address.bitlen; ++i) 
			(*indexed_components)[offset*8 + bitpos + i] = ioc;
		for (int i=0; i<bytes; ++i) {
			std::list<IOComponent *> *cl = io_map[offset+i];
			if (!cl) cl = new std::list<IOComponent *>();
			cl->push_back(ioc);
			io_map[offset+i] = cl;
			std::cout << "offset: " << (offset+i) << " io name: " << ioc->io_name << "\n";
		}
	}
	std::cout << "\n\n\n";
	io_process_mask = generateProcessMask(io_process_mask, process_data_size);
}

void IOComponent::markChange() {
	assert(io_process_data);
	if (!update_data) getUpdateData();
	uint8_t *offset = update_data + address.io_offset;
	int bitpos = address.io_bitpos;
	offset += (bitpos / 8);
	bitpos = bitpos % 8;

	if (address.bitlen == 1) {
		int32_t value = (*offset & (1<< bitpos)) ? 1 : 0;

		// only outputs will have an e_on or e_off event queued, 
		// if they do, set the bit accordingly, ignoring the previous value
		if (!value && last_event == e_on) {
/*
			std::cout << "IOComponent::markChange setting bit " 
				<< (offset - update_data) << ":" << bitpos 
				<< " for " << io_name << "\n";
*/
			set_bit(offset, bitpos, 1);			
			updatesSent(false);
		}
		else if (value &&last_event == e_off) {
			set_bit(offset, bitpos, 0);
			updatesSent(false);
		}
		last_event = e_none;
	}
	else {
		if (last_event == e_change) {
/*
			std::cerr << " marking change to " << pending_value 
				<< " at offset " << (unsigned long)(offset - update_data)
			    << " for " << io_name << "\n";
*/
			if (address.bitlen == 8) {
				*offset = (uint8_t)pending_value & 0xff;
			}
			else if (address.bitlen == 16) {
				uint16_t x = pending_value % 65536;
				toU16(offset, x);
			}
			else if (address.bitlen == 32) {
				toU32(offset, pending_value); 
			}
			last_event = e_none;
#if VERBOSE_DEBUG
			std::cout << "@";
			display(update_data);
			std::cout << "\n";
#endif
			updatesSent(false);
			//address.value = pending_value;
		}
	}
}


void IOComponent::handleChange(std::list<Package*> &work_queue) {
	assert(io_process_data);
//	std::cout << io_name << "::handleChange() " << last_event << "\n";
	uint8_t *offset = io_process_data + address.io_offset;
	int bitpos = address.io_bitpos;
	offset += (bitpos / 8);
	bitpos = bitpos % 8;

	if (address.bitlen == 1) {
		int32_t value = (*offset & (1<< bitpos)) ? 1 : 0;

			const char *evt;
			if (address.value != value)  // TBD is this test necessary?
			{
				std::list<MachineInstance*>::iterator iter;
#ifndef DISABLE_LEAVE_FUNCTIONS
					if (address.value) evt ="on_leave";
					else evt = "off_leave";
					std::list<MachineInstance*>::iterator owner_iter = owners.begin();
					while (owner_iter != owners.end()) {
						ProcessingThread::activate(*owner_iter);
						work_queue.push_back( new Package(this, *owner_iter++, new Message(evt, Message::LEAVEMSG)) );
					}
#endif
					if (value) evt = "on_enter";
					else evt = "off_enter";
					owner_iter = owners.begin();
					while (owner_iter != owners.end()) {
						work_queue.push_back( new Package(this, *owner_iter++, new Message(evt, Message::ENTERMSG)) );
					}
			}
			address.value = value;
	}
	else {
			//std::cout << io_name << " object of size " << address.bitlen << " val: ";
			//display(offset, address.bitlen/8);
			//std::cout << " bit pos: " << bitpos << " ";
			int32_t val = 0;
			if (address.bitlen < 8)  {
				uint8_t bitmask = 0x8 >> bitpos;
				val = 0;
				for (unsigned int count = 0; count < address.bitlen; ++count) {
					val = val + ( *offset & bitmask );
					bitmask = bitmask >> 1;
					// check for objects traversing byte boundaries
					if (count< address.bitlen && !bitmask) { bitmask = 0x80; ++offset; }
				}
				
				while (bitmask) { val = val >> 1; bitmask = bitmask >> 1; }
				//std::cout << " value: " << val << "\n";
			}
			else if (address.bitlen == 8) 
				val = *(int8_t*)(offset);
			else if (address.bitlen == 16) {
				val = fromU16(offset);
				//std::cout << " 16bit value: " << val << " " << std::hex << val << std::dec << "\n";
			}
			else if (address.bitlen == 32) 
				val = fromU32(offset);
			else {
				val = 0;
			}
			if ( regular_polls.count(this) ) {
				// this device is polled on a regular clock, do not process here
				raw_value = val;
				address.value = val;
			}
			else if (hardware_state == s_hardware_init 
				|| (hardware_state == s_operational &&  raw_value != (uint32_t)val ) ) {
				//std::cerr << "raw io value changed from " << raw_value << " to " << val << "\n";
				raw_value = val;
				int32_t new_val = filter(val);
				if (hardware_state == s_operational ) { //&& address.value != new_val) {
					address.value = new_val;
				}
			}
	}
}

void IOComponent::turnOn() { 
}
void IOComponent::turnOff() { 
}

void Output::turnOn() { 
	gettimeofday(&last, 0);
	last_event = e_on;
	updatedComponentsOut.insert(this);
	updatesSent(false);
	++outputs_waiting;
	markChange();
}

void Output::turnOff() { 
	gettimeofday(&last, 0);
	last_event = e_off;
	updatedComponentsOut.insert(this);
	updatesSent(false);
	++outputs_waiting;
	markChange();
}

void IOComponent::setValue(uint32_t new_value) {
	assert(!address.is_signed);
	pending_value = new_value;
	gettimeofday(&last, 0);
	last_event = e_change;
	updatesSent(false);
	updatedComponentsOut.insert(this);
	++outputs_waiting;
	markChange();
}

void IOComponent::setValue(int32_t new_value) {
	assert(address.is_signed);
	pending_value = new_value;
	gettimeofday(&last, 0);
	last_event = e_change;
	updatesSent(false);
	updatedComponentsOut.insert(this);
	++outputs_waiting;
	markChange();
}

bool IOComponent::isOn() {
	return last_event == e_none && address.value != 0;
}

bool IOComponent::isOff() {
	return last_event == e_none && address.value == 0;
}
