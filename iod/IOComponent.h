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

#ifndef __IOComponent
#define __IOComponent

#include <list>
#include <map>
#include <string>
#include <ostream>
#include "State.h"
#include "ECInterface.h"
#include "Message.h"
#include "MQTTInterface.h"
#include "filtering.h"

struct IOAddress {
	unsigned int module_position;
    unsigned int io_offset;
    int io_bitpos;
	int32_t value;
	unsigned int bitlen;
	unsigned int entry_position;
	IOAddress(unsigned int module_pos, unsigned int offs, int bitp, unsigned int entry_pos, unsigned int len=1) 
		: module_position(module_pos), io_offset(offs), io_bitpos(bitp), value(0), bitlen(len),entry_position(entry_pos) { }
	IOAddress(const IOAddress &other) 
		: module_position(other.module_position), io_offset(other.io_offset), io_bitpos(other.io_bitpos), value(other.value), 
		bitlen(other.bitlen), entry_position(other.entry_position), description(other.description) { }
	IOAddress() : module_position(0), io_offset(0), io_bitpos(0), value(0), bitlen(1), entry_position(0) {}
	std::string description;
};

struct MQTTTopic {
    std::string topic;
    std::string message;
    bool publisher;
    MQTTTopic(const char *t, const char *m) : topic(t), message(m) { }
    MQTTTopic(const std::string&t, const std::string&m) : topic(t), message(m) { }
};

class MachineInstance;
class IOComponent : public Transmitter {
public:
	typedef std::list<IOComponent *>::iterator Iterator;
	static Iterator begin() { return processing_queue.begin(); }
	static Iterator end() { return processing_queue.end(); }
	static IOAddress add_io_entry(const char *name, unsigned int module_pos, 
		unsigned int io_offset, unsigned int bit_offset, unsigned int entry_offs, unsigned int bit_len = 1);
    static void add_publisher(const char *name, const char *topic, const char *message);
    static void add_subscriber(const char *name, const char *topic);
	static void processAll();
	static void setupIOMap();
#ifndef EC_SIMULATOR
	ECModule *owner() { return ECInterface::findModule(address.module_position); }
#endif
protected:
	static std::map<std::string, IOAddress> io_names;
private:
	IOComponent(const IOComponent &);
	static std::list<IOComponent *>processing_queue;

protected:
	enum event { e_on, e_off, e_change, e_none};
	event last_event;
	struct timeval last;
public:

    IOComponent(IOAddress addr); 
    IOComponent();
	virtual ~IOComponent() { processing_queue.remove(this); }
	virtual void setInitialState();
	const char *getStateString();
	virtual void idle();
	virtual void turnOn();
	virtual void turnOff();
	bool isOn();
	bool isOff();
	int32_t value() { if (address.bitlen == 1) { if (isOn()) return 1; else return 0; } else return address.value; }
	void setValue(uint32_t new_value);
	virtual const char *type() { return "IOComponent"; }
	std::ostream &operator<<(std::ostream &out) const;
	IOAddress address;
	uint32_t pending_value;
    uint64_t read_time; // the last read time

	
	void addDependent(MachineInstance *m) {
		depends.push_back(m);
	}
    
    virtual int32_t filter(int32_t);

	std::list<MachineInstance*>depends;


	void setName(std::string new_name) { io_name = new_name; }
	std::string io_name;

	typedef std::map<std::string, IOComponent*> DeviceList;
	static IOComponent::DeviceList devices;
	static IOComponent* lookup_device(const std::string name);
protected:
	int getStatus(); 
};
std::ostream &operator<<(std::ostream&out, const IOComponent &);


class Output : public IOComponent {
public:
	Output(IOAddress addr) : IOComponent(addr) { }
//	Output(unsigned int offset, int bitpos, unsigned int bitlen = 1) : IOComponent(offset, bitpos, bitlen) { }
	virtual const char *type() { return "Output"; }
	virtual void turnOn();
	virtual void turnOff();
};

class Input : public IOComponent {
public:
	Input(IOAddress addr) : IOComponent(addr) { }
//	Input(unsigned int offset, int bitpos, unsigned int bitlen = 1) : IOComponent(offset, bitpos, bitlen) { }
	virtual const char *type() { return "Input"; }
};

class InputFilterSettings;
class AnalogueInput : public IOComponent {
public:
	AnalogueInput(IOAddress addr);
	virtual const char *type() { return "AnalogueInput"; }
    virtual int32_t filter(int32_t raw);
    void update(); // clockwork uses this to notify of updates
    InputFilterSettings *config;
};

class Counter : public IOComponent {
public:
	Counter(IOAddress addr);
	virtual const char *type() { return "Counter"; }
    void update(); // clockwork uses this to notify of updates
};

class CounterRate : public IOComponent {
public:
	CounterRate(IOAddress addr);
    virtual const char *type() { return "CounterRate"; }
    virtual int32_t filter(int32_t raw);
    int32_t position;
private:
    uint64_t start_t;
    LongBuffer times;
    FloatBuffer positions;
};

class AnalogueOutput : public Output {
public:
	AnalogueOutput(IOAddress addr) : Output(addr) { }
//	AnalogueOutput(unsigned int offset, int bitpos, unsigned int bitlen) : Output(offset, bitpos, bitlen) { }
	virtual const char *type() { return "AnalogueOutput"; }
};

class PID_Settings;
class PIDController : public Output {
public:
	PIDController(IOAddress addr);
    ~PIDController();
    //	AnalogueOutput(unsigned int offset, int bitpos, unsigned int bitlen) : Output(offset, bitpos, bitlen) { }
	virtual const char *type() { return "SpeedController"; }
    void idle();
    virtual int32_t filter(int32_t raw);
    void update(); // clockwork uses this to notify of updates
    PID_Settings *config;
};

class MQTTPublisher : public IOComponent {
public:
	MQTTPublisher(IOAddress addr) : IOComponent(addr) { }
	//MQTTPublisher(unsigned int offset, int bitpos, unsigned int bitlen = 1) : IOComponent(offset, bitpos, bitlen) { }
	virtual const char *type() { return "Output"; }
};
class MQTTSubscriber : public IOComponent {
public:
	MQTTSubscriber(IOAddress addr) : IOComponent(addr) { }
	//MQTTSubscriber(unsigned int offset, int bitpos, unsigned int bitlen = 1) : IOComponent(offset, bitpos, bitlen) { }
	virtual const char *type() { return "Input"; }
};

#endif
