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
#include "MachineInstance.h"
#include "MessagingInterface.h"

std::list<IOComponent *> IOComponent::processing_queue;
std::map<std::string, IOAddress> IOComponent::io_names;
static uint64_t current_time;

void IOComponent::processAll() {
    struct timeval now;
    gettimeofday(&now, NULL);
    current_time = now.tv_sec * 1000000 + now.tv_usec;
    
	std::list<IOComponent *>::iterator iter = processing_queue.begin();
	while (iter != processing_queue.end()) {
		IOComponent *ioc = *iter++;
		ioc->read_time = current_time;
		ioc->idle();
	}
}

IOAddress IOComponent::add_io_entry(const char *name, unsigned int module_pos, 
		unsigned int io_offset, unsigned int bit_offset, 
		unsigned int entry_pos, unsigned int bit_len){
	IOAddress addr(module_pos, io_offset, bit_offset, entry_pos, bit_len);
	addr.module_position = module_pos;
	addr.io_offset = io_offset;
	addr.io_bitpos = bit_offset;
    addr.entry_position = entry_pos;
	addr.description = name;
	if (io_names.find(std::string(name)) != io_names.end())  {
		std::cerr << "IOComponent::add_io_entry: warning - an IO component named " 
			<< name << " already existed\n";
	}
	io_names[name] = addr;
	return addr;
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


std::ostream &IOComponent::operator<<(std::ostream &out) const{
	out << '[' << address.description<<" "
		<<address.module_position << " "
		<< address.io_offset << ':' 
		<< address.io_bitpos << "." 
		<< address.bitlen << "]=" 
		<< address.value;
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

/*
unsigned int get_bits(uint8_t *offset, unsigned int bitpos, unsigned int bitlen)
{
	uint32_t val=0;
	int n = bitlen;
	if (n>32) n = 32;
	if (bitlen == 1) {
		val = EC_READ_BIT(offset, bitpos);
	}
	else if (bitlen == 8) 
		val = EC_READ_U8(offset);
	else if (bitlen == 16) 
		val = EC_READ_U16(offset);
	else if (bitlen == 32) 
		val = EC_READ_U32(offset);
    else {
		uint8_t mask=1<<(bitpos%8);
		while (n-- > 0) {
			val = val<<1;
			if (*offset & mask) ++val;
			mask = mask >> 1;
			if (mask == 0) {
				mask=1<<7;
				++offset;
			}
		}
	}
	return val;
}
*/
void set_bits(uint8_t *offset, unsigned int bitpos, unsigned int bitlen, uint32_t value)
{
    
}

int32_t IOComponent::filter(int32_t val) {
    return val;
}

class InputFilterSettings {
public:
    bool property_changed;
    uint32_t noise_tolerance; // filter out changes with +/- this range
    LongBuffer positions;
    uint32_t last_sent; // this is the value to send unless the read value moves away from the mean
    uint16_t buffer_len;
    
    InputFilterSettings() :property_changed(true), noise_tolerance(8), positions(4), last_sent(0), buffer_len(4) { }
};

AnalogueInput::AnalogueInput(IOAddress addr) : IOComponent(addr) { 
	config = new InputFilterSettings();
}
int32_t AnalogueInput::filter(int32_t raw) {
    if (config->property_changed) {
        config->property_changed = false;
    }
    config->positions.append(raw);
    uint32_t mean = (config->positions.average(4) + 0.5f);
    if ( abs(mean - config->last_sent) > config->noise_tolerance) {
        config->last_sent = mean;
    }
    return config->last_sent;
}

void AnalogueInput::update() {
    config->property_changed = true;
}

Counter::Counter(IOAddress addr) : IOComponent(addr) { }

CounterRate::CounterRate(IOAddress addr) : IOComponent(addr), times(16), positions(16) {
    struct timeval now;
    gettimeofday(&now, 0);
    start_t = now.tv_sec * 1000000 + now.tv_usec;
}

int32_t CounterRate::filter(int32_t val) {
    return IOComponent::filter(val);
/* disabled since this is now implemented at the MachineInstance level
    position = val;
    struct timeval now;
    gettimeofday(&now, 0);
    uint64_t now_t = now.tv_sec * 1000000 + now.tv_usec;
    uint64_t delta_t = now_t - start_t;
    times.append(delta_t);
    positions.append(val);
    if (positions.length() < 4) return 0;
    //float speed = positions.difference(positions.length()-1, 0) / times.difference(times.length()-1,0) * 1000000;
    float speed = positions.slopeFromLeastSquaresFit(times) * 250000;
    return speed;
 */
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

void PIDController::idle() {
    //calculate..
    
    if (config->property_changed) {
        config->property_changed = false;
    }
    
    if (last_event == e_change) {
        //config->
    }
    
    IOComponent::idle();
}

/* ---------- */


const char *IOComponent::getStateString() {
	if (last_event == e_on) return "turning_on";
	else if (last_event == e_off) return "turning_off";
	else if (last_event == e_change) return "changing";
	else if (address.bitlen > 1) return "stable";
	else if (address.value == 0) return "off"; else return "on";
}

void IOComponent::idle() {
	uint8_t *offset = ECInterface::domain1_pd + address.io_offset;
	int bitpos = address.io_bitpos;
	offset += (bitpos / 8);
	bitpos = bitpos % 8;
/*
	while (bitpos>=8) { 
		++offset;
		bitpos-=8;
	}
*/
	if (address.bitlen == 1) {
		int32_t value = EC_READ_BIT(offset, bitpos);
		if (!value && last_event == e_on) {
			EC_WRITE_BIT(offset, bitpos, 1);			
		}
		else if (value &&last_event == e_off) {
			EC_WRITE_BIT(offset, bitpos, 0);
		}
		else {
			last_event = e_none;
			const char *evt;
			if (address.value != value) 
			{
                std::list<MachineInstance*>::iterator iter;
#ifndef DISABLE_LEAVE_FUNCTIONS
				if (address.value) evt ="on_leave";
				else evt = "off_leave";
                iter = depends.begin();
	            while (iter != depends.end()) {
	                MachineInstance *m = *iter++;
					Message msg(evt);
                    if (m->receives(msg, this)) m->execute(msg, this);
				}
#endif
                
				if (value) evt = "on_enter";
				else evt = "off_enter";
	            iter = depends.begin();
	            while (iter != depends.end()) {
	                MachineInstance *m = *iter++;
					Message msg(evt);
					m->execute(msg, this);
				}
			}
			address.value = value;
		}
	}
    else {
        if (last_event == e_change) {
            //set_bits(offset, bitpos, address.bitlen, address.value);
			if (address.bitlen == 8) 
				EC_WRITE_U8(offset, (uint8_t)(pending_value % 256));
			else if (address.bitlen == 16) {
				uint16_t x = pending_value % 65536;
				EC_WRITE_U16(offset, x);
			}
			else if (address.bitlen == 32) 
				EC_WRITE_U32(offset, pending_value);
			last_event = e_none;
			address.value = pending_value;
        }
        else {
			int32_t val = 0;
			if (address.bitlen == 8) 
				val = EC_READ_S8(offset);
			else if (address.bitlen == 16) 
				val = EC_READ_S16(offset);
			else if (address.bitlen == 32) 
				val = EC_READ_S32(offset);
            else {
                val = 0;
            }

			//if (val) {for (int xx = 0; xx<4; ++xx) { std::cout << std::setw(2) << std::setfill('0') 
			//	<< std::hex << (int)*((uint8_t*)(offset+xx));
			//  << ":" << std::dec << val <<" "; }
			if (address.value != val || strcmp(type(), "CounterRate") == 0) {
                //address.value = get_bits(offset, bitpos, address.bitlen);
						    address.value = filter(val);
                last_event = e_none;
                const char *evt = "property_change";
                std::list<MachineInstance*>::iterator iter = depends.begin();
                while (iter != depends.end()) {
                  MachineInstance *m = *iter++;
                  Message msg(evt);
                  m->execute(msg, this);
                }
		    }
        }
    }
}

void IOComponent::turnOn() { 
//	std::cout << "Turning on " << address.io_offset << ':' << address.io_bitpos << "\n";
	uint8_t *offset = ECInterface::domain1_pd + address.io_offset;
	int bitpos = address.io_bitpos;
	while (bitpos>=8) { 
		++offset;
		bitpos-=8;
	}
	gettimeofday(&last, 0);
	last_event = e_on;
}

void IOComponent::turnOff() { 
//	std::cout << "Turning off " << address.io_offset << ':' << address.io_bitpos << "\n";
	uint8_t *offset = ECInterface::domain1_pd + address.io_offset;
	int bitpos = address.io_bitpos;
	while (bitpos>=8) { 
		++offset;
		bitpos-=8;
	}
	gettimeofday(&last, 0);
	last_event = e_off;
}

void IOComponent::setValue(uint32_t new_value) {
    pending_value = new_value;
	gettimeofday(&last, 0);
    last_event = e_change;
}


/*
int IOComponent::getStatus() {
	uint8_t *offset = ECInterface::domain1_pd + address.io_offset;
	int bitpos = address.io_bitpos;
	while (bitpos>=8) { 
		++offset;
		bitpos-=8;
	}
    if (address.bitlen == 1) {
		int value = EC_READ_BIT(offset, bitpos);
		return value;
    }
    else {
		address.value = get_bits(offset, address.io_bitpos, address.bitlen);
        last_event = e_none;
        const char *evt = "value_changed";
        std::list<MachineInstance*>::iterator iter = depends.begin();
        while (iter != depends.end()) {
            MachineInstance *m = *iter++;
            Message msg(evt);
            m->execute(msg, this);
        }
        return address.value;
    }
}
*/

bool IOComponent::isOn() {
	return last_event == e_none && address.value != 0;
}

bool IOComponent::isOff() {
	return last_event == e_none && address.value == 0;
}
