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
#include <set>
#include <map>
#include <string>
#include <ostream>
#include "State.h"
//#include "ECInterface.h"
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
	bool is_signed;
	IOAddress(unsigned int module_pos, unsigned int offs, int bitp, unsigned int entry_pos, unsigned int len=1, bool signed_value = false)
		: module_position(module_pos), io_offset(offs), io_bitpos(bitp), value(0),
			bitlen(len),entry_position(entry_pos), is_signed(signed_value) { }
	IOAddress(const IOAddress &other) 
		: module_position(other.module_position), io_offset(other.io_offset), io_bitpos(other.io_bitpos), value(other.value), 
		bitlen(other.bitlen), entry_position(other.entry_position),
 		is_signed(other.is_signed), description(other.description) { }
	IOAddress() : module_position(0), io_offset(0), io_bitpos(0), value(0), bitlen(1), entry_position(0), is_signed(false) {}
	std::string description;
};

struct MQTTTopic {
    std::string topic;
    std::string message;
    bool publisher;
    MQTTTopic(const char *t, const char *m) : topic(t), message(m) { }
    MQTTTopic(const std::string&t, const std::string&m) : topic(t), message(m) { }
};

class IOUpdate {
public:
	IOUpdate():size(0), data(0), mask(0) { }
	~IOUpdate() { delete mask; }
	uint32_t size;
	uint8_t *data; // shared pointer to process data
	uint8_t *mask; // allocated pointer to current mask
};

class MachineInstance;
class IOComponent : public Transmitter {
public:
	enum Direction { DirInput, DirOutput, DirBidirectional };
	typedef std::list<IOComponent *>::iterator Iterator;
	static Iterator begin() { return processing_queue.begin(); }
	static Iterator end() { return processing_queue.end(); }
	static IOAddress add_io_entry(const char *name, unsigned int module_pos, 
		unsigned int io_offset, unsigned int bit_offset, unsigned int entry_offs, unsigned int bit_len = 1, bool is_signed = false);
    static void add_publisher(const char *name, const char *topic, const char *message);
    static void add_subscriber(const char *name, const char *topic);
	static void processAll(uint64_t clock, size_t data_size, uint8_t *mask, uint8_t *data, 
			std::set<IOComponent *> &updatedMachines);
	static void setupIOMap();
	static int getMinIOOffset();
	static int getMaxIOOffset();
	static uint8_t *getProcessData() { return io_process_data; }
	static uint8_t *getProcessMask() { return io_process_mask; }
	static uint8_t *getUpdateData();
	static uint8_t *getDefaultData() { return default_data; }
	static uint8_t *getDefaultMask() { return default_mask; }
	static void setDefaultData(uint8_t *);
	static void setDefaultMask(uint8_t *);
	static int notifyComponentsAt(unsigned int offset);
	static bool hasUpdates();
	static IOUpdate *getUpdates();
	static IOUpdate *getDefaults();
	static uint8_t *generateMask(std::list<MachineInstance*> &outputs);
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
	virtual void markChange();
	virtual void handleChange(std::list<Package*>&work_queue);
	virtual void turnOn();
	virtual void turnOff();
	bool isOn();
	bool isOff();
	int32_t value() { if (address.bitlen == 1) { if (isOn()) return 1; else return 0; } else return address.value; }
	void setValue(uint32_t new_value);
	void setValue(int32_t new_value);
	virtual const char *type() { return "IOComponent"; }
	std::ostream &operator<<(std::ostream &out) const;
	IOAddress address;
	uint32_t pending_value;
    uint64_t read_time; // the last read time
	static uint64_t ioClock() { return io_clock; }
	
	void addDependent(MachineInstance *m) {
		depends.push_back(m);
	}
	std::list<MachineInstance*> depends;

	void addOwner(MachineInstance *m) { owners.push_back(m); }
	bool ownersEnabled()const;
	
	void setupProperties(MachineInstance *m); // link properties in the component to the MachineInstance properties
    
    virtual int32_t filter(int32_t);


	void setName(std::string new_name) { io_name = new_name; }
	std::string io_name;

	typedef std::map<std::string, IOComponent*> DeviceList;
	static IOComponent::DeviceList devices;
	static IOComponent* lookup_device(const std::string name);

	int index() { return io_index; } // this component's index into the io array
	void setIndex(int idx) { io_index=idx; }

	static int updatesWaiting() { return outputs_waiting; }
	Direction direction() { return direction_; }

	enum HardwareState { s_hardware_preinit, s_hardware_init, s_operational };
	static HardwareState getHardwareState();
	static void setHardwareState(HardwareState state);
  static void updatesSent() { updates_sent = true; }

protected:
	static uint64_t io_clock;
	int getStatus(); 
	int io_index; // the index of the first bit in this component's address space
	uint32_t raw_value;
	static int outputs_waiting; // this many outputs are waiting to change
	static size_t process_data_size;
	static uint8_t *io_process_data;
	static uint8_t *io_process_mask;
	static uint8_t *update_data;
	static unsigned int max_offset;
	static unsigned int min_offset;
	Direction direction_;
	static HardwareState hardware_state;
	static uint8_t *default_data;
	static uint8_t *default_mask;
	static bool updates_sent;
	std::list<MachineInstance*>owners;
};
std::ostream &operator<<(std::ostream&out, const IOComponent &);


class Output : public IOComponent {
public:
	Output(IOAddress addr) : IOComponent(addr) { direction_ = DirOutput; }
//	Output(unsigned int offset, int bitpos, unsigned int bitlen = 1) : IOComponent(offset, bitpos, bitlen) { }
	virtual const char *type() { return "Output"; }
	virtual void turnOn();
	virtual void turnOff();
};

class Input : public IOComponent {
public:
	Input(IOAddress addr) : IOComponent(addr) { direction_ = DirInput; }
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
	AnalogueOutput(IOAddress addr) : Output(addr) { direction_ = DirOutput; }
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
	void handleChange(std::list<Package*>&work_queue);
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
