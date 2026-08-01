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
#include "CommandClock.h"
#include "DebugExtra.h"
#include "ECInterface.h"
#include "Logger.h"
#include "MachineInstance.h"
#include "MessagingInterface.h"
#include <algorithm>
#include <boost/thread/mutex.hpp>
#include <iomanip>
#include <iostream>
#include <netinet/in.h>
#include <string.h>
#include <strings.h>
#include <string>
#ifndef EC_SIMULATOR
#include "cw_ethercat_types.h"
#endif
#include "ProcessingThread.h"
#include "buffering.c"

#define VERBOSE_DEBUG 0
//static void MEMCHECK() { char *x = new char[12358]; memset(x,0,12358); delete[] x; }

/* byte swapping macros using either custom code or the network byte order std functions */
#if __BIGENDIAN
#if 0
#define toU16(m, v)                                                                                \
    *((uint16_t *)m) = ((((uint16_t)v) & 0xff00) >> 8) | ((((uint16_t)v) & 0x00ff) << 8)
#define fromU16(m) (((*(uint16_t *)m) & 0xff00) >> 8) | ((*((uint16_t *)m) & 0x00ff) << 8)
#define toU32(m, v)                                                                                \
    *((uint32_t *)m) = ((((uint32_t)v) & 0xff000000) >> 24) |                                      \
                       ((((uint32_t)v) & 0x00ff0000) >> 8) | ((((uint32_t)v) & 0x0000ff00) << 8) | \
                       ((((uint32_t)v) & 0x000000ff) << 24)
#define fromU32(m)                                                                                 \
    (((*(uint32_t *)m) & 0xff000000) >> 24) | ((*((uint32_t *)m) & 0x00ff0000) >> 8) |             \
        (((*(uint32_t *)m) & 0x0000ff00) << 8) | ((*((uint32_t *)m) & 0x000000ff) << 24)

#else
#include <netinet/in.h>
#define toU16(m, v) *(uint16_t *)(m) = htons((v))
#define fromU16(m) ntohs(*(uint16_t *)m)
#define toU32(m, v) *(uint32_t *)(m) = htonl((v))
#define fromU32(m) ntohl(*(uint32_t *)m)
#endif
#else
#define toU16(m, v) EC_WRITE_U16(m, v)
#define fromU16(m) EC_READ_U16(m)
#define toU32(m, v) EC_WRITE_U32(m, v)
#define fromU32(m) EC_READ_U32(m)
#define toU64(m) EC_WRITE_U64(m, v)
#define fromU64(m) EC_READ_U64(m)
#endif

std::list<IOComponent *> IOComponent::processing_queue;
std::map<std::string, IOAddress> IOComponent::io_names;
static uint64_t current_time;
uint64_t IOComponent::io_clock;     // clock value when ProcessAll is called
uint64_t IOComponent::global_clock; // clock value last received
size_t IOComponent::process_data_size = 0;
uint8_t *IOComponent::io_process_data = 0;
uint8_t *IOComponent::io_process_mask = 0;
uint8_t *IOComponent::update_data = 0;
uint8_t *IOComponent::default_data = 0;
uint8_t *IOComponent::default_mask = 0;
static uint8_t *last_process_data = 0;
size_t IOComponent::outputs_waiting = 0;

boost::recursive_mutex processing_queue_mutex;
boost::recursive_mutex IOComponent::io_names_mutex;
boost::unique_lock<boost::recursive_mutex> io_lock(processing_queue_mutex, boost::defer_lock);

void IOComponent::lock() { io_lock.lock(); }

void IOComponent::unlock() { io_lock.unlock(); }

unsigned int IOComponent::max_offset = 0;
unsigned int IOComponent::min_offset = 1000000L;
static std::vector<IOComponent *> *indexed_components = 0;
IOComponent::HardwareState IOComponent::hardware_state = s_hardware_preinit;

/* these items define a process for polling certain IOComponents on a regular basis */

std::set<IOComponent *> regular_polls;

static uint64_t last_sample = 0; // retains a timestamp for the last sample

// as long as there has been a sufficient delay, run the filter for each
// of the nominated components.
// ProcessingThread rate-limits calls to POLLING_DELAY; min_period is a floor.
void handle_io_sampling(uint64_t io_clock) {
    uint64_t now = microsecs();
    if (now - last_sample < 1000) {
        return;
    }
    last_sample = now;
    std::list<Package *> no_machine_events;
    std::set<IOComponent *>::iterator iter = regular_polls.begin();
    while (iter != regular_polls.end()) {
        IOComponent *ioc = *iter++;
        ioc->read_time = io_clock;
        // Read live process image into address.value, then filter/publish.
        // (Previously only re-filtered a stale address.value, so analogs stuck
        // at 0 when processAll never enqueued a bit-edge for that sample.)
        if (IOComponent::getProcessData()) {
            ioc->handleChange(no_machine_events);
        }
        else {
            ioc->filter(ioc->address.value);
        }
    }
    // Periodic control is distinct from ANALOGINPUT/COUNTER change emits.
    // One IOD-monotonic tick dispatches every due COMMANDCLOCK instance.
    MachineInstance::dispatchCommandClocks(now);
}

void IOComponent::publishSampleTime(uint64_t sample_clock, bool publish_raw, int64_t raw) {
    read_time = sample_clock;
    std::list<MachineInstance *>::iterator owners_iter = owners.begin();
    while (owners_iter != owners.end()) {
        MachineInstance *o = *owners_iter++;
        if (!o) {
            continue;
        }
        // Skip SymbolTable churn when the stamp is already current.
        const Value &cur_t = o->properties.lookup("IOTIME");
        const bool time_same =
            cur_t.kind == Value::t_integer &&
            static_cast<uint64_t>(cur_t.iValue) == sample_clock;
        if (!time_same) {
            o->properties.add("IOTIME", static_cast<int64_t>(sample_clock),
                              SymbolTable::ST_REPLACE);
        }
        if (publish_raw) {
            const Value &cur_raw = o->properties.lookup("raw");
            if (cur_raw.kind != Value::t_integer || cur_raw.iValue != raw) {
                o->properties.add("raw", raw, SymbolTable::ST_REPLACE);
            }
        }
    }
}

void IOComponent::stampAllInputSampleTimes(uint64_t sample_clock) {
    // Not used on the hot path: full-queue POINT stamps every poll were a major
    // processing CPU cost. ANALOG/COUNTER use handle_io_sampling; POINT IOTIME
    // is published from handleChange when the bit changes. Kept for debug tools.
    io_clock = sample_clock;
    global_clock = sample_clock;
    boost::recursive_mutex::scoped_lock lock(processing_queue_mutex);
    for (IOComponent *ioc : processing_queue) {
        if (!ioc) {
            continue;
        }
        if (ioc->direction() == DirOutput) {
            continue;
        }
        if (regular_polls.count(ioc)) {
            continue;
        }
        ioc->publishSampleTime(sample_clock);
    }
}

#if VERBOSE_DEBUG
static void display(const uint8_t *p, unsigned int count = 0);
#endif

void set_bit(uint8_t *q, unsigned int bitpos, unsigned int val) {
    uint8_t bitmask = 1 << bitpos;
    if (val) {
        *q |= bitmask;
    }
    else {
        *q &= (uint8_t)(0xff - bitmask);
    }
}

void copyMaskedBits(uint8_t *dest, uint8_t *src, uint8_t *mask, size_t len) {

    uint8_t *result = dest;
#if VERBOSE_DEBUG
    std::cout << "copying masked bits: \n";
    display(dest, len);
    std::cout << "\n";
    display(src, len);
    std::cout << "\n";
    display(mask, len);
    std::cout << "\n";
#endif
    size_t count = len;
    while (count--) {
        uint8_t bitmask = 0x80;
        for (int i = 0; i < 8; ++i) {
            if (*mask & bitmask) {
                if (*src & bitmask) {
                    *dest |= bitmask;
                }
                else {
                    *dest &= (uint8_t)(0xff - bitmask);
                }
            }
            bitmask = bitmask >> 1;
        }
        ++src;
        ++dest;
        ++mask;
    }
#if VERBOSE_DEBUG
    display(result, len);
    std::cout << "\n";
#endif
}

IOUpdate::~IOUpdate() {
    assert(mask_);
    if (owns_mask_) {
        delete[] mask_;
    }
}

uint32_t IOUpdate::size() const { return size_; }
void IOUpdate::setSize(uint32_t sz) { size_ = sz; }

uint8_t *IOUpdate::data() const { return data_; }
void IOUpdate::setData(uint8_t *dt) {
    assert("memory leak setting IO Component data" && !data_);
    data_ = dt;
}

uint8_t *IOUpdate::mask() const { return mask_; }
void IOUpdate::setMask(uint8_t *ms, bool take_ownership) {
    assert("memory leak setting IO Component data" && !mask_);
    mask_ = ms;
    owns_mask_ = take_ownership;
}

// these components need to synchronise with clockwork
std::set<IOComponent *> updatedComponentsIn;
std::set<IOComponent *> updatedComponentsOut;
bool IOComponent::updates_sent = false;

IOComponent::HardwareState IOComponent::getHardwareState() { return hardware_state; }
void IOComponent::setHardwareState(IOComponent::HardwareState state) {
    const char *hw_state_str = "Hardware preinitialisation";
    if (state == s_hardware_init) {
        hw_state_str = "Hardware Initialisation";
    }
    else if (state == s_operational) {
        hw_state_str = "Operational";
    }
    DBG_INITIALISATION << "Hardware state  set to " << hw_state_str << "\n";
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
    std::list<MachineInstance *>::iterator iter = MachineInstance::begin();
    while (iter != MachineInstance::end()) {
        MachineInstance *m = *iter++;
        if (m->io_interface) {
            delete m->io_interface;
            m->io_interface = 0;
        }
    }
    if (io_process_data) {
        delete[] io_process_data;
    }
    io_process_data = 0;
    if (io_process_mask) {
        delete[] io_process_mask;
    }
    io_process_mask = 0;
    if (update_data) {
        delete[] update_data;
    }
    update_data = 0;
    if (default_data) {
        delete[] default_data;
    }
    default_data = 0;
    if (default_mask) {
        delete[] default_mask;
    }
    default_mask = 0;
    if (last_process_data) {
        delete[] last_process_data;
    }
    last_process_data = 0;
}

IOComponent::IOComponent(IOAddress addr)
    : last_event(e_none), address(addr), io_index(-1), raw_value(0) {
    boost::recursive_mutex::scoped_lock lock(processing_queue_mutex);
    processing_queue.push_back(this);
    // the io_index is the bit offset of the first bit in this objects address space
    io_index = addr.io_offset * 8 + addr.io_bitpos;
}

IOComponent::IOComponent()
    : last_event(e_none), io_index(-1), raw_value(0), direction_(DirBidirectional) {
    boost::recursive_mutex::scoped_lock lock(processing_queue_mutex);
    processing_queue.push_back(this);
    // use the same io-updated index as the processing queue position
}

// most io devices set their initial state to reflect the
// current hardware state
// output devices reset to an 'off' state
void IOComponent::setInitialState() {}

size_t IOComponent::updatesWaiting() {
    // Prefer set size; counter can desync if ++ was used on re-inserts.
    return updatedComponentsOut.size();
}

bool IOComponent::domainHasDigitalChange(const uint8_t *curr, const uint8_t *prev,
                                         size_t len) {
    if (!curr || len == 0) {
        return false;
    }
    // No previous sample: treat as digital so the first frame is pushed.
    if (!prev || !indexed_components || indexed_components->empty()) {
        return true;
    }
    const uint8_t *pm = io_process_mask;
    size_t n = len;
    if (process_data_size && n > process_data_size) {
        n = process_data_size;
    }
    for (size_t i = 0; i < n; ++i) {
        uint8_t care = pm ? pm[i] : 0xff;
        uint8_t diff = static_cast<uint8_t>((curr[i] ^ prev[i]) & care);
        if (!diff) {
            continue;
        }
        for (unsigned b = 0; b < 8; ++b) {
            if (!(diff & static_cast<uint8_t>(1u << b))) {
                continue;
            }
            const size_t idx = i * 8 + b;
            if (idx >= indexed_components->size()) {
                continue;
            }
            IOComponent *ioc = (*indexed_components)[idx];
            if (!ioc) {
                continue;
            }
            // ANALOGINPUT / COUNTER on regular_polls: not a digital edge.
            if (regular_polls.count(ioc) && ioc->address.bitlen > 1) {
                continue;
            }
            return true;
        }
    }
    return false;
}

void IOComponent::clearPendingOutputUpdates() {
    boost::recursive_mutex::scoped_lock lock(processing_queue_mutex);
    updatedComponentsOut.clear();
    outputs_waiting = 0;
}

void IOComponent::updatesSent(bool which) {
    //if (which) std::cout << "updates sent\n";
    updates_sent = which;
}

bool IOComponent::updatesToSend() { return !updates_sent; }

IOComponent *IOComponent::lookup_device(const std::string &name) {
    IOComponent::DeviceList::iterator device_iter = IOComponent::devices.find(name);
    if (device_iter != IOComponent::devices.end()) {
        return (*device_iter).second;
    }
    return 0;
}

#if VERBOSE_DEBUG
static void display(const uint8_t *p, unsigned int count) {
    int max = IOComponent::getMaxIOOffset();
    int min = IOComponent::getMinIOOffset();
    if (count == 0)
        for (int i = min; i <= max; ++i) {
            std::cout << std::setw(2) << std::setfill('0') << std::hex << (unsigned int)p[i];
        }
    else
        for (unsigned int i = 0; i < count; ++i) {
            std::cout << std::setw(2) << std::setfill('0') << std::hex << (unsigned int)p[i];
        }
    std::cout << std::dec;
}
#endif

uint8_t *IOComponent::getUpdateData() {
    assert(io_process_data);
    if (!update_data) {
        update_data = new uint8_t[process_data_size];
        memcpy(update_data, io_process_data, process_data_size);
    }
    return update_data;
}

void IOComponent::processAll(const Update &update, std::set<IOComponent *> &updated_machines) {
    processAll(update.global_clock, update.incoming_data_size, &update.incoming_process_mask[0],
               &update.incoming_process_data[0], updated_machines);
}

void IOComponent::processAll(uint64_t clock, uint64_t data_size, const uint8_t *mask,
                             const uint8_t *data, std::set<IOComponent *> &updated_machines) {
    io_clock = clock;
    global_clock = clock; // getIOClock() / plugins — same µs sample clock as IOTIME
    // receive process data updates and mask to yield updated components
    current_time = microsecs();

    assert(data != io_process_data);

#if VERBOSE_DEBUG
    for (size_t ii = 0; ii < data_size; ++ii)
        if (mask[ii]) {
            std::cout << "IOComponent::processAll()\n";
            std::cout << "size: " << data_size << "\n";
            std::cout << "pdta: ";
            display(io_process_data, data_size);
            std::cout << "\n";
            std::cout << "pmsk: ";
            display(io_process_mask, data_size);
            std::cout << "\n";
            std::cout << "data: ";
            display(data, data_size);
            std::cout << "\n";
            std::cout << "mask: ";
            display(mask, data_size);
            std::cout << "\n";
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

    // Care bits: prefer the static process map (all registered IO). The ecat
    // update_mask is often *diff-only* after the first frame; if we only absorb
    // those bits, multi-bit DIGITALVALUE (0x603F/0x6041) never leave 0 when the
    // first coherent push was missed or preinit already matched zeros.
    // Fall back to the frame mask when the process map is not ready.
    const uint8_t *care_base = io_process_mask ? io_process_mask : mask;

    // step through care bits and update process data from the coherent image
    const uint8_t *p = data;
    const uint8_t *m = care_base;
    const uint8_t *frame_m = mask;
    uint8_t *q = io_process_data;
    IOComponent *just_added = 0;
    // ANALOG/COUNTER on regular_polls: refresh address.value for sampleRegularPolls
    // but do NOT enqueue for handleChange/CW. Queuing them every domain frame
    // forced ~1 kHz full processing loops with empty stableQ/exec.
    std::set<IOComponent *> regular_poll_dirty;
    for (unsigned int i = 0; i < process_data_size; ++i) {
        // Union process-map care with this frame's update mask.
        uint8_t care = static_cast<uint8_t>((m ? *m : 0) | (frame_m ? *frame_m : 0));
        if (!last_process_data) {
            if (care) {
                notifyComponentsAt(i);
            }
        }
        if (*p != *q && care) { // copy cared bits if any changed
            uint8_t bitmask = 0x01;
            int j = 0;
            // check each bit against the care mask and if set, check if the bit
            // has changed. If it has, notify components and update the bit.
            while (bitmask) {
                if (care & bitmask) {
                    //std::cout << "looking up " << i << ":" << j << "\n";
                    IOComponent *ioc = (*indexed_components)[i * 8 + j];
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
                        if ((*p & bitmask) != (*q & bitmask)) {
                            // remotely source change on this io
                            if (ioc) {
                                // Only ANALOGINPUT/COUNTER join regular_polls (bitlen>1).
                                // POINT / STATUS_FLAG / DIGITALVALUE / 1-bit Input must
                                // stay on the event path (on_enter/off_enter / setValue).
                                if (regular_polls.count(ioc) && ioc->address.bitlen > 1) {
                                    regular_poll_dirty.insert(ioc);
                                }
                                else {
                                    //std::cout << " adding " << ioc->io_name << " due to bit change\n";
                                    boost::recursive_mutex::scoped_lock lock(processing_queue_mutex);
                                    updatedComponentsIn.insert(ioc);
                                }
                            }

                            if (*p & bitmask) {
                                *q |= bitmask;
                            }
                            else {
                                *q &= (uint8_t)(0xff - bitmask);
                            }
                        }
                        //else {
                        //  std::cout << "no change " << (unsigned int)*p << " vs " <<
                        //      (unsigned int)*q << "\n";}
                    }
                    else {
                        if (!ioc) {
                            std::cout << "IOComponent::processAll(): no io component at " << i
                                      << ":" << j << " but mask bit is set\n";
                        }
                        if ((*p & bitmask) != (*q & bitmask)) {
                            if (*p & bitmask) {
                                *q |= bitmask;
                            }
                            else {
                                *q &= (uint8_t)(0xff - bitmask);
                            }
                        }
                    }
                }
                bitmask = bitmask << 1;
                ++j;
            }
        }
        ++p;
        ++q;
        if (m) {
            ++m;
        }
        if (frame_m) {
            ++frame_m;
        }
    }

    // Mirror wire value into address.value for polled analogs/counters without
    // generating CW work (filter/IOTIME run from sampleRegularPolls).
    if (!regular_poll_dirty.empty()) {
        std::list<Package *> no_machine_events;
        for (IOComponent *ioc : regular_poll_dirty) {
            if (ioc) {
                ioc->handleChange(no_machine_events);
            }
        }
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
        // Input-domain changes: enqueue CW work and drop matching pending outs.
        // Do NOT return early when empty — pending-out cleanup below must still
        // run. Previously an early return left updatesWaiting() true forever
        // whenever the domain was stable (only TX pending), forcing ~200 Hz
        // brk_out full outer loops on the processing thread.
        if (!updatedComponentsIn.empty()) {
            //  std::cout << updatedComponentsIn.size() << " component updates from hardware\n";
            // look at the components that changed and remove them from the outgoing queue as long as the
            // outputs have been sent to the hardware
            std::set<IOComponent *>::iterator iter = updatedComponentsIn.begin();
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
        }
        // for machines with updates to send, if these machines already have the same value
        // as the hardware (and updates have been sent) we also remove them from the
        // outgoing queue — runs even when no inputs changed this frame
        if (updates_sent && !updatedComponentsOut.empty()) {
            std::set<IOComponent *>::iterator iter = updatedComponentsOut.begin();
            while (iter != updatedComponentsOut.end()) {
                IOComponent *ioc = *iter++;
                if (ioc->pending_value == ioc->address.value) {
                    //std::cout << "output request for " << ioc->io_name << " cleared as hardware value matches\n";
                    updatedComponentsOut.erase(ioc);
                }
            }
        }
        outputs_waiting = updatedComponentsOut.size();
    } // scoped lock
}

IOAddress IOComponent::add_io_entry(const char *name, unsigned int module_pos,
                                    unsigned int io_offset, unsigned int bit_offset,
                                    unsigned int entry_pos, unsigned int bit_len,
                                    bool signed_value) {
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
        if (io_names.find(std::string(buf)) != io_names.end()) {
            DBG_MSG << "IOComponent::add_io_entry: warning - an IO component named " << name
                    << " already existed on module " << module_pos << "\n";
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

void IOComponent::setupProperties(MachineInstance *m) {}

std::ostream &operator<<(std::ostream &out, const IOAddress &address) {
    return out << " [" << address.description << ", " << address.module_position << " "
               << address.io_offset << ':' << address.io_bitpos << "." << address.bitlen
               << "]=" << address.value;
}

std::ostream &IOComponent::operator<<(std::ostream &out) const {
    out << "readtime: " << (io_clock - read_time) << address;
    if (address.bitlen == 1 && io_process_data) {
        out << " (" << (bool)(io_process_data[address.io_offset] & (1 << address.io_bitpos)) << ")";
    }
    return out;
}

std::ostream &operator<<(std::ostream &out, const IOComponent &ioc) { return ioc.operator<<(out); }

#ifdef EC_SIMULATOR
unsigned char mem[1000];
/* Simulator uses the same EC_READ/WRITE macros from cw_ethercat_types.h. */
#endif

int64_t IOComponent::filter(int64_t val) { return val; }

namespace {
// Period (µs) from a settings integer in ms. Defaults and clamps as given.
uint64_t periodUsFromMs(const int64_t *ms_prop, int64_t default_ms, int64_t min_ms,
                        int64_t max_ms) {
    int64_t ms = default_ms;
    if (ms_prop && *ms_prop > 0) {
        ms = *ms_prop;
    }
    if (ms < min_ms) {
        ms = min_ms;
    }
    if (ms > max_ms) {
        ms = max_ms;
    }
    return static_cast<uint64_t>(ms) * 1000ULL;
}

// Filter advance tick (throttle; legacy rate alias on settings). Default 100 ms.
uint64_t filterPeriodUs(const int64_t *throttle_ms) {
    return periodUsFromMs(throttle_ms, 100, 1, 5000);
}

// Owner notify_period (ms): max rate for dependant command fan-out. Default 100.
// Actual sends also require a value change (or first startup emit) — see notifyIfDue.
uint64_t ownerNotifyPeriodMs(MachineInstance *o) {
    if (!o) {
        return 100;
    }
    const Value &v = o->getValue("notify_period");
    if (v.kind == Value::t_integer && v.iValue > 0) {
        return static_cast<uint64_t>(v.iValue);
    }
    return 100;
}

const char *ownerCommandName(MachineInstance *o, const char *default_cmd) {
    if (!o) {
        return default_cmd;
    }
    const Value &v = o->getValue("command");
    if (v.kind == Value::t_string && !v.sValue.empty()) {
        return v.sValue.c_str();
    }
    return default_cmd;
}

// Apply plant OPTION signed:1 on owner to IOAddress (ESI may leave is_signed false).
void applyOwnerSignedFlag(MachineInstance *m, IOAddress &addr) {
    if (!m) {
        return;
    }
    const Value &s = m->getValue("signed");
    if (s.kind == Value::t_integer && s.iValue != 0) {
        addr.is_signed = true;
    }
}

// Sign-extend multi-bit wire value when address is marked signed.
int64_t signExtendWireValue(int64_t val, unsigned int bitlen, bool is_signed) {
    if (!is_signed) {
        return val;
    }
    if (bitlen == 8) {
        return static_cast<int64_t>(static_cast<int8_t>(val & 0xff));
    }
    if (bitlen == 16) {
        return static_cast<int64_t>(static_cast<int16_t>(val & 0xffff));
    }
    if (bitlen == 32) {
        return static_cast<int64_t>(static_cast<int32_t>(val & 0xffffffffu));
    }
    return val;
}

// Guard / flag like M_ClockedAnalogInputs + G_CoreE24:
//   - null guard + no flag → emit allowed
//   - integer flag 0 → off; non-zero → on
//   - guard DISABLED or state "off"/"false" → off
//   - otherwise on
bool emitAllowed(MachineInstance *guard, const int64_t *emit_flag) {
    if (emit_flag && *emit_flag == 0) {
        return false;
    }
    if (!guard) {
        return true;
    }
    if (!guard->enabled()) {
        return false;
    }
    const char *st = guard->getCurrentStateString();
    if (!st) {
        return true;
    }
    if (strcasecmp(st, "off") == 0 || strcasecmp(st, "false") == 0) {
        return false;
    }
    return true;
}

// Engineering scale from machine OPTION (same as CLOCKEDANALOGINPUT A_*):
//   VALUE = raw_filtered * factor + base
//   window = eng-unit hysteresis before change-emit
void readScaleOptions(MachineInstance *m, double &factor, double &base, double &window) {
    factor = 1.0;
    base = 0.0;
    window = 0.0;
    if (!m) {
        return;
    }
    auto as_float = [](const Value &v, double &out) -> bool {
        if (v.kind == Value::t_float) {
            out = v.fValue;
            return true;
        }
        if (v.kind == Value::t_integer) {
            out = static_cast<double>(v.iValue);
            return true;
        }
        return v.asFloat(out);
    };
    as_float(m->properties.lookup("factor"), factor);
    as_float(m->properties.lookup("base"), base);
    as_float(m->properties.lookup("window"), window);
}

double engFromRaw(int64_t raw, double factor, double base) {
    return static_cast<double>(raw) * factor + base;
}

// First emit, or eng/raw moved past owner window (max-rate limited by notify_period).
bool notifyValueChanged(const std::list<MachineInstance *> &owners, int64_t raw_val,
                        int64_t last_raw, double last_eng, bool startup_done) {
    if (!startup_done) {
        return true;
    }
    if (owners.empty()) {
        return raw_val != last_raw;
    }
    for (MachineInstance *o : owners) {
        if (!o) {
            continue;
        }
        double factor = 1.0, base = 0.0, window = 0.0;
        readScaleOptions(o, factor, base, window);
        const double eng = engFromRaw(raw_val, factor, base);
        if (window <= 0.0) {
            if (raw_val != last_raw) {
                return true;
            }
        }
        else {
            double deng = eng - last_eng;
            if (deng < 0) {
                deng = -deng;
            }
            if (deng > window) {
                return true;
            }
        }
    }
    return false;
}
} // namespace

class InputFilterSettings {
  public:
    bool property_changed;
    CircularBuffer *positions;
    double last_sent;    // this is the value to send unless the read value moves away from the mean
    double prev_sent;    // this is previous value of last_sent
    uint64_t last_time;    // last filter/sample tick (monotonic sample clock)
    uint64_t last_emit_us; // last CW emit (change or safety)
    int64_t last_emitted_raw; // last raw filtered value emitted
    double last_emitted_eng;  // last engineering VALUE emitted (factor/base)
    bool startup_emitted; // true after first emit
    uint16_t buffer_len; // the maximum length of the circular buffer
    const int64_t
        *tolerance; // some filters use a tolerance settable by the user in the "tolerance" property
    double *filter_c_coeff;     // the Butterworth filter uses these coefficients
    double *filter_d_coeff;     // the Butterworth filter uses these coefficients
    const int64_t *filter_len;  // adjust the filter length of some filters
    const int64_t *filter_type; // the user can select the filter using a "filter" property
    const int64_t *calc_dt;
    const int64_t *calc_d2t;
    const int64_t *calc_stddev;
    const int64_t
        *position_history;          // the amount of position history to use in determining movement
    const int64_t *speed_tolerance; // the tolerance used in determining movement
    size_t butterworth_len;         // the number of coefficients in the Butterworth filter
    double speed;                   // the current estimated speed
    double speed_scale;             // the current estimated speed
    double accel;
    double accel_scale;
    static int64_t default_tolerance;        // a default value for filter_len
    static int64_t default_filter_len;       // a default value for filter_len
    static int64_t default_speed_filter_len; // a default value for speed_filter_len
    static int64_t default_calc_dt;
    static int64_t default_calc_d2t;
    static int64_t default_calc_stddev;
    static int64_t default_position_history; // a default value for position_history
    static int64_t default_speed_tolerance;  // a default value for speed_tolerance
    FloatBuffer speeds;
    int rate_len;
    ButterworthFilter *input_bwf;
    ButterworthFilter *vel_bwf;
    ButterworthFilter *accel_bwf;
    const int64_t *throttle;     // ms filter advance period (alias: rate on settings)
    const int64_t *safety_emit;  // legacy; not used for notify schedule
    const int64_t *emit_flag;    // optional 0/1 flag (emit / enable)
    MachineInstance *emit_guard; // optional on/off|true/false machine (e.g. G_CoreE24)
    CommandClock notify_clock;   // dependant command cadence (owner notify_period)

    InputFilterSettings()
        : property_changed(true), positions(0), last_sent(0.0), prev_sent(0.0), last_time(0),
          last_emit_us(0), last_emitted_raw(0), last_emitted_eng(0.0), startup_emitted(false),
          buffer_len(200), tolerance(&default_tolerance), filter_c_coeff(0), filter_d_coeff(0),
          filter_len(&default_filter_len), filter_type(0), calc_dt(&default_calc_dt),
          calc_d2t(&default_calc_d2t), calc_stddev(&default_calc_stddev),
          position_history(&default_position_history), speed_tolerance(&default_speed_tolerance),
          speed(0.0), speed_scale(1.0), accel(0.0), accel_scale(1.0), speeds(4), rate_len(4),
          input_bwf(0), vel_bwf(0), accel_bwf(0), throttle(0), safety_emit(0), emit_flag(0),
          emit_guard(0) {

        //double bw_c[] = { 0.000003756838020,0.000011270514059,0.000011270514059,0.000003756838020 };
        //double bw_d[] = { 1.000000000000,-2.937170728450,2.876299723479,-0.939098940325 };
        double bw_c[] = {0.002898194633721, 0.008694583901164, 0.008694583901164,
                         0.002898194633721};
        double bw_d[] = {1.000000000000, -2.374094743709, 1.929355669091, -0.532075368312};
        //double c[] = {0.081,0.215,0.541,0.865,1,0.865,0.541,0.215,0.081};
        butterworth_len = sizeof(bw_c) / sizeof(double);
        filter_c_coeff = new double[butterworth_len];
        memmove(filter_c_coeff, bw_c, sizeof(bw_c));
        filter_d_coeff = new double[butterworth_len];
        memmove(filter_d_coeff, bw_c, sizeof(bw_d));
        input_bwf = new ButterworthFilter(butterworth_len, bw_c, butterworth_len, bw_d);
        vel_bwf = new ButterworthFilter(butterworth_len, bw_c, butterworth_len, bw_d);
        accel_bwf = new ButterworthFilter(butterworth_len, bw_c, butterworth_len, bw_d);
        positions = createBuffer(buffer_len);
    }

    ~InputFilterSettings();

    void update(uint64_t read_time) {
#if 0
        double smoothing_coeff[] = { -7, 8, 8, 0, -9, -12, -2, 28, 85 };
        double first_derivative_coeff[] = { -2086, 1862, 2441, 918, -1440, -3366, -3593, -854, 6118 };
        double second_derivative_coeff[] = { -308, 217, 340, 201, -60, -303, -388, -175, 476 };
#define SMOOTHING_NORM 99.0f
#define FIRST_DERIV_NORM 8316.0f
#define SECOND_DERIV_NORM 1386.0f
        int smoothing_len = sizeof(smoothing_coeff) / sizeof(double);
#else
        double smoothing_coeff[] = {-2, 4, 1, -4, -4, 8, 39};
        double first_derivative_coeff[] = {-77, 122, 77, -72, -185, -122, 257};
        double second_derivative_coeff[] = {-16, 21, 18, -4, -24, -21, 26};
#define SMOOTHING_NORM 42.0
#define FIRST_DERIV_NORM 252.0
#define SECOND_DERIV_NORM 42.0
        size_t smoothing_len = sizeof(smoothing_coeff) / sizeof(double);
        assert(smoothing_len == 7);
#endif

        // replace the raw value the positions buffer with the filtered value
        //std::cout << read_time << " replacing pos: " << getBufferValue(positions, 0) << " with " << last_sent << "\n";
        setBufferValue(positions, last_sent);
        //if (prev_sent == 0.0) prev_sent = last_sent; TBD wrong?
        if (last_time == 0) {
            last_time = read_time;
            prev_sent = last_sent;
            speed = 0.0;
            speeds.append(speed);
        }
        else if ((int64_t)(read_time - last_time) >= ((throttle) ? (*throttle * 1000L) : 10000L)) {
            double dt = ((double)(read_time - last_time)) / 1000000.0;
            //speed = (getBufferValue(positions,0) - getBufferValue(positions,1)) / dt;
            //rate_len = findMovement(positions, 20, *position_history);
            // if there has been movement in the last N (N=20) readings, calculate speed
            //if (rate_len < *position_history) {
            if (*calc_dt) {
                speed = savitsky_golay_filter(positions, smoothing_len, first_derivative_coeff,
                                              FIRST_DERIV_NORM);
                speed = speed / dt;
                //speed = vel_bwf->filter(speed);
                //speed = 1000000.0 * rate(positions, (rate_len<4)? 4 : rate_len);
                //std::cout << "computed speed " << speed << " at " << getBufferValueAt(positions, 0) << "\n";
                //}
                //else speed = 0.0;
                speeds.append(speed);
            }
            if (*calc_d2t) {
                //accel = 0.0;
                //accel = (speeds.get(0) - speeds.get(1)) / dt;
                accel = savitsky_golay_filter(positions, smoothing_len, second_derivative_coeff,
                                              SECOND_DERIV_NORM);
                accel = accel / dt;
                //accel = accel_bwf->filter(accel / dt);
            }

            last_time = read_time;
            prev_sent = last_sent;
        }
    }

    double filter() {
        int64_t filter_length = *filter_len;
        if ((unsigned int)bufferLength(positions) < filter_length) {
            return getBufferValue(positions, 0);
        }
        double c[] = {0.081, 0.215, 0.541, 0.865, 1, 0.865, 0.541, 0.215, 0.081};
        double res = 0;
        for (ssize_t i = 0; i < filter_length; ++i) {
            double f = (double)getBufferValue(positions, i);
            //printf(" %.3f,%.3f ",f, f*c[i]);
            res += f * c[i];
        }
        //printf(" %.3f\n",res);

        return res;
    }
};

int64_t InputFilterSettings::default_tolerance = 8;         // a default value for filter_len
int64_t InputFilterSettings::default_filter_len = 12;       // a default value for filter_len
int64_t InputFilterSettings::default_speed_filter_len = 4;  // a default value for speed_filter_len
int64_t InputFilterSettings::default_position_history = 20; // a default value for position_history
int64_t InputFilterSettings::default_speed_tolerance = 20;  // a default value for speed_tolerance
int64_t InputFilterSettings::default_calc_dt = 0;           // calculate first deriv
int64_t InputFilterSettings::default_calc_d2t = 0;          // calculate second deriv
int64_t InputFilterSettings::default_calc_stddev = 0;       // don't calculate stddev

InputFilterSettings::~InputFilterSettings() {
    destroyBuffer(positions);
    delete[] filter_c_coeff;
    delete[] filter_d_coeff;
    delete input_bwf;
    delete vel_bwf;
    delete accel_bwf;
}

AnalogueInput::AnalogueInput(IOAddress addr) : IOComponent(addr) {
    config = new InputFilterSettings();
    direction_ = DirInput;
    regular_polls.insert(this);
}

static bool getFloatValue(MachineInstance *scope, const char *name, double &result) {
    const Value &v = scope->getValue(name);
    if (v.kind == Value::t_integer || v.kind == Value::t_float) {
        double res;
        if (v.asFloat(res)) {
            result = res;
            return true;
        }
    }
    return false;
}

void AnalogueInput::setupProperties(MachineInstance *m) {
    MachineInstance *settings = 0;
    int64_t object_subindex = 0;
    if (m->parameters.size() >= 4 &&
        m->parameters[2].val.asInteger(object_subindex)) {
        size_t settings_param = 3;
        int64_t pdo_index = 0;
        if (m->parameters.size() >= 5 &&
            m->parameters[3].val.asInteger(pdo_index)) {
            settings_param = 4;
        }
        if (m->parameters.size() > settings_param) {
            settings = m->lookup(m->parameters[settings_param]);
        }
    }
    else {
        settings = m->lookup("filter_settings");
    }
    if (settings) {
        double speed_scale = 1.0, accel_scale = 1.0;
        if (getFloatValue(settings, "velocity_scale", speed_scale)) {
            config->speed_scale = speed_scale;
            std::cout << m->getName() << " set velocity scale to: " << config->speed_scale << "\n";
        }
        if (getFloatValue(settings, "acceleration_scale", accel_scale)) {
            config->accel_scale = accel_scale;
            std::cout << m->getName() << " set accel scale to: " << config->accel_scale << "\n";
        }
        // throttle or rate (ms): filter/change tick — plant CLOCKING rate:100 → 100
        const Value &throttle_v = settings->getValue("throttle");
        if (throttle_v.kind == Value::t_integer) {
            config->throttle = &throttle_v.iValue;
            std::cout << m->getName() << " set throttle/rate to: " << *config->throttle << "ms\n";
        }
        else {
            const Value &rate_v = settings->getValue("rate");
            if (rate_v.kind == Value::t_integer) {
                config->throttle = &rate_v.iValue;
                std::cout << m->getName() << " set rate (throttle) to: " << *config->throttle
                          << "ms\n";
            }
        }
        // safety_emit (ms): always re-emit at least this often (default 1000)
        const Value &safety_v = settings->getValue("safety_emit");
        if (safety_v.kind == Value::t_integer) {
            config->safety_emit = &safety_v.iValue;
        }
        // emit flag 0/1 (or enable) — same role as CLOCKINGWITHENABLE off
        const Value &emit_v = settings->getValue("emit");
        if (emit_v.kind == Value::t_integer) {
            config->emit_flag = &emit_v.iValue;
        }
        else {
            const Value &en_v = settings->getValue("enable");
            if (en_v.kind == Value::t_integer) {
                config->emit_flag = &en_v.iValue;
            }
        }
        // guard machine: on/off or true/false (e.g. G_CoreE24)
        MachineInstance *guard = settings->lookup("guard");
        if (!guard) {
            guard = settings->lookup("emit_guard");
        }
        if (!guard) {
            guard = m->lookup("emit_guard");
        }
        if (guard) {
            config->emit_guard = guard;
            std::cout << m->getName() << " emit_guard=" << guard->getName() << "\n";
        }
        const Value &conf_dt = settings->getValue("enable_velocity");
        if (conf_dt.kind == Value::t_integer) {
            config->calc_dt = &conf_dt.iValue;
        }
        const Value &conf_d2t = settings->getValue("enable_acceleration");
        if (conf_d2t.kind == Value::t_integer) {
            config->calc_d2t = &conf_d2t.iValue;
        }
        const Value &conf_stddev = settings->getValue("enable_stddev");
        if (conf_stddev.kind == Value::t_integer) {
            config->calc_stddev = &conf_stddev.iValue;
        }
        MachineInstance *c_coeff = settings->lookup("C");
        MachineInstance *d_coeff = settings->lookup("D");
        if (c_coeff && d_coeff) {
            size_t num_c = c_coeff->parameters.size();
            size_t num_d = d_coeff->parameters.size();
            if (num_c > 0 && num_c == num_d) {
                config->butterworth_len = num_c;
                delete[] config->filter_c_coeff;
                delete[] config->filter_d_coeff;
                config->filter_c_coeff = new double[num_c];
                config->filter_d_coeff = new double[num_d];
                std::cout << "C: " << (int)num_c;
                for (unsigned int i = 0; i < num_c; ++i) {
                    double val;
                    config->filter_c_coeff[i] = c_coeff->parameters[i].val.asFloat(val) ? val : 1.0;
                    std::cout << " " << config->filter_c_coeff[i];
                }
                std::cout << "\nD: " << (int)num_d;
                for (unsigned int i = 0; i < num_d; ++i) {
                    double val;
                    config->filter_d_coeff[i] = d_coeff->parameters[i].val.asFloat(val) ? val : 1.0;
                    std::cout << " " << config->filter_d_coeff[i];
                }
                std::cout << "\n";
                // swap the filter, TBD copy current values from old filter?
                ButterworthFilter *input_bwf = new ButterworthFilter(num_c, config->filter_c_coeff,
                                                                     num_d, config->filter_d_coeff);
                delete config->input_bwf;
                config->input_bwf = input_bwf;
            }
            else {
                std::cout << "filter parameters are incorrect: \n";
            }
        }
        const Value &v1 = settings->getValue("tolerance");
        if (v1.kind == Value::t_integer) {
            config->tolerance = &v1.iValue;
        }
        const Value &v2 = settings->getValue("filter");
        if (v2.kind == Value::t_integer) {
            config->filter_type = &v2.iValue;
            if (*config->filter_type == 2) {
                std::cout << "butterworth filter order " << config->butterworth_len << "\n";
            }
        }
        const Value &v3 = settings->getValue("filter_len");
        if (v3.kind == Value::t_integer) {
            config->filter_len = &v3.iValue;
        }
        const Value &v4 = settings->getValue("speed_tolerance");
        if (v4.kind == Value::t_integer) {
            config->speed_tolerance = &v4.iValue;
        }
        const Value &v5 = settings->getValue("position_history");
        if (v5.kind == Value::t_integer) {
            config->position_history = &v5.iValue;
        }
    }
    else {
        std::cout << "Warning: analog input " << m->getName() << " has no filter settings\n";
    }
    applyOwnerSignedFlag(m, address);
}

int64_t AnalogueInput::filter(int64_t raw) {
    /*  Plugins (and CW) need IOTIME/raw on every EtherCAT sample, even when the
        filtered VALUE is unchanged. properties.add does not notify dependents. */
    publishSampleTime(read_time, true, raw);

    // Period gates use wall clock (microsecs). Do not mix with EC sample
    // read_time / application clock — different epochs make filter always-due.
    const uint64_t filter_us = filterPeriodUs(config->throttle);
    const uint64_t now_us = microsecs();
    const bool filter_due =
        (config->last_time == 0) || (now_us - config->last_time >= filter_us);

    if (filter_due) {
        if (config->property_changed) {
            config->property_changed = false;
        }

        // Filter + tolerance (setup standard). Only advance last_sent past tol.
        addSample(config->positions, (long)read_time, (double)raw);
        int64_t candidate = static_cast<int64_t>(config->last_sent);
        if (config->filter_type && *config->filter_type == 0) { // raw
            candidate = raw;
        }
        else if (!config->filter_type || (config->filter_type && *config->filter_type == 1)) {
            candidate = static_cast<int64_t>(
                bufferAverage(config->positions, static_cast<size_t>(*config->filter_len)) + 0.5f);
        }
        else if (config->filter_type && *config->filter_type == 2) {
            if (config->input_bwf) {
                candidate = static_cast<int64_t>(config->input_bwf->filter((float)raw) + 0.5);
            }
            else {
                assert(false);
            }
        }
        {
            const int64_t tol =
                (config->tolerance && *config->tolerance > 0) ? *config->tolerance : 1;
            int64_t delta = candidate - static_cast<int64_t>(config->last_sent);
            if (delta < 0) {
                delta = -delta;
            }
            if (delta >= tol) {
                config->last_sent = candidate;
            }
        }
        config->update(read_time);
        config->last_time = now_us;
    }

    // Silent property publish every poll (no PROPERTY_CHANGE / no dependant wake).
    // Dependant command fan-out only on owner notify_period (COMMANDCLOCK-style).
    const int64_t raw_val = static_cast<int64_t>(config->last_sent);
    const bool allowed = emitAllowed(config->emit_guard, config->emit_flag);

    for (MachineInstance *o : owners) {
        if (!o) {
            continue;
        }
        double factor = 1.0, base = 0.0, window = 0.0;
        readScaleOptions(o, factor, base, window);
        const double eng = engFromRaw(raw_val, factor, base);
        o->properties.add("VALUE", raw_val, SymbolTable::ST_REPLACE);
        o->properties.add("ENG", eng, SymbolTable::ST_REPLACE);
        o->properties.add("DurationTolerance", config->rate_len, SymbolTable::ST_REPLACE);
        if (*config->calc_stddev) {
            o->properties.add("stddev", bufferStddev(config->positions, 5),
                              SymbolTable::ST_REPLACE);
        }
        if (*config->calc_dt) {
            o->properties.add("Velocity", config->speed * config->speed_scale,
                              SymbolTable::ST_REPLACE);
        }
        if (*config->calc_d2t) {
            o->properties.add("Acceleration", config->accel * config->accel_scale,
                              SymbolTable::ST_REPLACE);
        }

    }

    // Max rate = notify_period; only SEND when value changed (or first emit).
    MachineInstance *notify_owner = owners.empty() ? nullptr : owners.front();
    const uint64_t period_ms = ownerNotifyPeriodMs(notify_owner);
    if (config->notify_clock.due(now_us, period_ms, allowed)) {
        if (notifyValueChanged(owners, raw_val, config->last_emitted_raw,
                               config->last_emitted_eng, config->startup_emitted)) {
            const char *cmd = ownerCommandName(notify_owner, "update");
            double eng_for_track = static_cast<double>(raw_val);
            for (MachineInstance *o : owners) {
                if (!o) {
                    continue;
                }
                double factor = 1.0, base = 0.0, window = 0.0;
                readScaleOptions(o, factor, base, window);
                eng_for_track = engFromRaw(raw_val, factor, base);
                DBG_MESSAGING << o->getName() << " iod " << cmd
                              << " notify_period=" << period_ms << "ms (change)\n";
                o->notifyCommandConsumers(cmd);
            }
            config->last_emitted_raw = raw_val;
            config->last_emitted_eng = eng_for_track;
            config->last_emit_us = now_us;
            config->startup_emitted = true;
        }
    }
    return raw_val;
}

void AnalogueInput::update() { config->property_changed = false; }

class CounterInternals {
  public:
    CircularBuffer *positions;
    const int64_t
        *tolerance; // some filters use a tolerance settable by the user in the "tolerance" property
    const int64_t *
        filter_len; // the user can adjust the filter length of some filters via a "filter_len" property
    const int64_t
        *position_history;          // the amount of position history to use in determining movement
    const int64_t *speed_tolerance; // the tolerance used in determining movement
    const int64_t *input_scale;     // input readings are divided by this amount
    const int64_t *throttle;        // ms filter advance (owner throttle; rate = legacy alias)
    const int64_t *safety_emit;     // legacy; not used for notify schedule
    const int64_t *emit_flag;       // 0/1 enable
    MachineInstance *emit_guard;    // on/off|true/false guard
    CommandClock notify_clock;      // dependant command cadence (owner notify_period)
    int64_t last_sent;  // filtered position (raw counts / scaled input)
    int64_t prev_sent;
    int64_t last_emitted_raw;
    double last_emitted_eng;
    uint64_t last_time;    // last filter tick
    uint64_t last_emit_us; // last CW emit
    bool startup_emitted;
    static int64_t default_tolerance;
    static int64_t default_filter_len;
    static int64_t default_position_history;
    static int64_t default_speed_tolerance;
    static int64_t default_input_scale;
    int64_t speed;
    uint16_t buffer_len;
    FloatBuffer speeds;
    ssize_t rate_len;

    CounterInternals()
        : positions(0), tolerance(&default_tolerance), filter_len(&default_filter_len),
          position_history(&default_position_history), speed_tolerance(&default_speed_tolerance),
          input_scale(&default_input_scale), throttle(0), safety_emit(0), emit_flag(0),
          emit_guard(0), last_sent(0), prev_sent(0), last_emitted_raw(0), last_emitted_eng(0.0),
          last_time(0), last_emit_us(0), startup_emitted(false), speed(0), buffer_len(200),
          speeds(4), rate_len(4) {
        positions = createBuffer(buffer_len);
    }

    void update(uint64_t read_time) {
        if (prev_sent == 0) {
            prev_sent = last_sent;
        }
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
                    speed = (int64_t)( dv / dt * 1000000.0) ;
            */
            rate_len = findMovement(positions, 20, static_cast<size_t>(*position_history));
            if (rate_len < *position_history) {
                speed = 1000000.0 * rate(positions, (rate_len < 4) ? 4 : rate_len);
            }
            else {
                speed = 0.0;
            }
            speeds.append(speed);
            last_time = read_time;
            prev_sent = last_sent;
        }
    }

    double filter() {
        if ((unsigned int)bufferLength(positions) < 9) {
            return getBufferValue(positions, 0);
        }
        double c[] = {0.081, 0.215, 0.541, 0.865, 1, 0.865, 0.541, 0.215, 0.081};
        double res = 0;
        for (size_t i = 0; i < 9; ++i) {
            double f = (double)getBufferValue(positions, i);
            res += f * c[i];
        }

        return res;
    }
};

DigitalValue::DigitalValue(IOAddress addr) : IOComponent(addr) {
    // Inputs only (0x603F/0x6041 etc.). Default IOComponent is bidirectional,
    // which let generateUpdateMask / updateDomain poison g_kernel_output_mask
    // and zero process-image inputs on every mergeKernelOutputShadow.
    direction_ = DirInput;
    // Sample like ANALOGINPUT/COUNTER: multi-bit CiA objects (error code,
    // statusword) must track 0 as well as non-zero. Edge-only processAll can
    // miss a transition when the first bit of the value is unchanged (e.g.
    // 0x76 → 0x00), leaving Error.VALUE stuck and ClearFault thrashing.
    regular_polls.insert(this);
}

int64_t DigitalValue::filter(int64_t val) {
    // IO only: stamp sample time and publish masked VALUE. Bit decode / flags
    // belong in Clockwork, not here.
    publishSampleTime(read_time);
    std::list<MachineInstance *>::iterator owners_iter = owners.begin();
    while (owners_iter != owners.end()) {
        MachineInstance *o = *owners_iter++;
        if (!o) {
            continue;
        }
        Value mask = o->properties.lookup("MASK");
        int64_t new_val = val;
        if (mask.kind == Value::t_integer && mask.iValue != 0) {
            new_val = val & mask.iValue;
        }
        // setValue is relatively expensive (authority/modbus/dependents).
        const Value &cur = o->properties.lookup("VALUE");
        if (cur.kind == Value::t_integer && cur.iValue == new_val) {
            continue;
        }
        o->setValue("VALUE", Value{new_val});
    }
    return val;
}

int64_t CounterInternals::default_tolerance = 1;
int64_t CounterInternals::default_filter_len = 8;
int64_t CounterInternals::default_position_history = 20;
int64_t CounterInternals::default_speed_tolerance = 10;
int64_t CounterInternals::default_input_scale = 1;

Counter::Counter(IOAddress addr) : IOComponent(addr), internals(0) {
    internals = new CounterInternals;
    regular_polls.insert(this);
}

void Counter::setupProperties(MachineInstance *m) {
    const Value &v = m->getValue("tolerance");
    if (v.kind == Value::t_integer) {
        internals->tolerance = &v.iValue;
    }
    const Value &v3 = m->getValue("filter_len");
    if (v3.kind == Value::t_integer) {
        internals->filter_len = &v3.iValue;
    }
    const Value &v4 = m->getValue("speed_tolerance");
    if (v4.kind == Value::t_integer) {
        internals->speed_tolerance = &v4.iValue;
    }
    const Value &v5 = m->getValue("position_history");
    if (v5.kind == Value::t_integer) {
        internals->position_history = &v5.iValue;
    }
    const Value &v6 = m->getValue("input_scale");
    if (v6.kind == Value::t_integer) {
        internals->input_scale = &v6.iValue;
    }
    // Filter period: throttle only (legacy rate = throttle alias, not notify_period).
    const Value &th = m->getValue("throttle");
    if (th.kind == Value::t_integer) {
        internals->throttle = &th.iValue;
    }
    else {
        const Value &rate = m->getValue("rate");
        if (rate.kind == Value::t_integer) {
            internals->throttle = &rate.iValue;
        }
    }
    const Value &safety = m->getValue("safety_emit");
    if (safety.kind == Value::t_integer) {
        internals->safety_emit = &safety.iValue;
    }
    const Value &emit_v = m->getValue("emit");
    if (emit_v.kind == Value::t_integer) {
        internals->emit_flag = &emit_v.iValue;
    }
    else {
        const Value &en = m->getValue("enable");
        if (en.kind == Value::t_integer) {
            internals->emit_flag = &en.iValue;
        }
    }
    MachineInstance *guard = m->lookup("emit_guard");
    if (!guard) {
        guard = m->lookup("guard");
    }
    if (guard) {
        internals->emit_guard = guard;
    }
    applyOwnerSignedFlag(m, address);
}

int64_t Counter::filter(int64_t val) {
    publishSampleTime(read_time);

    // Wall-clock periods only (see AnalogueInput::filter).
    const uint64_t filter_us = filterPeriodUs(internals->throttle);
    const uint64_t now_us = microsecs();
    const bool filter_due =
        (internals->last_time == 0) || (now_us - internals->last_time >= filter_us);

    if (filter_due) {
        double scaled_val = (double)val / (double)*internals->input_scale;
        addSample(internals->positions, (long)read_time, scaled_val);
        if (*internals->tolerance > 1) {
            int64_t mean = static_cast<int64_t>(
                bufferAverage(internals->positions, static_cast<size_t>(*internals->filter_len)) +
                0.5f);
            long delta = static_cast<long>(abs(mean - internals->last_sent));
            if (delta >= *internals->tolerance) {
                internals->last_sent = mean;
            }
        }
        else {
            internals->last_sent =
                (*internals->input_scale == 1) ? val : static_cast<int64_t>(scaled_val + 0.5);
        }
        internals->update(read_time);
        internals->last_time = now_us;
    }

    const int64_t raw_val = internals->last_sent;
    const bool allowed = emitAllowed(internals->emit_guard, internals->emit_flag);

    // Silent property publish every poll.
    for (MachineInstance *o : owners) {
        if (!o) {
            continue;
        }
        double factor = 1.0, base = 0.0, window = 0.0;
        readScaleOptions(o, factor, base, window);
        const double eng = engFromRaw(raw_val, factor, base);
        o->properties.add("VALUE", raw_val, SymbolTable::ST_REPLACE);
        o->properties.add("Position", raw_val, SymbolTable::ST_REPLACE);
        o->properties.add("ENG", eng, SymbolTable::ST_REPLACE);
        o->properties.add("DurationTolerance", static_cast<uint64_t>(internals->rate_len),
                          SymbolTable::ST_REPLACE);
        o->properties.add("Velocity", internals->speeds.average(internals->speeds.length()),
                          SymbolTable::ST_REPLACE);
    }

    MachineInstance *notify_owner = owners.empty() ? nullptr : owners.front();
    const uint64_t period_ms = ownerNotifyPeriodMs(notify_owner);
    if (internals->notify_clock.due(now_us, period_ms, allowed)) {
        if (notifyValueChanged(owners, raw_val, internals->last_emitted_raw,
                               internals->last_emitted_eng, internals->startup_emitted)) {
            const char *cmd = ownerCommandName(notify_owner, "update");
            double eng_for_track = static_cast<double>(raw_val);
            for (MachineInstance *o : owners) {
                if (!o) {
                    continue;
                }
                double factor = 1.0, base = 0.0, window = 0.0;
                readScaleOptions(o, factor, base, window);
                eng_for_track = engFromRaw(raw_val, factor, base);
                DBG_MESSAGING << o->getName() << " iod " << cmd
                              << " notify_period=" << period_ms << "ms (change)\n";
                o->notifyCommandConsumers(cmd);
            }
            internals->last_emitted_raw = raw_val;
            internals->last_emitted_eng = eng_for_track;
            internals->last_emit_us = now_us;
            internals->startup_emitted = true;
        }
    }
    return raw_val;
}

CounterRate::CounterRate(IOAddress addr) : IOComponent(addr), times(16), positions(0) {
    start_t = microsecs();
}

int64_t CounterRate::filter(int64_t val) {
#if 0
    /*  as for the AnalogueInput, note that these 'IO' properties do not
        cause value change notifications throughout clockwork
    */
    std::list<MachineInstance *>::iterator owners_iter = owners.begin();
    while (owrars_iter != owners.end()) {
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
    uint64_t max_forward;
    uint64_t max_reverse;
    float Kp;
    uint64_t set_point;

    float estimated_speed;
    float Pe;      // used for process error: SetPoint - EstimatedSpeed;
    float current; // current power level this is our PV (control variable)
    float last_pos;
    uint64_t position;
    uint64_t measure_time;
    uint64_t last_time;
    float last_power;
    uint64_t slow_speed;
    uint64_t full_speed;
    uint64_t stopping_time;
    uint64_t stopping_distance;
    uint64_t tolerance;
    uint64_t min_update_time;
    uint64_t deceleration_allowance;
    float deceleration_rate;

    PID_Settings()
        : property_changed(true), max_forward(16383), max_reverse(-16383), Kp(20.0), set_point(0),
          estimated_speed(0.0f), Pe(0.0f), current(0), last_pos(0.0f), position(0), measure_time(0),
          last_time(0), last_power(0), slow_speed(200), full_speed(1000), stopping_time(300),
          stopping_distance(2000), tolerance(10), min_update_time(20), deceleration_allowance(20),
          deceleration_rate(0.8f) {}
};

int64_t PIDController::filter(int64_t raw) { return raw; }

PIDController::PIDController(IOAddress addr) : Output(addr), config(0) {
    config = new PID_Settings();
}

PIDController::~PIDController() { delete config; }

void PIDController::update() { config->property_changed = true; }

void PIDController::handleChange(std::list<Package *> &work_queue) {
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
    if (last_event == e_change && address.bitlen > 1) {
        return "unstable";
    }
    else if (address.bitlen > 1) {
        return "stable";
    }
    else if (last_event == e_on) {
        return "turning_on";
    }
    else if (last_event == e_off) {
        return "turning_off";
    }
    else if (address.value == 0) {
        return "off";
    }
    else {
        return "on";
    }
}

std::vector<std::list<IOComponent *> *> io_map;

int IOComponent::getMinIOOffset() { return min_offset; }

int IOComponent::getMaxIOOffset() { return max_offset; }

int IOComponent::notifyComponentsAt(unsigned int offset) {
    assert(offset <= max_offset && offset >= min_offset);
    int count = 0;
    std::list<IOComponent *> *cl = io_map[offset];
    if (cl) {
        //std::cout << "component list at offset " << offset << " size: " << cl->size() << "\n";
        std::list<IOComponent *>::iterator items = cl->begin();
        //std::cout << "items: \n";
        //while (items != cl->end()) {
        //  IOComponent *c = *items++;
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
    if (res && len != max + 1) {
        delete[] res;
        res = 0;
    }
    if (!res) {
        res = new uint8_t[max + 1];
    }
    memset(res, 0, max + 1);

    IOComponent::Iterator iter = IOComponent::begin();
    while (iter != IOComponent::end()) {
        IOComponent *ioc = *iter++;
        unsigned int offset = ioc->address.io_offset;
        unsigned int bitpos = ioc->address.io_bitpos;
        offset += bitpos / 8;
        bitpos = bitpos % 8;
        if (offset > max) {
            continue;
        }
        uint8_t mask = 0x01 << bitpos;
        // set  a bit in the mask for each bit of this value
        for (unsigned int i = 0; i < ioc->address.bitlen; ++i) {
            if (offset > max) {
                break;
            }
            res[offset] |= mask;
            mask = mask << 1;
            if (!mask) {
                mask = 0x01;
                ++offset;
            }
        }
    }
#if VERBOSE_DEBUG
    std::cout << " Process Mask: ";
    display(res, len);
    std::cout << "\n";
#endif
    return res;
}

// copy the provied data to the default data block
void IOComponent::setDefaultData(uint8_t *data) {
#if VERBOSE_DEBUG
    std::cout << "Setting default data to : \n";
    display(data, process_data_size);
    std::cout << "\n";
#endif
    if (!default_data) {
        default_data = new uint8_t[process_data_size];
    }
    memcpy(default_data, data, process_data_size);
}

// copy the provided mask to the default data mask
void IOComponent::setDefaultMask(uint8_t *mask) {
#if VERBOSE_DEBUG
    std::cout << "Setting default mask to : \n";
    display(mask, process_data_size);
    std::cout << "\n";
#endif
    if (!default_mask) {
        default_mask = new uint8_t[process_data_size];
    }
    memcpy(default_mask, mask, process_data_size);
}

uint8_t *IOComponent::generateMask(std::list<MachineInstance *> &outputs) {
    unsigned int max = IOComponent::getMaxIOOffset();
    // process data size
    uint8_t *res = new uint8_t[max + 1];
    memset(res, 0, max + 1);

    std::list<MachineInstance *>::iterator iter = outputs.begin();
    while (iter != outputs.end()) {
        MachineInstance *m = *iter++;
        IOComponent *ioc = m->io_interface;
        if (ioc) {
            unsigned int offset = ioc->address.io_offset;
            unsigned int bitpos = ioc->address.io_bitpos;
            offset += bitpos / 8;
            bitpos = bitpos % 8;
            uint8_t mask = 0x01 << bitpos;
            // set  a bit in the mask for each bit of this value
            for (unsigned int i = 0; i < ioc->address.bitlen; ++i) {
                res[offset] |= mask;
                mask = mask << 1;
                if (!mask) {
                    mask = 0x01;
                    ++offset;
                }
            }
        }
    }
    return res;
}

bool IOComponent::ownersEnabled() const {
    if (owners.empty()) {
        return true;
    }
    else {
        std::list<MachineInstance *>::const_iterator owners_iter = owners.begin();
        while (owners_iter != owners.end()) {
            MachineInstance *o = *owners_iter++;
            if (o->enabled()) {
                return true;
            }
        }
    }
    return false;
}

static uint8_t *generateUpdateMask() {
    //  returns null if there are no updates, otherwise returns
    // a mask for the update data

    //std::cout << "generating mask\n";
    if (updatedComponentsOut.empty()) {
        return 0;
    }

    unsigned int min = IOComponent::getMinIOOffset();
    unsigned int max = IOComponent::getMaxIOOffset();
    uint8_t *res = new uint8_t[max + 1];
    memset(res, 0, max + 1);
    //std::cout << "mask is " << (max+1) << " bytes\n";

    std::set<IOComponent *>::iterator iter = updatedComponentsOut.begin();
    while (iter != updatedComponentsOut.end()) {
        IOComponent *ioc = *iter; //TBD this can be null
        if (ioc->ownersEnabled()) {
            iter++;
        }
        else {
            iter = updatedComponentsOut.erase(iter);
        }

        if (ioc->direction() != IOComponent::DirOutput &&
            ioc->direction() != IOComponent::DirBidirectional) {
            continue;
        }
        unsigned int offset = ioc->address.io_offset;
        unsigned int bitpos = ioc->address.io_bitpos;
        offset += bitpos / 8;
        bitpos = bitpos % 8;
        uint8_t mask = 0x01 << bitpos;
        // set  a bit in the mask for each bit of this value
        for (unsigned int i = 0; i < ioc->address.bitlen; ++i) {
            res[offset] |= mask;
            mask = mask << 1;
            if (!mask) {
                mask = 0x01;
                ++offset;
            }
        }
    }
#if VERBOSE_DEBUG
    std::cout << "generated mask: ";
    display(res, max - min + 1);
    std::cout << "\n";
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
    res->setMask(mask, true);
    //MEMCHECK();
#if VERBOSE_DEBUG
    std::cout << std::flush << "IOComponent::getUpdates preparing to send " << res->size() << " d:";
    display(res->data(), process_data_size);
    std::cout << " m:";
    display(res->mask(), process_data_size);
    std::cout << "\n" << std::flush;
#endif
    //MEMCHECK();
    return res;
}

IOUpdate *IOComponent::getDefaults() {
    if (min_offset > max_offset) {
        return 0;
    }
    if (!io_process_data || !process_data_size || !default_data || !default_mask) {
        return 0;
    }
    IOUpdate *res = new IOUpdate;
    res->setSize(max_offset - min_offset + 1);
    copyMaskedBits(io_process_data, default_data, default_mask, process_data_size);
    res->setData(getProcessData());
    res->setMask(default_mask);

#if VERBOSE_DEBUG
    std::cout << "preparing to send defaults " << res->size() << " bytes\n";
    display(res->data(), res->size());
    std::cout << "\n";
    display(res->mask(), res->size());
    std::cout << "\n";
#endif
    return res;
}

void IOComponent::setupIOMap() {
    boost::recursive_mutex::scoped_lock lock(processing_queue_mutex);
    max_offset = 0;
    min_offset = 1000000L;
    io_map.clear();

    if (indexed_components) {
        delete indexed_components;
    }
    //std::cout << "\n\n setupIOMap\n";
    // find the highest and lowest offset location within the process data
    std::list<IOComponent *>::iterator iter = processing_queue.begin();
    while (iter != processing_queue.end()) {
        IOComponent *ioc = *iter++;
        //std::cout << ioc->getName() << " " << ioc->address << "\n";
        unsigned int offset = ioc->address.io_offset;
        unsigned int bitpos = ioc->address.io_bitpos;
        offset += bitpos / 8;

        if (offset < min_offset) {
            min_offset = offset;
        }
        //bitpos = bitpos % 8;
        if (ioc->address.bitlen >= 8) {
            offset += ioc->address.bitlen / 8 - 1;
        }
        if (offset > max_offset) {
            max_offset = offset;
        }
    }
    // If entries are not mapped yet, keep a minimal process map so startup can proceed.
    if (max_offset >= 10000 || processing_queue.empty()) {
        if (max_offset >= 10000) {
            std::cerr << "setupIOMap: unregistered domain offsets (max would be " << max_offset
                      << "); using empty process map until entries are mapped\n";
        }
        max_offset = 0;
        min_offset = 0;
    }
    if (min_offset > max_offset) {
        min_offset = max_offset;
    }
    std::cout << std::dec << "min io offset: " << min_offset << "\n"
              << "max io offset: " << max_offset << "\n"
              << ((max_offset + 1) * sizeof(IOComponent *)) << " bytes reserved for index io\n";

    indexed_components = new std::vector<IOComponent *>((max_offset + 1) * 8);

    if (io_process_data) {
        delete[] io_process_data;
    }
    process_data_size = max_offset + 1;
    io_process_data = new uint8_t[process_data_size];
    memset(io_process_data, 0, process_data_size);

    io_map.resize(max_offset + 1);
    for (unsigned int i = 0; i <= max_offset; ++i) {
        io_map[i] = 0;
    }
    iter = processing_queue.begin();
    while (iter != processing_queue.end()) {
        IOComponent *ioc = *iter++;
        if (ioc->io_name == "") {
            continue;
        }
        unsigned int offset = ioc->address.io_offset;
        unsigned int bitpos = ioc->address.io_bitpos;
        offset += bitpos / 8;
        if (offset > max_offset || offset * 8 + bitpos >= indexed_components->size()) {
            continue;
        }
        int bytes = 1;
        for (unsigned int i = 0; i < ioc->address.bitlen; ++i) {
            size_t idx = offset * 8 + bitpos + i;
            if (idx >= indexed_components->size()) {
                break;
            }
            (*indexed_components)[idx] = ioc;
        }
        for (int i = 0; i < bytes; ++i) {
            if (offset + i > max_offset) {
                break;
            }
            std::list<IOComponent *> *cl = io_map[offset + i];
            if (!cl) {
                cl = new std::list<IOComponent *>();
            }
            cl->push_back(ioc);
            io_map[offset + i] = cl;
            //std::cout << "offset: " << (offset + i) << " io name: " << ioc->io_name << "\n";
        }
    }
    if (io_process_mask) { delete io_process_mask; io_process_mask = nullptr; }
    io_process_mask = generateProcessMask(io_process_mask, process_data_size);
}

void IOComponent::markChange() {
    assert(io_process_data);
    if (!update_data) {
        getUpdateData();
    }
    uint8_t *offset = update_data + address.io_offset;
    int bitpos = address.io_bitpos;
    offset += (bitpos / 8);
    bitpos = bitpos % 8;

    if (address.bitlen == 1) {
        int64_t value = (*offset & (1 << bitpos)) ? 1 : 0;

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
        else if (value && last_event == e_off) {
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

void IOComponent::handleChange(std::list<Package *> &work_queue) {
    assert(io_process_data);
    uint8_t *offset = io_process_data + address.io_offset;
    int bitpos = address.io_bitpos;
    offset += (bitpos / 8);
    bitpos = bitpos % 8;

    if (address.bitlen == 1) {
        int64_t value = (*offset & (1 << bitpos)) ? 1 : 0;

        // Outputs: until the process image reflects our command, do not clobber
        // address.value with 0 from an unarmed/stale snapshot (SetStateAction /
        // PWM would flap off immediately).
        if (direction() == DirOutput &&
            (last_event == e_on || last_event == e_off)) {
            if ((last_event == e_on && value == 1) ||
                (last_event == e_off && value == 0)) {
                last_event = e_none;
            }
            else {
                return;
            }
        }

        const char *evt;
        if (address.value != value) { // TBD is this test necessary?
            // POINT/STATUS_FLAG: stamp IOTIME only on real bit changes (not every poll).
            if (direction() != DirOutput) {
                publishSampleTime(read_time ? read_time : io_clock);
            }
            std::list<MachineInstance *>::iterator iter;
#ifndef DISABLE_LEAVE_FUNCTIONS
            if (address.value) {
                evt = "on_leave";
            }
            else {
                evt = "off_leave";
            }
            std::list<MachineInstance *>::iterator owner_iter = owners.begin();
            while (owner_iter != owners.end()) {
                ProcessingThread::activate(*owner_iter);
                Message m(evt, Message::LEAVEMSG);
                work_queue.push_back(new Package(this, *owner_iter++, m));
            }
#endif
            if (value) {
                evt = "on_enter";
            }
            else {
                evt = "off_enter";
            }
            owner_iter = owners.begin();
            while (owner_iter != owners.end()) {
                Message msg(evt, Message::ENTERMSG);
                work_queue.push_back(new Package(this, *owner_iter++, msg));
            }
        }
        address.value = value;
    }
    else {
        //std::cout << io_name << " object of size " << address.bitlen << " val: ";
        //display(offset, address.bitlen/8);
        //std::cout << " bit pos: " << bitpos << " ";
        int64_t val = 0;
        if (address.bitlen < 8) {
            uint8_t bitmask = 0x8 >> bitpos;
            val = 0;
            for (unsigned int count = 0; count < address.bitlen; ++count) {
                val = val + (*offset & bitmask);
                bitmask = bitmask >> 1;
                // check for objects traversing byte boundaries
                if (count < address.bitlen && !bitmask) {
                    bitmask = 0x80;
                    ++offset;
                }
            }

            while (bitmask) {
                val = val >> 1;
                bitmask = bitmask >> 1;
            }
            //std::cout << " value: " << val << "\n";
        }
        else if (address.bitlen == 8) {
            val = *(int8_t *)(offset);
        }
        else if (address.bitlen == 16) {
            val = fromU16(offset);
            //std::cout << " 16bit value: " << val << " " << std::hex << val << std::dec << "\n";
        }
        else if (address.bitlen == 32) {
            val = fromU32(offset);
        }
        else if (address.bitlen == 64) {
            val = fromU64(offset);
        }
        else {
            std::cout << " unusual bitlen: " << address.bitlen << "\n";
            val = 0;
            uint32_t bitlen = address.bitlen;
            while (bitlen > 8) {
                ++offset;
                bitlen -= 8;
            }
            bitpos += bitlen;
            if (bitpos > 8) {
                ++offset;
                bitpos -= 8;
            }
        }
        // UDINT wire bits as int32 when owner (signed:1): wrap past 0 → −1, −2, …
        val = signExtendWireValue(val, address.bitlen, address.is_signed);
        if (regular_polls.count(this)) {
            // Polled multi-bit: always absorb wire value and run filter so machine
            // VALUE/ENG/IOTIME track (ANALOGINPUT/COUNTER/DIGITALVALUE). Skipping
            // filter left Error.VALUE=0 while address.value already held 0x76.
            raw_value = val;
            address.value = filter(val);
        }
        else if (hardware_state == s_hardware_init ||
                 (hardware_state == s_operational && raw_value != val)) {
            //std::cerr << "raw io value changed from " << raw_value << " to " << val << "\n";
            raw_value = val;
            int64_t new_val = filter(val);
            // Always publish address.value (including s_hardware_init). If we only
            // set raw_value during init and the first operational sample matches,
            // multi-bit DIGITALVALUE stayed at 0 forever (filter/setValue may also
            // have run before owners were linked).
            address.value = new_val;
        }
    }
}

void IOComponent::turnOn() {}
void IOComponent::turnOff() {}

void Output::turnOn() {
    last = microsecs();
    // Commanded value is authoritative for SetStateAction completion.
    address.value = 1;
    pending_value = 1; // processAll clears when updates_sent && pending==value
    // Kernel path writes the output shadow immediately (no process-image echo).
    // Do NOT leave this in updatedComponentsOut / outputs_waiting — nothing
    // clears those without an input-domain change, so updatesWaiting() stayed
    // true forever and forced the processing loop to ~1/POLLING_DELAY forever.
    last_event = e_none;
#ifndef EC_SIMULATOR
    ECInterface::instance()->applyKernelOutputBit(address.io_offset, address.io_bitpos, true);
#endif
}

void Output::turnOff() {
    last = microsecs();
    address.value = 0;
    pending_value = 0; // processAll clears when updates_sent && pending==value
    last_event = e_none; // see turnOn — getStateString must become "off"
#ifndef EC_SIMULATOR
    ECInterface::instance()->applyKernelOutputBit(address.io_offset, address.io_bitpos, false);
#endif
}

void IOComponent::setValue(uint32_t new_value) {
    assert(!address.is_signed);
    pending_value = new_value;
    last = microsecs();
    address.value = new_value;
    // Never publish inputs into the kernel output shadow (zeros TxPDO in merge).
    if (direction_ == DirInput) {
        last_event = e_none;
        return;
    }
    last_event = e_none;
#ifndef EC_SIMULATOR
    ECInterface::instance()->applyKernelOutputValue(address.io_offset, address.io_bitpos,
                                                    address.bitlen, new_value);
#endif
}

void IOComponent::setValue(int32_t new_value) {
    assert(address.is_signed);
    pending_value = new_value;
    last = microsecs();
    address.value = new_value;
    if (direction_ == DirInput) {
        last_event = e_none;
        return;
    }
    last_event = e_none;
#ifndef EC_SIMULATOR
    ECInterface::instance()->applyKernelOutputValue(address.io_offset, address.io_bitpos,
                                                    address.bitlen, (uint32_t)new_value);
#endif
}

void IOComponent::setValue(uint64_t new_value) {
    assert(!address.is_signed);
    pending_value = new_value;
    last = microsecs();
    address.value = static_cast<int64_t>(new_value);
    if (direction_ == DirInput) {
        last_event = e_none;
        return;
    }
    last_event = e_none;
#ifndef EC_SIMULATOR
    ECInterface::instance()->applyKernelOutputValue(address.io_offset, address.io_bitpos,
                                                    address.bitlen, (uint32_t)(new_value & 0xffffffffu));
#endif
}

void IOComponent::setValue(int64_t new_value) {
    assert(address.is_signed);
    pending_value = new_value;
    last = microsecs();
    address.value = new_value;
    if (direction_ == DirInput) {
        last_event = e_none;
        return;
    }
    last_event = e_none;
#ifndef EC_SIMULATOR
    ECInterface::instance()->applyKernelOutputValue(address.io_offset, address.io_bitpos,
                                                    address.bitlen,
                                                    (uint32_t)(static_cast<uint64_t>(new_value) &
                                                               0xffffffffu));
#endif
}

bool IOComponent::isOn() { return last_event == e_none && address.value != 0; }

bool IOComponent::isOff() { return last_event == e_none && address.value == 0; }
