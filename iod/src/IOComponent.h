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

#include "State.h"
#include <list>
#include <map>
#include <ostream>
#include <set>
#include <string>
#include "MQTTInterface.h"
#include "Message.h"
#include "filtering.h"
#include <boost/thread/mutex.hpp>
#include <vector>

struct IOAddress {
    unsigned int module_position;
    unsigned int io_offset;
    int io_bitpos;
    int64_t value;
    unsigned int bitlen;
    unsigned int entry_position;
    bool is_signed;
    IOAddress(unsigned int module_pos, unsigned int offs, int bitp, unsigned int entry_pos,
              unsigned int len = 1, bool signed_value = false)
        : module_position(module_pos), io_offset(offs), io_bitpos(bitp), value(0), bitlen(len),
          entry_position(entry_pos), is_signed(signed_value) {}
    IOAddress(const IOAddress &other)
        : module_position(other.module_position), io_offset(other.io_offset),
          io_bitpos(other.io_bitpos), value(other.value), bitlen(other.bitlen),
          entry_position(other.entry_position), is_signed(other.is_signed),
          description(other.description) {}
    IOAddress()
        : module_position(0), io_offset(0), io_bitpos(0), value(0), bitlen(1), entry_position(0),
          is_signed(false) {}
    std::string description;
};

std::ostream &operator<<(std::ostream &out, const IOAddress &address);

struct MQTTTopic {
    std::string topic;
    std::string message;
    bool publisher = false;
    MQTTTopic(const char *t, const char *m) : topic(t), message(m) {}
    MQTTTopic(std::string &&t, std::string &&m) : topic(std::move(t)), message(std::move(m)) {}
};

struct Update {
    std::vector<uint8_t> incoming_process_data;
    std::vector<uint8_t> incoming_process_mask;
    uint64_t incoming_data_size;
    uint64_t global_clock = 0;
};

class IOUpdate {
  public:
    IOUpdate() : size_(0), data_(nullptr), mask_(nullptr) {}
    ~IOUpdate();

    uint32_t size() const { return size_; }
    void setSize(uint32_t sz) { size_ = sz; }

    uint8_t *data() const { return data_; }
    void setData(uint8_t *dt) { data_ = dt; }

    uint8_t *mask() const { return mask_; }
    void setMask(uint8_t *ms) { mask_ = ms; }

  private:
    uint32_t size_;
    uint8_t *data_; // shared pointer to process data
    uint8_t *mask_; // allocated pointer to current mask
};

class MachineInstance;
class IOComponent : public Transmitter {
  public:
    enum Direction { DirInput, DirOutput, DirBidirectional };
    typedef std::list<IOComponent *>::iterator Iterator;
    static Iterator begin() { return processing_queue.begin(); }
    static Iterator end() { return processing_queue.end(); }
    static void reset();
    static IOAddress add_io_entry(const char *name, unsigned int module_pos, unsigned int io_offset,
                                  unsigned int bit_offset, unsigned int entry_offs,
                                  unsigned int bit_len = 1, bool is_signed = false);
    static void add_publisher(const char *name, const char *topic, const char *message);
    static void add_subscriber(const char *name, const char *topic);
    static void processAll(const Update & update, std::set<IOComponent *> &updatedMachines);
    static void processAll(uint64_t clock, size_t data_size, const uint8_t *mask, const uint8_t *data,
                           std::set<IOComponent *> &updatedMachines);
    static void setupIOMap();
    static int getMinIOOffset();
    static int getMaxIOOffset();
    static uint8_t *getProcessData();
    static uint8_t *getProcessMask();
    static uint8_t *getUpdateData();
    static uint8_t *getDefaultData();
    static uint8_t *getDefaultMask();
    static void setDefaultData(uint8_t *);
    static void setDefaultMask(uint8_t *);
    static int notifyComponentsAt(unsigned int offset);
    static bool hasUpdates();
    static IOUpdate *getUpdates();
    static IOUpdate *getDefaults();
    static uint8_t *generateMask(std::list<MachineInstance *> &outputs);
    static uint64_t getClock() { return global_clock; }
    static void remove_io_module(int pos);
    static void lock();   // block others from resetting io
    static void unlock(); // allow io resets
  protected:
    static std::map<std::string, IOAddress> io_names;
    static uint64_t global_clock;

  private:
    IOComponent(const IOComponent &);
    static std::list<IOComponent *> processing_queue;

  protected:
    enum event { e_on, e_off, e_change, e_none };
    event last_event;
    uint64_t last;

  public:
    explicit IOComponent(IOAddress addr);
    IOComponent();
    virtual ~IOComponent() { processing_queue.remove(this); }
    virtual void setInitialState();
    const char *getStateString();
    virtual void markChange();
    virtual void handleChange(std::list<Package *> &work_queue);
    virtual void turnOn();
    virtual void turnOff();
    bool isOn();
    bool isOff();
    int64_t value() {
        if (address.bitlen == 1) {
            if (isOn()) {
                return 1;
            }
            else {
                return 0;
            }
        }
        else {
            return address.value;
        }
    }
    void setValue(uint32_t new_value);
    void setValue(int32_t new_value);
    void setValue(uint64_t new_value);
    void setValue(int64_t new_value);
    virtual const char *type() { return "IOComponent"; }
    std::ostream &operator<<(std::ostream &out) const;
    IOAddress address;
    int64_t pending_value;
    uint64_t read_time; // the last read time
    static uint64_t ioClock() { return io_clock; }

    void addDependent(MachineInstance *m) { depends.push_back(m); }
    std::list<MachineInstance *> depends;

    void addOwner(MachineInstance *m) { owners.push_back(m); }
    bool ownersEnabled() const;

    virtual void setupProperties(
        MachineInstance *m); // link properties in the component to the MachineInstance properties

    virtual int64_t filter(int64_t);

    void setName(std::string &&new_name) { io_name = std::move(new_name); }
    std::string io_name;

    typedef std::map<std::string, IOComponent *> DeviceList;
    static IOComponent::DeviceList devices;
    static IOComponent *lookup_device(const std::string &name);

    int index() { return io_index; } // this component's index into the io array
    void setIndex(int idx) { io_index = idx; }

    static int updatesWaiting();
    Direction direction() { return direction_; }

    enum HardwareState { s_hardware_preinit, s_hardware_init, s_operational };
    static HardwareState getHardwareState();
    static void setHardwareState(HardwareState state);
    static void updatesSent(bool which);
    static bool updatesToSend();

  protected:
    static boost::recursive_mutex io_names_mutex;
    static uint64_t io_clock;
    int getStatus();
    int io_index; // the index of the first bit in this component's address space
    int64_t raw_value;
    static unsigned int outputs_waiting; // this many outputs are waiting to change
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
    std::list<MachineInstance *> owners;
};
std::ostream &operator<<(std::ostream &out, const IOComponent &);

class Output : public IOComponent {
  public:
    Output(IOAddress addr) : IOComponent(addr) { direction_ = DirOutput; }
    //  Output(unsigned int offset, int bitpos, unsigned int bitlen = 1) : IOComponent(offset, bitpos, bitlen) { }
    virtual const char *type() { return "Output"; }
    virtual void turnOn();
    virtual void turnOff();
};

class Input : public IOComponent {
  public:
    Input(IOAddress addr) : IOComponent(addr) { direction_ = DirInput; }
    //  Input(unsigned int offset, int bitpos, unsigned int bitlen = 1) : IOComponent(offset, bitpos, bitlen) { }
    virtual const char *type() { return "Input"; }
};

class InputFilterSettings;
class AnalogueInput : public IOComponent {
  public:
    AnalogueInput(IOAddress addr);
    virtual const char *type() { return "AnalogueInput"; }
    void setupProperties(
        MachineInstance *m); // link properties in the component to the MachineInstance properties
    virtual int64_t filter(int64_t raw);
    void update(); // clockwork uses this to notify of updates
    InputFilterSettings *config;
};

class CounterInternals;
class Counter : public IOComponent {
  public:
    Counter(IOAddress addr);
    virtual const char *type() { return "Counter"; }
    void update(); // clockwork uses this to notify of updates
    virtual int64_t filter(int64_t raw);
    virtual void setupProperties(
        MachineInstance *m); // link properties in the component to the MachineInstance properties
  private:
    CounterInternals *internals;
};

class CounterRate : public IOComponent {
  public:
    CounterRate(IOAddress addr);
    virtual const char *type() { return "CounterRate"; }
    virtual int64_t filter(int64_t raw);
    int64_t position;

  private:
    uint64_t start_t;
    LongBuffer times;
    FloatBuffer positions;
};

class AnalogueOutput : public Output {
  public:
    AnalogueOutput(IOAddress addr) : Output(addr) { direction_ = DirOutput; }
    //  AnalogueOutput(unsigned int offset, int bitpos, unsigned int bitlen) : Output(offset, bitpos, bitlen) { }
    virtual const char *type() { return "AnalogueOutput"; }
};

class PID_Settings;
class PIDController : public Output {
  public:
    PIDController(IOAddress addr);
    ~PIDController();
    //  AnalogueOutput(unsigned int offset, int bitpos, unsigned int bitlen) : Output(offset, bitpos, bitlen) { }
    virtual const char *type() { return "SpeedController"; }
    void handleChange(std::list<Package *> &work_queue);
    virtual int64_t filter(int64_t raw);
    void update(); // clockwork uses this to notify of updates
    PID_Settings *config;
};

class MQTTPublisher : public IOComponent {
  public:
    MQTTPublisher(IOAddress addr) : IOComponent(addr) {}
    //MQTTPublisher(unsigned int offset, int bitpos, unsigned int bitlen = 1) : IOComponent(offset, bitpos, bitlen) { }
    virtual const char *type() { return "Output"; }
};
class MQTTSubscriber : public IOComponent {
  public:
    MQTTSubscriber(IOAddress addr) : IOComponent(addr) {}
    //MQTTSubscriber(unsigned int offset, int bitpos, unsigned int bitlen = 1) : IOComponent(offset, bitpos, bitlen) { }
    virtual const char *type() { return "Input"; }
};

#endif
