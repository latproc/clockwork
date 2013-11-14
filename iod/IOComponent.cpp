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

void IOComponent::processAll() {
	std::list<IOComponent *>::iterator iter = processing_queue.begin();
	while (iter != processing_queue.end()) {
		IOComponent *ioc = *iter++;
		ioc->idle();
	}
}

void IOComponent::add_io_entry(const char *name, unsigned int io_offset, unsigned int bit_offset, unsigned int bit_len){
	IOAddress addr(io_offset, bit_offset, bit_len);
	addr.io_offset = io_offset;
	addr.io_bitpos = bit_offset;
	io_names[name] = addr;
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
	out << '['<< address.io_offset << ':' << address.io_bitpos << "]=" << address.value;
	return out;
}

std::ostream &operator<<(std::ostream&out, const IOComponent &ioc) {
	return ioc.operator<<(out);
}

#ifdef EC_SIMULATOR
unsigned char mem[1000];
#define EC_READ_BIT(offset, bitpos) ( (*offset) & (1 << (bitpos)) )
#define EC_WRITE_BIT(offset, bitpos, val) *(offset) |=  ( 1<< (bitpos))
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
	while (bitpos>=8) { 
		++offset;
		bitpos-=8;
	}
	if (address.bitlen == 1) {
		int value = EC_READ_BIT(offset, bitpos);
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
				/* this no longer needs to be published here as the responsible state machine
					publishes it
		        MessagingInterface *mif = MessagingInterface::getCurrent();
		        std::stringstream ss; 
		        ss << io_name << " STATE " << ((value) ? "on" : "off") << std::flush;
		        mif->send(ss.str().c_str());
				 */

				if (value) evt = "on_enter";
				else evt = "off_enter";
	            std::list<MachineInstance*>::iterator iter = depends.begin();
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
			uint32_t val = 0;
			if (address.bitlen == 8) 
				val = EC_READ_U8(offset);
			else if (address.bitlen == 16) 
				val = EC_READ_U16(offset);
			else if (address.bitlen == 32) 
				val = EC_READ_U32(offset);
            else {
                val = 0;
            }
			//if (val) {for (int xx = 0; xx<4; ++xx) { std::cout << std::setw(2) << std::setfill('0') 
			//	<< std::hex << (int)*((uint8_t*)(offset+xx)); }
			//  << ":" << std::dec << val <<" "; }
			if (address.value != val) {
                //address.value = get_bits(offset, bitpos, address.bitlen);
			    address.value = val;
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
