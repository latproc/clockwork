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

#include "ECInterface.h"
#include "DebugExtra.h"
#include "MachineClass.h"
#include "MachineInstance.h"
#include "MessageLog.h"
#include "SetStateAction.h"
#include "Statistic.h"
#include "Statistics.h"
#include "cJSON.h"
#include "tl/expected.hpp"
#include <boost/thread/condition.hpp>
#include <cstddef>
#include <errno.h>
#include <fstream>
#include <iomanip>
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <vector>
#include <list>
#include <limits>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#ifndef EC_SIMULATOR
#include "SDOEntry.h"
#include "symboltable.h"
#include <ecrt.h>
#include "process_data.h"
#ifdef USE_KERNEL_ETHERCAT
#include "KernelEthercatBus.h"
#include "ElcConfigFile.h"
#include "options.h"
#include "IOComponent.h"
#endif
#endif

#define VERBOSE_DEBUG 0
#if VERBOSE_DEBUG
static void display(uint8_t *p, size_t n);
#endif

extern Statistics *statistics;
void signal_handler(int signum);

unsigned int ECInterface::FREQUENCY = 2000;
unsigned long ECInterface::activated_cycle_period_us_ = 0;

#ifdef USE_KERNEL_ETHERCAT
// Commanded process image for outputs. receiveState() overwrites domain1_pd with
// the input snapshot; without this shadow, turnOn bits are wiped before publish.
static std::vector<uint8_t> g_kernel_output_image;
static std::vector<uint8_t> g_kernel_output_mask;
// Publish only when the shadow changes (or until first successful arm).
static bool g_kernel_output_dirty = true;
static bool g_kernel_outputs_armed = false;
static std::vector<uint8_t> g_kernel_pub_mask; // cached full-domain publish mask
static bool g_kernel_pub_mask_valid = false;
// CAP_OUTPUT_LEASE: 0.18 uses timeout_ms + publish/arm refill; no renew loop.
static bool g_output_lease_enabled = false;
static uint32_t g_output_lease_timeout_ms = 0;
static bool g_output_lease_publish_renew = false;
static void enableKernelOutputLeaseAfterActivate();
// Multi-domain isolation (API 0.12/0.17): each domain_config_id is a WC /
// validity / arm boundary. N domains → N ECDomain_<id> machines on L_ECDomains.
// First domain declared in topology is *primary* (ETHERCAT_WC / all_ok /
// slave_states). Plant LPC pulls a REFERENCE by domain_id (DOMAINREF).
struct ElcDomainSlot {
    uint32_t id = 0;
    bool active = false;
    bool valid = false;
    bool armed = false;
    bool rearm_required = false;
    // False until the first successful getDomainStatus after (re)activate.
    // Lifecycle / ESTALE must not publish dual INVALID + size=0 as a bus fault.
    bool status_known = false;
    uint32_t wc = 0;
    uint8_t wc_state = 0;
    uint32_t faults = 0;
    uint32_t slave_states = 0;
    uint32_t base_offset = 0;
    uint32_t domain_size = 0;
    // Last CW state name we applied (for edge log + change detection).
    const char *published_state = "INVALID";
    MachineInstance *machine = nullptr;
};
static std::vector<ElcDomainSlot> g_domains;
static bool g_domain_status_ok = false; // primary domain status available
static uint32_t g_primary_domain_id = 0;
// True when every configured domain is WC-complete + data_valid (for poll rate).
static bool g_all_domains_complete = false;
// Cycle is active but at least one domain has no successful status yet.
static bool g_domain_status_pending = false;

// Map elc WC/data_valid to CW ETHERCAT_DOMAIN states.
// Domain bus firewall: WC completeness is the isolation boundary — do not
// report COMPLETE unless wc_state is complete *and* data_valid.
//
// Returns nullptr when the slot has no bus sample yet (post-activate /
// ESTALE): caller must hold the previous CW state (not force INVALID).
static const char *domainSlotCwState(const ElcDomainSlot &slot) {
    // EC_WC_*: ZERO=0, INCOMPLETE=1, COMPLETE=2 (EtherLab / elc UAPI).
    if (!slot.status_known) {
        return nullptr; // lifecycle hold — not a bus fault
    }
    // Kernel reports inactive only when the cycle is not running for this
    // controller context. Prefer INCOMPLETE over INVALID while ECInterface
    // still has active==true so dual INVALID is reserved for true session-down.
    if (!slot.active) {
        return "INCOMPLETE";
    }
    if (slot.wc_state == 2 && slot.valid) {
        return "COMPLETE";
    }
    // Live or failed segment: WC incomplete, faults, or !data_valid.
    return "INCOMPLETE";
}

static MachineInstance *ensureECDomainMachine(uint32_t domain_id) {
    const std::string name = "ECDomain_" + std::to_string(domain_id);
    MachineInstance *mi = MachineInstance::find(name.c_str());
    if (mi) {
        // Status mirrors must be active so setState is reliable (not PASSIVE).
        if (!mi->isActive()) {
            mi->markActive();
        }
        return mi;
    }
    mi = MachineInstanceFactory::create(name.c_str(), "ETHERCAT_DOMAIN");
    if (!mi) {
        std::cerr << "Failed to create " << name << "\n";
        return nullptr;
    }
    MachineClass *cls = MachineClass::find("ETHERCAT_DOMAIN");
    if (cls) {
        mi->setProperties(cls->getProperties());
        mi->setStateMachine(cls);
    }
    mi->setDefinitionLocation("Internal", 0);
    mi->setValue("domain_id", Value{static_cast<long>(domain_id)});
    mi->markActive();
    machines[name] = mi;
    MachineInstance *list = MachineInstance::find("L_ECDomains");
    if (list) {
        list->addParameter(Value(name.c_str(), Value::t_symbol), mi);
        if (!list->enabled()) {
            list->enable();
        }
        if (!mi->enabled()) {
            mi->enable();
        }
    }
    std::cerr << "Registered " << name << " on L_ECDomains (active status mirror)\n";
    return mi;
}

void elcRegisterClockworkDomains(const std::vector<uint32_t> &domain_ids) {
    g_domains.clear();
    g_domain_status_ok = false;
    g_all_domains_complete = false;
    g_primary_domain_id = domain_ids.empty() ? 0 : domain_ids.front();
    for (uint32_t id : domain_ids) {
        ElcDomainSlot slot;
        slot.id = id;
        slot.machine = ensureECDomainMachine(id);
        g_domains.push_back(slot);
    }
    MachineInstance *ec = MachineInstance::find("ETHERCAT");
    if (ec) {
        ec->setValue("primary_domain_id", Value{static_cast<long>(g_primary_domain_id)});
        ec->setValue("domain_count", Value{static_cast<long>(g_domains.size())});
    }
}

// Status mirrors (ECDomain_*, ETHERCAT_WC): queue SetState when changed.
// ECDomain machines are markActive() so they are not PASSIVE and the action
// runs on the processing thread (passive machines dropped status updates).
static void setMachineStateIfChanged(MachineInstance *m, const char *state) {
    if (!m || !state) {
        return;
    }
    if (!m->enabled()) {
        m->enable();
    }
    // Keep the machine active so idle() processes the SetStateAction.
    if (!m->isActive()) {
        m->markActive();
    }
    if (m->getCurrent().getName() == state) {
        return;
    }
    // Drop any unfinished SetState so we do not queue COMPLETE→INCOMPLETE→…
    // behind a backlog of stale transitions when domains flap.
    if (m->executingCommand()) {
        m->clearAllActions();
    }
    SetStateActionTemplate ssat = SetStateActionTemplate("SELF", Value{state});
    SetStateAction *ssa = dynamic_cast<SetStateAction *>(ssat.factory(m));
    if (ssa) {
        m->enqueueAction(ssa);
    }
}

// Push status into each ECDomain_<id>, ETHERCAT (bus), ETHERCAT_WC (primary).
static void publishKernelEthercatClockworkMachines() {
    MachineInstance *ec = MachineInstance::find("ETHERCAT");
    MachineInstance *wc = MachineInstance::find("ETHERCAT_WC");

    bool all_complete = !g_domains.empty();
    bool any_known = false;
    bool any_pending = false;
    for (ElcDomainSlot &slot : g_domains) {
        MachineInstance *dm = slot.machine;
        const char *dstate = domainSlotCwState(slot);
        if (!slot.status_known) {
            any_pending = true;
            all_complete = false;
        }
        else {
            any_known = true;
            if (!(slot.valid && slot.wc_state == 2 && slot.active)) {
                all_complete = false;
            }
        }
        if (!dm) {
            if (dstate) {
                slot.published_state = dstate;
            }
            continue;
        }
        dm->setValue("domain_id", Value{static_cast<long>(slot.id)});
        // status_known=0 → lifecycle hold (not "bus failed"). Keep last size/offset.
        dm->setValue("status_known", Value{slot.status_known ? 1 : 0});
        if (slot.status_known) {
            dm->setValue("valid", Value{slot.valid ? 1 : 0});
            dm->setValue("armed", Value{slot.armed ? 1 : 0});
            dm->setValue("rearm", Value{slot.rearm_required ? 1 : 0});
            dm->setValue("wc", Value{static_cast<long>(slot.wc)});
            dm->setValue("wc_state", Value{static_cast<long>(slot.wc_state)});
            dm->setValue("faults", Value{static_cast<long>(slot.faults)});
            dm->setValue("slave_states", Value{static_cast<long>(slot.slave_states)});
            if (slot.domain_size != 0) {
                dm->setValue("base_offset", Value{static_cast<long>(slot.base_offset)});
                dm->setValue("domain_size", Value{static_cast<long>(slot.domain_size)});
            }
        }
        if (!dstate) {
            // Lifecycle: hold last CW state; do not COMPLETE→INVALID→INCOMPLETE.
            continue;
        }
        if (slot.published_state != dstate) {
            std::cerr << "ECDomain_" << slot.id << " " << slot.published_state << " -> "
                      << dstate << " valid=" << (int)slot.valid
                      << " wc=" << slot.wc << " wc_state=" << (unsigned)slot.wc_state
                      << " faults=0x" << std::hex << slot.faults << std::dec
                      << " armed=" << (int)slot.armed
                      << " rearm=" << (int)slot.rearm_required
                      << " slave_al=0x" << std::hex << slot.slave_states << std::dec
                      << " known=1\n";
            slot.published_state = dstate;
        }
        setMachineStateIfChanged(dm, dstate);
    }
    g_domain_status_pending = any_pending;
    g_all_domains_complete = all_complete && any_known && !any_pending;

    if (ec) {
        ec->setValue("primary_domain_id", Value{static_cast<long>(g_primary_domain_id)});
        ec->setValue("domain_count", Value{static_cast<long>(g_domains.size())});
        ec->setValue("domain_status_pending", Value{any_pending ? 1 : 0});
        if (g_domain_status_ok) {
            ec->setValue("all_ok_source", Value("primary_domain", Value::t_string));
        }
        else {
            ec->setValue("all_ok_source", Value("aggregate", Value::t_string));
        }
    }

    if (!wc) {
        return;
    }
    // ETHERCAT_WC follows the primary domain (first declared) WC only.
    // Hold last VALUE/state while primary status is not yet known (lifecycle).
    if (g_domain_status_ok && !g_domains.empty() && g_domains.front().status_known) {
        const ElcDomainSlot &p = g_domains.front();
        const char *state = "INCOMPLETE";
        long value = static_cast<long>(p.wc);
        if (p.wc_state == 2 && p.valid) {
            state = "COMPLETE";
        }
        else if (p.wc_state == 0 && p.wc == 0 && !p.active && p.faults == 0) {
            state = "ZERO";
        }
        wc->setValue("VALUE", Value{value});
        setMachineStateIfChanged(wc, state);
    }
}
#endif
ec_master_t *ECInterface::master = NULL;
ec_master_state_t ECInterface::master_state = {};
uint64_t ECInterface::master_state_changed = 0;
uint64_t ECInterface::master_last_checked = 0;

bool ECInterface::active = false;

ec_domain_t *ECInterface::domain1 = NULL;
ec_domain_state_t ECInterface::domain1_state = {};
uint8_t *ECInterface::domain1_pd = 0;

static unsigned int expected_slaves = 0;
bool all_ok = false;
static bool link_was_up = false;
static bool master_was_running = false;

static uint64_t last_receive = 0;
static uint64_t last_update = 0;

long ECInterface::default_tolerance = 1;
#ifndef EC_SIMULATOR

static boost::recursive_mutex modules_mutex;
std::vector<ECModule *> ECInterface::modules;

#ifdef USE_KERNEL_ETHERCAT


KernelEthercatBus* ECInterface::getKernelBus() { return kernelBus.get(); }

bool ECInterface::initialiseKernelTransport() {
    if (!kernelBus) {
        kernelBus.reset(new KernelEthercatBus());
    }
    int ret = kernelBus->open();
    if (ret != 0) {
        DBG_MSG << "Failed to open kernel EtherCAT transport: " << ret << "\n";
        return false;
    }
    initialised = true;
    DBG_ETHERCAT << "KernelEthercatBus opened successfully for discovery and SDO ()\n";
    return true;
}
#endif

static int slaves_not_operational = 1; // initialise to nonzero until we know for sure
static int slaves_offline = 1;

#ifdef USE_SDO
static std::list<SDOEntry *> prepared_sdo_entries;
static std::list<SDOEntry *> new_sdo_entries;
#endif //USE_SDO

#if KEEP_STATS
bool keep_stats = true;
#else
bool keep_stats = false;
#endif
Statistic recv_to_update("Receive to update");
Statistic update_to_recv("Update to receive");

namespace {

    std::string compare_modules(const ECModule & a, const ECModule &b) {
        std::stringstream ss;
        ss << "a.alias: " << a.alias << " b.alias: " << b.alias << "\n";
        ss << "a.position: " << a.position << " b.position: " << b.position << "\n";
        ss << "a.revision_no: " << a.revision_no << " b.revision_no: " << b.revision_no << "\n";
        ss << "a.sync_count: " << a.sync_count << " b.sync_count: " << b.sync_count << "\n";
        ss << "a.num_entries: " << a.num_entries << " b.num_entries: " << b.num_entries << "\n";
        return ss.str();
    }

} // namespace

ECModule::ECModule() : pdo_entries(0), pdos(0), syncs(0), num_entries(0), entry_details(0) {
    offsets = new unsigned int[64];
    bit_positions = new unsigned int[64];
    slave_config = 0;
    memset(&slave_config_state, 0, sizeof(ec_slave_config_state_t));
    alias = 0;
    position = 0;
    vendor_id = 0;
    product_code = 0;
    revision_no = 0;
    elc_config_id = 0;
    elc_domain_id = 0;
    sdo_seen_online = false;
    sync_count = 0;
}

ECModule::~ECModule() {
    if (pdo_entries) {
        delete[] pdo_entries;
        pdo_entries = 0;
    }
    if (pdos) {
        delete[] pdos;
        pdos = 0;
    }
    else if (syncs && sync_count) {
        // pdos were not allocated in a block so they must be allocated per sync manager
        for (unsigned int i = 0; i < sync_count; ++i) {
            delete[] syncs[i].pdos;
        }
    }
    if (syncs) {
        delete[] syncs;
        syncs = 0;
    }
    if (entry_details && num_entries) {
        delete[] entry_details;
    }
    entry_details = 0;
    delete[] bit_positions;
    delete[] offsets;
}

bool ECModule::online() { return slave_config_state.online; }

bool ECModule::operational() { return slave_config_state.operational; }

int ECModule::state() { return slave_config_state.al_state; }

void ECModule::link_to_machine(MachineInstance *m) {
    machine_instance = m;
}
MachineInstance *ECModule::machine() {
    return machine_instance;
}
void ECModule::update() {
}

#ifdef USE_SDO
SDOEntry::SDOEntry(std::string nam, uint16_t index, uint8_t subindex, const uint8_t *data,
                   size_t size, uint8_t offset)
    : name(nam), module_(0), index_(index), subindex_(subindex), offset_(offset), data_(0),
      size_(size), realtime_request(0), sync_done(false), error_count(0), op(READ),
      machine_instance(0), next_poll_time(0), read_pending(true) {
    if (data && size != 0) {
        data_ = new uint8_t[size];
        assert(data_);
        memcpy(data_, data, size);
    }
    new_sdo_entries.push_back(this);
}

#if 0
SDOEntry::SDOEntry(std::string nam, ec_sdo_request_t *sdo_req)
    : name(nam), module_(0), index_(0), subindex_(0), offset_(0), data_(0), size_(0),
      realtime_request(sdo_req), sync_done(false), error_count(0)
{
    new_sdo_entries.push_back(this);
}
#endif

SDOEntry::~SDOEntry() {
    if (data_) {
        delete[] data_;
        data_ = 0;
    }
}

ec_sdo_request_t *SDOEntry::getRequest() { return realtime_request; }

void SDOEntry::setData(bool val) {
    DBG_ETHERCAT_CALLS << "ecrt_sdo_request_data\n";
    uint8_t *data = ecrt_sdo_request_data(realtime_request);
    if (data) {
        EC_WRITE_BIT(data, offset_, ((val) ? 1 : 0));
    }
}

void SDOEntry::setData(uint8_t val) {
    DBG_ETHERCAT_CALLS << "ecrt_sdo_request_data\n";
    EC_WRITE_U8(ecrt_sdo_request_data(realtime_request), val);
}

void SDOEntry::setData(int8_t val) {
    DBG_ETHERCAT_CALLS << "ecrt_sdo_request_data\n";
    EC_WRITE_S8(ecrt_sdo_request_data(realtime_request), val);
}

void SDOEntry::setData(uint16_t val) {
    DBG_ETHERCAT_CALLS << "ecrt_sdo_request_data\n";
    EC_WRITE_U16(ecrt_sdo_request_data(realtime_request), val);
}

void SDOEntry::setData(int16_t val) {
    DBG_ETHERCAT_CALLS << "ecrt_sdo_request_data\n";
    EC_WRITE_S16(ecrt_sdo_request_data(realtime_request), val);
}

void SDOEntry::setData(uint32_t val) {
    DBG_ETHERCAT_CALLS << "ecrt_sdo_request_data\n";
    EC_WRITE_U32(ecrt_sdo_request_data(realtime_request), val);
}

void SDOEntry::setData(int32_t val) {
    DBG_ETHERCAT_CALLS << "ecrt_sdo_request_data\n";
    EC_WRITE_S32(ecrt_sdo_request_data(realtime_request), val);
}

ECModule *SDOEntry::getModule() { return module_; }

void SDOEntry::setModule(ECModule *m) { module_ = m; }

void SDOEntry::failure() { ++error_count; }
void SDOEntry::success() {
    error_count = 0;
    sync_done = true;
    if (op == WRITE) {
        op = READ; // value has been successfully written, switch back to reading
    }
}

bool SDOEntry::ok() { return error_count == 0; }

uint32_t SDOEntry::pollIntervalMs() const {
    if (machine_instance && machine_instance->properties.exists("poll_interval")) {
        int64_t interval = machine_instance->properties.lookup("poll_interval").iValue;
        return interval > 0 ? static_cast<uint32_t>(interval) : 0;
    }
    return 0;
}

bool SDOEntry::pollDue(uint64_t now) const {
    return (read_pending || pollIntervalMs() != 0) && now >= next_poll_time;
}

void SDOEntry::schedulePoll(uint64_t now, uint32_t delay_ms) {
    read_pending = false;
    uint32_t interval = delay_ms ? delay_ms : pollIntervalMs();
    next_poll_time = interval ? now + static_cast<uint64_t>(interval) * 1000
                              : std::numeric_limits<uint64_t>::max();
}

void SDOEntry::requestRead(uint64_t now, uint32_t delay_ms) {
    read_pending = true;
    next_poll_time = now + static_cast<uint64_t>(delay_ms) * 1000;
}

void SDOEntry::markNeedsRecommission(uint64_t now_us, uint32_t settle_ms) {
    // Forget the prior sync so the next successful upload is treated as a
    // first_read and re-queues any explicit `default` write.
    sync_done = false;
    error_count = 0;
    op = READ;
    requestRead(now_us, settle_ms);
}

void SDOEntry::recommissionModule(ECModule *module, uint64_t now_us) {
    if (!module) {
        return;
    }
    // Spread mailbox work across slaves (and entries) so a multi-drive return
    // does not freeze the ecat thread in setup_apply for hundreds of ms.
    uint32_t stagger_ms = 200 + static_cast<uint32_t>(module->position) * 30u;
    for (SDOEntry *entry : prepared_sdo_entries) {
        if (!entry || entry->getModule() != module) {
            continue;
        }
        DBG_ETHERCAT_SDO << "Recommission SDO " << entry->getName()
                         << " on module " << module->getName()
                         << " in " << stagger_ms << " ms\n";
        entry->markNeedsRecommission(now_us, stagger_ms);
        stagger_ms += 15;
    }
}

SDOEntry *SDOEntry::find(std::string name) {
    std::list<SDOEntry *>::iterator iter = prepared_sdo_entries.begin();
    while (iter != prepared_sdo_entries.end()) {
        SDOEntry *entry = *iter++;
        if (name == entry->getName()) {
            return entry;
        }
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
            DBG_ETHERCAT << "Module already linked to SDO entry "
                    << entry->getName() << "- replacing old entry\n";
            iter = new_sdo_entries.erase(iter);
            continue;
        }
        DBG_ETHERCAT << "Attempting to prepare SDO entry: " << entry->getName() << "\n";
        MachineInstance *mi = MachineInstance::find(entry->getModuleName().c_str());
        if (mi) {
            int module_position = mi->properties.lookup("position").iValue;
            ECModule *module = 0;
            if (module_position >= 0) {
                module = ECInterface::findModule(module_position);
            }
            else {
                DBG_ETHERCAT << "Invalid module " << mi->getName()
                             << " bad position value: " << module_position << "aborting\n"
                             << std::flush;
                exit(1);
            }
            if (module && entry->prepareRequest(module)) {
                DBG_ETHERCAT << "Prepared SDO entry: " << entry->getName() << "\n";
                iter = new_sdo_entries.erase(iter);
                // Every SDO is read first. An explicit default is queued only
                // after that upload succeeds, so VALUE briefly reflects the
                // device value before the configured value is applied.
                ECInterface::instance()->queueRuntimeRequest(entry);
            }
            else {
                DBG_ETHERCAT << "Warning: failed to prepare SDO entry: " << entry->getName()
                             << "\n";
                iter++;
            }
        }
        else {
            iter++;
        }
    }
}
#endif //USE_SDO

#endif


ECInterface::ECInterface()
    : initialised(0), reference_time(0),
#ifdef USE_DC
      dc_application_time_ns(0), dc_cycle_adjustment_ns(0), dc_difference_total_ns(0),
      dc_delta_total_ns(0), dc_last_difference_ns(0), dc_filter_count(0),
      dc_monitor_countdown(0), dc_monitor_wait_cycles(0), dc_reference_valid(false),
      dc_monitor_pending(false), dc_last_reference_result(0),
#endif
#ifndef EC_SIMULATOR
#ifdef USE_SDO
      current_init_entry(initialisation_entries.begin()),
      current_update_entry(sdo_update_entries.begin()), sdo_entry_state(e_None), sdo_not_before(0),
#endif //USE_SDO
#endif
      ethercat_status(0), failure_tolerance(0), failure_count(0)
#ifdef USE_KERNEL_ETHERCAT
      , kernelBus(nullptr)
#endif
{
}

void ECInterface::setup(void *data) { instance()->init(); }

#ifndef EC_SIMULATOR

void ECInterface::setReferenceTime(uint32_t now) { reference_time = now; }

uint32_t ECInterface::getReferenceTime() { return reference_time; }

#ifdef USE_DC
uint64_t ECInterface::monotonicTimeNs() {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return static_cast<uint64_t>(now.tv_sec) * 1000000000ULL + now.tv_nsec;
}

void ECInterface::processDistributedClock() {
    uint32_t reference = 0;
    const int result = ecrt_master_reference_clock_time(master, &reference);
    if (result == 0) {
        reference_time = reference;
        // Deliberate 32-bit subtraction handles the reference clock rollover.
        int32_t difference = static_cast<int32_t>(
            static_cast<uint32_t>(dc_application_time_ns) - reference);
        const int64_t cycle_ns = 1000000000ULL / FREQUENCY;
        int64_t normalized = static_cast<int64_t>(difference) % cycle_ns;
        if (normalized > cycle_ns / 2) normalized -= cycle_ns;
        if (normalized < -cycle_ns / 2) normalized += cycle_ns;
        difference = static_cast<int32_t>(normalized);

        if (dc_reference_valid) {
            dc_difference_total_ns += difference;
            dc_delta_total_ns += static_cast<int64_t>(difference) - dc_last_difference_ns;
            if (++dc_filter_count >= 1024) {
                dc_cycle_adjustment_ns += dc_delta_total_ns / 1024;
                dc_cycle_adjustment_ns += (dc_difference_total_ns > 0) -
                                          (dc_difference_total_ns < 0);
                if (dc_cycle_adjustment_ns > 1000) dc_cycle_adjustment_ns = 1000;
                if (dc_cycle_adjustment_ns < -1000) dc_cycle_adjustment_ns = -1000;
                dc_difference_total_ns = 0;
                dc_delta_total_ns = 0;
                dc_filter_count = 0;
            }
        }
        dc_last_difference_ns = difference;
        dc_reference_valid = true;
        if (dc_last_reference_result != 0) {
            std::cerr << "EtherCAT DC reference clock time resumed\n";
        }
    }
    else {
        if (dc_last_reference_result != result) {
            std::cerr << "EtherCAT DC reference clock read failed: " << strerror(-result) << "\n";
        }
        dc_reference_valid = false;
    }
    dc_last_reference_result = result;

    if (dc_monitor_pending) {
        const uint32_t deviation = ecrt_master_sync_monitor_process(master);
        if (deviation != static_cast<uint32_t>(-1)) {
            std::cout << "EtherCAT DC: reference difference " << dc_last_difference_ns
                      << " ns, maximum slave deviation " << deviation
                      << " ns, cycle adjustment " << dc_cycle_adjustment_ns << " ns\n";
            dc_monitor_pending = false;
            dc_monitor_wait_cycles = 0;
        }
        else if (++dc_monitor_wait_cycles >= 10) {
            std::cerr << "EtherCAT DC synchrony monitor timed out\n";
            dc_monitor_pending = false;
            dc_monitor_wait_cycles = 0;
        }
    }
}

void ECInterface::queueDistributedClockSync() {
    const int64_t phase_step = (dc_last_difference_ns > 0) - (dc_last_difference_ns < 0);
    // A positive (application - reference) difference means application time
    // is ahead, so slow it by subtracting the positive correction.
    dc_application_time_ns += 1000000000ULL / FREQUENCY - dc_cycle_adjustment_ns - phase_step;
    ecrt_master_application_time(master, dc_application_time_ns);
    ecrt_master_sync_slave_clocks(master);

    if (!dc_monitor_pending && dc_monitor_countdown-- == 0) {
        if (ecrt_master_sync_monitor_queue(master) == 0) {
            dc_monitor_pending = true;
            dc_monitor_wait_cycles = 0;
        }
        dc_monitor_countdown = FREQUENCY;
    }
}
#endif

bool ECModule::ecrtMasterSlaveConfig(ec_master_t *master) {
    if (master) {
        DBG_ETHERCAT_CALLS << "ecrt_master_slave_config\n";
        slave_config = ecrt_master_slave_config(master, alias, position, vendor_id, product_code);
    }
    return slave_config != 0;
}

bool ECModule::ecrtSlaveConfigPdos() {
    DBG_ETHERCAT_CALLS << "ecrt_config_pdos\n";
    int res = ecrt_slave_config_pdos(slave_config, sync_count, syncs);
    if (res) {
        std::cerr << "Error: " << res << " attempting to configure slave '" << name << "'\n";
        assert(false);
    }
    return true;
}

std::ostream &ECModule::operator<<(std::ostream &out) const {
    out << "Slave " << name << ": " << alias << ", " << position << ", " << std::hex << vendor_id
        << ", " << product_code << std::dec << "\n";
    for (unsigned int i = 0; i < sync_count; ++i) {
        out << "  SM" << i << ": " << (int)syncs[i].index << ", " << syncs[i].dir << ", "
            << syncs[i].n_pdos << "\n";
        for (unsigned int j = 0; j < syncs[i].n_pdos; ++j) {
            out << " PDO" << j << ": " << std::hex << syncs[i].pdos[j].index << ", " << std::dec
                << syncs[i].pdos[j].n_entries << "\n";
            for (unsigned int k = 0; k < syncs[i].pdos[j].n_entries; ++k) {
                const ec_pdo_entry_info_t *e = syncs[i].pdos[j].entries + k;
                out << "       " << k << ", " << std::hex << e->index << std::dec << ", "
                    << (int)e->subindex << ", " << (int)e->bit_length << "\n";
            }
        }
    }
    return out;
}

std::ostream &operator<<(std::ostream &out, const ECModule &module) {
    return module.operator<<(out);
}

#ifdef USE_SDO
ec_sdo_request_t *SDOEntry::prepareRequest(ECModule *module) {
    assert(module);
    assert(ECInterface::active == false);
    module_ = module;
    prepared_sdo_entries.remove(this);
    DBG_ETHERCAT_CALLS << "ecrt_master_slave_config\n";
    ec_slave_config_t *x = ecrt_master_slave_config(ECInterface::master, 0, module->position,
                                                    module->vendor_id, module->product_code);
    assert(x);
    ec_slave_config_state_t s;
    DBG_ETHERCAT_CALLS << "ecrt_slave_config_state\n";
    ecrt_slave_config_state(x, &s);
    // the request field size must be big enough to hold the offset
    // the EtherLab interface only provides a byte-sized interface to SDO so we convert
    // our bit-sized fields before creating the sdo request
    size_t sz = ((size_ + offset_ - 1) / 8) + 1;

    DBG_ETHERCAT_SDO << "Creating SDO request " << module->getName() << " 0x" << std::hex << index_
                     << ":" << subindex_ << std::dec << " (" << sz << ")" << "\n";
    DBG_ETHERCAT_CALLS << "ecrt_slave_config_create_sdo_request\n";
    realtime_request = ecrt_slave_config_create_sdo_request(x, index_, subindex_, sz);
    if (realtime_request) {
        ecrt_sdo_request_timeout(realtime_request, 2000);
    }
    prepared_sdo_entries.push_back(this);
    return realtime_request;
}
#endif //USE_SDO

#if 0
SDOEntry *ECInterface::createSDORequest(std::string name, ECModule *module, uint16_t index, uint8_t subindex, size_t size)
{
    assert(module);
    assert(ECInterface::active == false);
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

#ifdef USE_SDO
void ECInterface::queueInitialisationRequest(SDOEntry *entry, Value val) {
    if (!entry) {
        return;
    }
    std::lock_guard<std::mutex> lock(pending_sdo_mutex);
    for (auto &pending : pending_sdo_writes) {
        if (pending.first == entry) {
            pending.second = val;
            return;
        }
    }
    pending_sdo_writes.push_back(std::make_pair(entry, val));
}

void ECInterface::queueRuntimeRequest(SDOEntry *entry) { sdo_update_entries.push_back(entry); }

void ECInterface::acceptPendingSDOWrites() {
    std::unique_lock<std::mutex> lock(pending_sdo_mutex, std::try_to_lock);
    if (!lock.owns_lock()) {
        return;
    }
    while (!pending_sdo_writes.empty()) {
        auto pending = pending_sdo_writes.front();
        pending_sdo_writes.pop_front();

        // Preserve an in-flight value. A later queued value for the same entry
        // is replaced, otherwise the newest value is appended.
        auto queued = initialisation_entries.begin();
        for (; queued != initialisation_entries.end(); ++queued) {
            if (sdo_entry_state == e_Busy_Initialisation && queued == current_init_entry) {
                continue;
            }
            if (queued->first == pending.first) {
                queued->second = pending.second;
                break;
            }
        }
        if (queued == initialisation_entries.end()) {
            initialisation_entries.push_back(pending);
        }
    }
}

void ECInterface::beginModulePreparation() {
    DBG_ETHERCAT << "beginning module preparation\n";
    acceptPendingSDOWrites();
    current_init_entry = initialisation_entries.begin();
    sdo_entry_state = e_None;
}

void readValue(ec_sdo_request_t *sdo, unsigned int size, int offset = 0) {
    DBG_ETHERCAT_CALLS << "ecrt_sdo_request_data\n";
    if (size == 32) {
        fprintf(stderr, "SDO value: 0x%08X\n", EC_READ_U32(ecrt_sdo_request_data(sdo)));
    }
    else if (size == 16) {
        fprintf(stderr, "SDO value: 0x%04X\n", EC_READ_U16(ecrt_sdo_request_data(sdo)));
    }
    else if (size == 8) {
        fprintf(stderr, "SDO value: 0x%02X\n", EC_READ_U8(ecrt_sdo_request_data(sdo)));
    }
    else if (size == 1) {
        fprintf(stderr, "SDO value: 0x%01X\n", EC_READ_BIT(ecrt_sdo_request_data(sdo), offset));
    }
}

void SDOEntry::syncValue() {
    DBG_ETHERCAT_CALLS << "ecrt_sdo_request_data\n";
    if (size_ == 32) {
        if (machine_instance) {
            machine_instance->setValue("VALUE",
                                       EC_READ_U32(ecrt_sdo_request_data(realtime_request)));
        }
    }
    else if (size_ == 16) {
        if (machine_instance) {
            machine_instance->setValue("VALUE",
                                       EC_READ_U16(ecrt_sdo_request_data(realtime_request)));
        }
    }
    else if (size_ == 8) {
        if (machine_instance) {
            machine_instance->setValue("VALUE",
                                       EC_READ_U8(ecrt_sdo_request_data(realtime_request)));
        }
    }
    else if (size_ == 1) {
        if (machine_instance) {
            machine_instance->setValue(
                "VALUE", EC_READ_BIT(ecrt_sdo_request_data(realtime_request), offset_));
        }
    }
}

Value SDOEntry::readValue() {
    DBG_ETHERCAT_CALLS << "ecrt_sdo_request_data\n";
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
        return EC_READ_BIT(ecrt_sdo_request_data(realtime_request), offset_);
    }
    return SymbolTable::Null;
}

#ifdef USE_KERNEL_ETHERCAT
// Map Clockwork bit-width to elc_sdo_type + byte length for mailbox I/O.
static bool kernelSdoTypeAndLen(size_t bit_size, uint8_t *type_out, uint16_t *len_out) {
    if (!type_out || !len_out) {
        return false;
    }
    switch (bit_size) {
    case 1:
    case 8:
        *type_out = ELC_SDO_U8;
        *len_out = 1;
        return true;
    case 16:
        *type_out = ELC_SDO_U16;
        *len_out = 2;
        return true;
    case 32:
        *type_out = ELC_SDO_U32;
        *len_out = 4;
        return true;
    default:
        return false;
    }
}

static void kernelSdoPackValue(size_t bit_size, int64_t value, uint8_t *data, uint16_t len) {
    memset(data, 0, len);
    if (bit_size <= 8) {
        data[0] = static_cast<uint8_t>(value & 0xff);
    }
    else if (bit_size == 16) {
        uint16_t v = static_cast<uint16_t>(value & 0xffff);
        memcpy(data, &v, 2);
    }
    else if (bit_size == 32) {
        uint32_t v = static_cast<uint32_t>(value & 0xffffffffu);
        memcpy(data, &v, 4);
    }
}

// Synchronous CoE upload into the entry request buffer. Returns true on success.
static bool kernelSdoUploadEntry(SDOEntry *entry) {
    if (!entry || !entry->getModule() || !ECInterface::instance()->getKernelBus() ||
        !ECInterface::instance()->getKernelBus()->isOpen()) {
        return false;
    }
    uint8_t type = 0;
    uint16_t len = 0;
    if (!kernelSdoTypeAndLen(entry->getSize(), &type, &len)) {
        return false;
    }
    struct elc_sdo_upload req = {};
    req.struct_size = sizeof(req);
    req.api_major = ELC_API_VERSION_MAJOR;
    req.position = entry->getModule()->position;
    req.index = entry->getIndex();
    req.subindex = entry->getSubindex();
    req.requested_len = len;
    int ret = ECInterface::instance()->getKernelBus()->sdoUpload(&req);
    if (ret != 0 || req.result != 0) {
        DBG_ETHERCAT_SDO << "kernel SDO upload failed " << entry->getName() << " 0x" << std::hex
                         << entry->getIndex() << ":" << (int)entry->getSubindex() << std::dec
                         << " ret=" << ret << " result=" << req.result
                         << " abort=0x" << std::hex << req.abort_code << std::dec << "\n";
        return false;
    }
    uint8_t *dst = ecrt_sdo_request_data(entry->getRequest());
    if (!dst) {
        return false;
    }
    memset(dst, 0, 8);
    size_t copy = req.result_len < len ? req.result_len : len;
    memcpy(dst, req.data, copy);
    return true;
}

// Synchronous CoE download of curr value. Returns true on success.
static bool kernelSdoDownloadEntry(SDOEntry *entry, const Value &val) {
    if (!entry || !entry->getModule() || !ECInterface::instance()->getKernelBus() ||
        !ECInterface::instance()->getKernelBus()->isOpen()) {
        return false;
    }
    uint8_t type = 0;
    uint16_t len = 0;
    if (!kernelSdoTypeAndLen(entry->getSize(), &type, &len)) {
        return false;
    }
    uint8_t data[8] = {};
    kernelSdoPackValue(entry->getSize(), val.iValue, data, len);
    int ret = ECInterface::instance()->getKernelBus()->sdoDownload(
        entry->getModule()->position, entry->getIndex(), entry->getSubindex(), type, data, len);
    if (ret != 0) {
        DBG_ETHERCAT_SDO << "kernel SDO download failed " << entry->getName() << " 0x" << std::hex
                         << entry->getIndex() << ":" << (int)entry->getSubindex() << std::dec
                         << " ret=" << ret << " val=" << val << "\n";
        return false;
    }
    // Mirror written value into the request buffer so confirmation/readValue agree.
    uint8_t *dst = ecrt_sdo_request_data(entry->getRequest());
    if (dst) {
        memset(dst, 0, 8);
        memcpy(dst, data, len);
    }
    return true;
}
#endif // USE_KERNEL_ETHERCAT

void ECInterface::checkSDOUpdates() {
    const uint64_t now = microsecs();
    if (now < sdo_not_before) {
        return;
    }
    if (current_update_entry == sdo_update_entries.end()) {
        if (sdo_update_entries.size() == 0) {
            return;
        }
        current_update_entry = sdo_update_entries.begin();
        sdo_entry_state = e_None; // no active entry, next
    }
    if (current_update_entry != sdo_update_entries.end()) {
        SDOEntry *entry = *current_update_entry;
        if (!entry) {
            current_update_entry++;
            return;
        } // odd: no entry at this position

        // disabled entries are not automatically polled for changes unless they were already
        // in the middle of a poll when they were disabled. Initial and
        // write-confirmation reads are required and are allowed to complete.
        if (sdo_entry_state == e_None && entry->machineInstance() &&
            !entry->machineInstance()->enabled() && !entry->readPending()) {
            current_update_entry++;
            return;
        }
        ec_sdo_request_t *sdo = entry->getRequest();

        if (sdo_entry_state == e_None) {
            if (entry->operation() == SDOEntry::WRITE) {
                assert(!initialisation_entries.empty());
                return; // let the initialisation process deal with this update
            }

            switch (entry->operation()) {
            case SDOEntry::READ:
                if (!entry->pollDue(now)) {
                    current_update_entry++;
                    return;
                }
#ifdef USE_KERNEL_ETHERCAT
                // iod-elc: ecrt SDO is stubbed; use libelcethercat mailbox I/O.
                if (kernelBus && kernelBus->isOpen()) {
                    // At most one mailbox op per call, then back off so the ecat
                    // thread can renew domain output leases (SM WD if starved).
                    if (kernelSdoUploadEntry(entry)) {
                        bool first_read = !entry->ready();
                        entry->syncValue();
                        entry->success();
                        entry->schedulePoll(now);
                        if (first_read && entry->machineInstance() &&
                            entry->machineInstance()->properties.exists("default")) {
                            const Value &val =
                                entry->machineInstance()->properties.lookup("default");
                            DBG_ETHERCAT_SDO
                                << "Applying SDO default after initial/recommission read for "
                                << entry->getName() << ": " << val << "\n";
                            queueInitialisationRequest(entry, val);
                        }
                        // 10 ms when outputs armed (lease path critical); 2 ms at prep.
                        sdo_not_before = now + (g_kernel_outputs_armed ? 10000 : 2000);
                    }
                    else {
                        entry->failure();
                        entry->requestRead(now, 250);
                        sdo_not_before = now + 250000;
                    }
                    current_update_entry++;
                    sdo_entry_state = e_None;
                    return;
                }
#endif
                if (ecrt_sdo_request_state(sdo) == EC_REQUEST_BUSY) {
                    return;
                }
                DBG_ETHERCAT_CALLS << "ecrt_sdo_request_read\n";
                if (ecrt_sdo_request_read(sdo) != 0) {
                    entry->failure();
                    entry->requestRead(now, 250);
                    sdo_not_before = now + 250000;
                    current_update_entry++;
                    return;
                }
                sdo_entry_state = e_Busy_Update;
                break;
            case SDOEntry::WRITE:
                assert(false); // this should not be active
                DBG_ETHERCAT_CALLS << "ecrt_sdo_request_write\n";
                readValue(sdo, entry->getSize(), entry->getOffset());
                ecrt_sdo_request_write(sdo); // trigger first read
                sdo_entry_state = e_Busy_Update;
                break;
            default:
                assert(false);
            }
            return;
        }

        int state = 0;
        DBG_ETHERCAT_CALLS << "ecrt_sdo_request_state\n";
        switch ((state = ecrt_sdo_request_state(sdo))) {
        case EC_REQUEST_UNUSED: // request was not used yet
            sdo_entry_state = e_None;
            break;
        case EC_REQUEST_BUSY:
            break;
        case EC_REQUEST_SUCCESS:
            // before updating the value of the object check whether a new value is about to
            // be written to the io
            if (entry->operation() == SDOEntry::READ) {
                bool first_read = !entry->ready();
                entry->syncValue();
                entry->success();
                entry->schedulePoll(now);
                if (first_read && entry->machineInstance() &&
                    entry->machineInstance()->properties.exists("default")) {
                    const Value &val =
                        entry->machineInstance()->properties.lookup("default");
                    DBG_ETHERCAT_SDO << "Applying SDO default after initial read for "
                                     << entry->getName() << ": " << val << "\n";
                    queueInitialisationRequest(entry, val);
                }
            }
            // prepare to get the next entry
            current_update_entry++;
            sdo_entry_state = e_None;
            break;
        case EC_REQUEST_ERROR: {
            std::stringstream error;
            error << "Failed to read SDO!" << std::hex << "0x" << entry->getIndex() << ":"
                  << (int)entry->getSubindex() << std::dec;
            MessageLog::instance()->add(error.str());
            NB_MSG << error.str() << "\n";
            entry->failure();
            entry->requestRead(now, 250);
            sdo_not_before = now + 250000;
            current_update_entry++; // move on to the next item and retry soon
            sdo_entry_state = e_None;
        } break;
        default: {
            std::stringstream error;
            error << "unexpected sdo request state: " << state << std::hex << "0x"
                  << entry->getIndex() << ":" << (int)entry->getSubindex() << std::dec;
            MessageLog::instance()->add(error.str());
            NB_MSG << error.str() << "\n";
        }
        }
    }
}

bool ECInterface::checkSDOInitialisation() // returns true when no more initialisation is required
{
    acceptPendingSDOWrites();
    const uint64_t now = microsecs();
    if (now < sdo_not_before) {
        return false;
    }
    if (sdo_entry_state == e_Busy_Update) {
        return true;
    }
    if (current_init_entry == initialisation_entries.end()) {
        if (initialisation_entries.size() == 0) {
            return true;
        }
        current_init_entry = initialisation_entries.begin();
        sdo_entry_state = e_None;
    }
    if (current_init_entry != initialisation_entries.end()) {
        std::pair<SDOEntry *, Value> curr = *current_init_entry;
        SDOEntry *entry = curr.first;
        if (!entry) {
            DBG_ETHERCAT_SDO << "Skipping null entry when checking SDO\n";
            current_init_entry++;
            return false;
        } // odd: no entry at this position

        // A write, including an explicit startup default, must never overtake
        // the entry's initial upload.
        if (!entry->ready()) {
            return true;
        }

        ec_sdo_request_t *sdo = entry->getRequest();

        if (sdo_entry_state == e_None) {
#ifdef USE_KERNEL_ETHERCAT
            if (kernelBus && kernelBus->isOpen()) {
                entry->setOperation(SDOEntry::WRITE);
                DBG_ETHERCAT_SDO << "SDO entry - kernel write " << curr.second << "\n";
                if (kernelSdoDownloadEntry(entry, curr.second)) {
                    entry->syncValue();
                    entry->success();
                    entry->requestRead(now, 1); // confirm with a prompt upload
                    current_init_entry = initialisation_entries.erase(current_init_entry);
                    sdo_not_before = now + (g_kernel_outputs_armed ? 10000 : 2000);
                }
                else {
                    entry->failure();
                    sdo_not_before = now + 250000;
                    if (entry->getErrorCount() < 4) {
                        current_init_entry++;
                    }
                    else {
                        current_init_entry = initialisation_entries.erase(current_init_entry);
                        entry->setOperation(SDOEntry::READ);
                        entry->requestRead(now, 250);
                    }
                }
                sdo_entry_state = e_None;
                return false;
            }
#endif
            if (ecrt_sdo_request_state(sdo) == EC_REQUEST_BUSY) {
                return false;
            }
            entry->setOperation(SDOEntry::WRITE);
            if (entry->getSize() == 1) {
                entry->setData((bool)curr.second.iValue);
            }
            else if (entry->getSize() == 8) {
                entry->setData((uint8_t)curr.second.iValue);
            }
            else if (entry->getSize() == 16) {
                entry->setData((uint16_t)curr.second.iValue);
            }
            else if (entry->getSize() == 32) {
                entry->setData((uint32_t)curr.second.iValue);
            }
            DBG_ETHERCAT_SDO << "SDO entry - trigger write " << curr.second << "\n";
            readValue(sdo, entry->getSize());
            DBG_ETHERCAT_CALLS << "ecrt_sdo_request_write\n";
            if (ecrt_sdo_request_write(sdo) != 0) {
                entry->failure();
                sdo_not_before = now + 250000;
                return false;
            }
            sdo_entry_state = e_Busy_Initialisation;
            return false;
        }

        int state = 0;
        DBG_ETHERCAT_CALLS << "ecrt_sdo_request_state\n";
        switch ((state = ecrt_sdo_request_state(sdo))) {
        case EC_REQUEST_UNUSED: // request was not used yet
            sdo_entry_state = e_None;
            break;
        case EC_REQUEST_BUSY:
            break;
        case EC_REQUEST_SUCCESS:
            if (entry->operation() == SDOEntry::READ) {
                DBG_ETHERCAT_SDO << "SDO entry read\n";
            }
            else {
                DBG_ETHERCAT_SDO << "SDO entry written\n";
            }
            entry->syncValue();
            entry->success();
            entry->requestRead(now, 1); // confirm the write with a prompt read
            // prepare to get the next entry
            current_init_entry = initialisation_entries.erase(current_init_entry);
            sdo_entry_state = e_None;
            break;
        case EC_REQUEST_ERROR:
            if (entry->operation() == SDOEntry::READ) {
                DBG_ETHERCAT_SDO << "Failed to read SDO entry ";
            }
            else {
                DBG_ETHERCAT_SDO << "Failed to write SDO entry ";
            }
            DBG_ETHERCAT_SDO << std::hex << "0x" << entry->getIndex() << ":"
                             << (int)entry->getSubindex() << std::dec << "\n";
            entry->failure();
            sdo_not_before = now + 250000;
            if (entry->getErrorCount() < 4) {
                current_init_entry++; // move on to the next item and retry soon
            }
            else {
                current_init_entry = initialisation_entries.erase(current_init_entry);
                entry->setOperation(SDOEntry::READ);
                entry->requestRead(now, 250);
            }
            sdo_entry_state = e_None;
            break;
        default:
            DBG_ETHERCAT_SDO << "unexpected sdo request state: " << state << "\n";
        }
    }
    return false;
}
#endif //USE_SDO

ECModule *ECInterface::findModule(unsigned int pos) {
    boost::recursive_mutex::scoped_lock lock(modules_mutex);
    if (pos < 0 || (unsigned int)pos >= modules.size()) {
        return 0;
    }
    auto module = modules.at(pos);
    assert(module);
    assert(module->position == pos);
    return module;
}

void ECInterface::registerModules() {
#ifdef USE_KERNEL_ETHERCAT
    if (kernelBus && kernelBus->isOpen()) {
        // Offsets already resolved while populating modules from topology conf.
        DBG_ETHERCAT << "registerModules: kernel transport offsets already set from topology\n";
        return;
    }
#endif
    boost::recursive_mutex::scoped_lock lock(modules_mutex);
    for (unsigned int mi = 0; mi < modules.size(); ++mi) {
        ECModule *m = findModule(mi);
        assert(m);
        if (!m->ecrtMasterSlaveConfig(master)) {
            auto error = "Failed to get slave configuration.";
            MessageLog::instance()->add(error);
            DBG_ETHERCAT << error << "\n";
            return;
        }
        assert(m->slave_config);
        unsigned int module_offset_idx = 0;

        for (unsigned int i = 0; i < m->sync_count; ++i) {
            for (unsigned int j = 0; j < m->syncs[i].n_pdos; ++j) {
                for (unsigned int k = 0; k < m->syncs[i].pdos[j].n_entries; ++k) {
                    DBG_ETHERCAT_CALLS << "ecrt_config_reg_pdo_entry_pos\n";
                    if (std::string(m->name).substr(0, 6) == "EL2535" && i == 3 &&
                        m->syncs[i].n_pdos == 2) {
                        std::stringstream ss;
                        ss << "******* Warning: Configureing EL2535 with 2 pdos (need 4)";
                        MessageLog::instance()->add(ss.str());
                        std::cerr << ss.str() << "\n";
                    }
                    int res = ecrt_slave_config_reg_pdo_entry_pos(
                        m->slave_config, m->syncs[i].index, j, k, domain1,
                        &(m->bit_positions[module_offset_idx]));
                    if (res < 0) {
                        DBG_ETHERCAT << "Error: " << res << " registering pdo entry mapping "
                                     << " to sm " << i << " pdo: " << std::hex << "0x"
                                     << m->syncs[i].pdos[j].index << " " << " entry 0x"
                                     << m->syncs[i].pdos[j].entries[k].index << " " << std::dec
                                     << (int)m->syncs[i].pdos[j].entries[k].subindex << " " << "\n";
                    }
                    else {
                        DBG_ETHERCAT << "Successfully added item " << module_offset_idx
                                     << " at index " << std::hex << "0x" << m->syncs[i].pdos[j].index << " " << std::dec
                                     << " subindex " << (int)m->syncs[i].pdos[j].entries[k].subindex
                                     << " length "   << (int)m->syncs[i].pdos[j].entries[k].bit_length
                                     << " offset: "  << res
                                     << " bitpos: "  << m->bit_positions[module_offset_idx] << " "
                                     << m->entry_details[module_offset_idx].name << "\n";
                        m->offsets[module_offset_idx] = res;
                    }
                    module_offset_idx++;
                }
            }
        }
        assert(module_offset_idx < 64);
    }
}

void ECInterface::configureModules() {
#ifdef USE_KERNEL_ETHERCAT
    if (kernelBus && kernelBus->isOpen()) {
        // Most Clockwork MODULE entries have no ESI XML; legacy iod filled PDOs via
        // ecrt bus scan. Use the captured full-bus topology conf instead.
        const char *topo = elcDefaultTopologyConfigPath();
        std::vector<uint32_t> domain_ids;
        int ret = elcApplyConfigFile(kernelBus.get(), topo, &domain_ids);
        if (ret != 0) {
            std::cerr << "Failed to apply ELC topology config " << topo << " (" << ret << ")\n";
            return;
        }
        elcRegisterClockworkDomains(domain_ids);
        ret = elcPopulateModulesFromConfigFile(kernelBus.get(), topo);
        if (ret != 0) {
            std::cerr << "Failed to populate modules from topology " << topo << " (" << ret
                      << ")\n";
        }
        // Ready for STARTUP SEND activate: bus configured, report PREOP via kernel AL.
        master_state.link_up = 1;
        if (!ethercat_status) {
            ethercat_status = MachineInstance::find("ETHERCAT");
        }
        if (ethercat_status) {
            // Seed slave_states from current module AL aggregate (typically PREOP=2).
            check_slave_config_states();
            SetStateActionTemplate ssat = SetStateActionTemplate("SELF", "CONNECTED");
            SetStateAction *ssa = dynamic_cast<SetStateAction *>(ssat.factory(ethercat_status));
            if (ssa) {
                ethercat_status->enqueueAction(ssa);
            }
        }
        return;
    }
#endif
    boost::recursive_mutex::scoped_lock lock(modules_mutex);
    for (unsigned int mi = 0; mi < modules.size(); ++mi) {
        ECModule *m = findModule(mi);
        if (!m) {
            DBG_ETHERCAT << __FUNCTION__ << " missing ECModule at position " << mi << "\n";
        }
        assert(m);

        if (!m->ecrtMasterSlaveConfig(master)) {
            DBG_ETHERCAT << "Failed to get slave configuration.\n";
            return;
        }

        if (m->sync_count == 0) {
            DBG_ETHERCAT << "Warning: configuring module " << m->position
                         << " with no sync managers\n";
        }

        assert(m->slave_config);
        DBG_ETHERCAT << "\n\nConfiguring module " << m->position << ": " << m->name << "\n";
        unsigned int module_offset_idx = 0;

        for (unsigned int i = 0; i < m->sync_count; ++i) {
            int res;
            ec_direction_t dir = m->syncs[i].dir;
            if (dir == EC_DIR_OUTPUT) {
                m->syncs[i].watchdog_mode = EC_WD_ENABLE;
            }
            else {
                m->syncs[i].watchdog_mode = EC_WD_DEFAULT;
            }

            DBG_ETHERCAT_CALLS << "ecrt_config_sync_manager\n";
            res = ecrt_slave_config_sync_manager(m->slave_config, m->syncs[i].index,
                                                 m->syncs[i].dir, m->syncs[i].watchdog_mode);
            if (res < 0) {
                char buf[100];
                snprintf(buf, 100,
                         "Error %d setting WD enable state on sync manager %d for module %d", res,
                         i, m->position);
                MessageLog::instance()->add(buf);
                DBG_ETHERCAT << buf << "\n";
            }
            if (m->syncs[i].n_pdos && m->syncs[i].pdos) {
                DBG_ETHERCAT_CALLS << "ecrt_slave_config_pdo_assign_clear\n";
                ecrt_slave_config_pdo_assign_clear(m->slave_config, m->syncs[i].index);
            }

#if 0
            // TODO: why is this chunk here?
            if (m->syncs[i].dir == EC_DIR_OUTPUT && m->syncs[i].n_pdos > 0) {
                res = ecrt_slave_config_sync_manager(m->slave_config, i, EC_DIR_OUTPUT, EC_WD_ENABLE);
                if (res < 0) {
                    char buf[100];
                    snprintf(buf, 100, "Error %d setting WD enable state on sync manager %d for module %d\n",
                            res, i, m->position);
                    MessageLog::instance()->add(buf);
                    std::cout << buf << "\n";
                }
            }
#endif

            DBG_ETHERCAT << "---- adding pdo assignments for sm " << i << " " << m->syncs[i].n_pdos
                         << " items\n";
            for (unsigned int j = 0; j < m->syncs[i].n_pdos; ++j) {
                DBG_ETHERCAT_CALLS << "ecrt_slave_config_pdo_assign_add" << std::hex
                                   << m->syncs[i].index << " " << m->syncs[i].pdos[j].index
                                   << std::dec << "\n";
                int res = ecrt_slave_config_pdo_assign_add(m->slave_config, m->syncs[i].index,
                                                           m->syncs[i].pdos[j].index);
                if (res < 0) {
                    std::cerr << "Error: " << res << " appending pdo assignment " << " to sm " << i
                              << " pdo: " << std::hex << "0x" << m->syncs[i].pdos[j].index
                              << std::dec << "\n";
                }
                else {
                    DBG_ETHERCAT << "**** added pdo assignment " << " to sm " << i
                                 << " pdo: " << std::hex << "0x" << m->syncs[i].pdos[j].index
                                 << std::dec << " adding " << m->syncs[i].pdos[j].n_entries
                                 << " entries\n"
                                 << "\n";
                    if (m->syncs[i].pdos[j].n_entries) {
                        DBG_ETHERCAT_CALLS << "ecrt_config_pdo_mapping_clear\n";
                        ecrt_slave_config_pdo_mapping_clear(m->slave_config,
                                                            m->syncs[i].pdos[j].index);
                    }
                }
                for (unsigned int k = 0; k < m->syncs[i].pdos[j].n_entries; ++k) {
                    DBG_ETHERCAT_CALLS << "ecrt_config_pdo_mapping_add\n";
                    res = ecrt_slave_config_pdo_mapping_add(
                        m->slave_config, m->syncs[i].pdos[j].index,
                        m->syncs[i].pdos[j].entries[k].index,
                        m->syncs[i].pdos[j].entries[k].subindex,
                        m->syncs[i].pdos[j].entries[k].bit_length);
                    if (res < 0) {
                        std::cerr << "Error: " << res << " adding pdo entry mapping " << std::hex
                                  << "0x" << m->syncs[i].pdos[j].entries[k].index << " " << std::dec
                                  << m->syncs[i].pdos[j].entries[k].subindex << " " << "\n";
                        assert(false);
                    }
                    else {
                        DBG_ETHERCAT << "Successfully added entry item " << module_offset_idx
                                     << " at index " << std::hex << "0x" << m->syncs[i].pdos[j].index << " " << std::dec
                                     << " subindex " << (int)m->syncs[i].pdos[j].entries[k].subindex
                                     << " length "   << (int)m->syncs[i].pdos[j].entries[k].bit_length
                                     << " offset: "  << res
                                     << " bitpos: "  << m->bit_positions[module_offset_idx] << " "
                                     << m->entry_details[module_offset_idx].name << "\n";
                    }
#if 0
                    DBG_ETHERCAT_CALLS << "ecrt_slave_config_reg_pdo_entry_pos\n";
                    res = ecrt_slave_config_reg_pdo_entry_pos(
                                    m->slave_config,
                                    //m->syncs[i].pdos[j].entries[k].index,
                                    //m->syncs[i].pdos[j].entries[k].subindex,
                                    m->syncs[i].index, j, module_offset_idx,
                                    domain1, &(m->bit_positions[module_offset_idx]));
                    if (res < 0) {
                        std::cerr << "Error: " << res << " registering pdo entry mapping "
                                << " to sm " << i << " pdo: "
                                << std::hex
                                << "0x" << m->syncs[i].pdos[j].index << " "
                                << " entry 0x" << m->syncs[i].pdos[j].entries[k].index << " "
                                << std::dec
                                << (int)m->syncs[i].pdos[j].entries[k].subindex << " "
                                << "\n";
                    }
                    else {
                        std::cerr << "Successfully added item " << module_offset_idx
                                << " at index "
                                << std::hex
                                << "0x" << m->syncs[i].pdos[j].index << " "
                                << " subindex " << (int)m->syncs[i].pdos[j].entries[k].subindex
                                << " length " << (int)m->syncs[i].pdos[j].entries[k].bit_length
                                << " offset: " << res
                                << " bitpos: " << m->bit_positions[module_offset_idx]
                                << std::dec
                                << " " << m->entry_details[module_offset_idx].name
                                << "\n";
                        m->offsets[module_offset_idx] = res;
                    }
#endif
                    /*
                                        uint16_t subix = m->syncs[i].pdos[j].entries[k].subindex;
                                        if (
                                            ( m->syncs[i].pdos[j].index == 0x1a01
                                                && m->syncs[i].pdos[j].entries[k].index == 0x6000
                                                && m->syncs[i].pdos[j].entries[k].subindex/4 == 4)
                                            || ( m->syncs[i].pdos[j].index == 0x1a03
                                                && m->syncs[i].pdos[j].entries[k].index == 0x6010
                                                && m->syncs[i].pdos[j].entries[k].subindex/4 == 4)
                                            )
                                        {
                                            //subix = m->syncs[i].pdos[j].entries[k].subindex-17;
                                            if (m->syncs[i].pdos[j].entries[k].subindex == 17)
                                            for (subix = 0; subix<80; ++subix) {
                                               DBG_ETHERCAT_CALLS << "ecrt_slave_config_reg_pdo_entry\n";
                                                   res = ecrt_slave_config_reg_pdo_entry(
                                                m->slave_config, m->syncs[i].pdos[j].entries[k].index,
                                                subix,
                                                domain1, &(m->bit_positions[module_offset_idx]) );
                                        if (res < 0) {
                                            std::cerr << "Error: " << res <<" appending pdo entry mapping "
                                            << " to sm " << i << " pdo: "
                                            << std::hex
                                            << "0x" << m->syncs[i].pdos[j].index << " "
                                            << " entry 0x" << m->syncs[i].pdos[j].entries[k].index << " "
                                            << std::dec << subix << " "
                                            <<"\n";
                                        }
                                        else {
                                            std::cerr << "Successfully added item at index " << subix
                                                << " offset: " << res
                                                << " bitpos: " << m->bit_positions[module_offset_idx]
                                                << "\n";
                                            m->offsets[module_offset_idx] = res;
                                        }
                                        }
                                        }
                    */
                    ++module_offset_idx;
                    //}
                }
            }
        }
    }
}

// Add or replace a module in the modules list
tl::expected<bool, std::string> ECInterface::addModule(ECModule *module, bool reset_io) {

    if (module) {
        boost::recursive_mutex::scoped_lock lock(modules_mutex);
        DBG_ETHERCAT << "adding module " << module->name << " pos: " << module->position
                     << " to io\n";
        if (modules.size() == module->position) {
            modules.push_back(module);
        }
        else if (modules.size() > module->position) {
            auto m = modules.at(module->position);
            if (m->alias == module->alias && m->position == module->position) {
                if (reset_io) {
                    modules.at(module->position) = module;
                    std::stringstream ss;
                    ss << "similar module at " << module->position << " replaced"
                       << "\ncompare old,new:\n"
                       << compare_modules(*m, *module);
                    DBG_ETHERCAT << ss.str() << "\n";
                    delete m;
                }
                else {
                    std::stringstream ss;
                    ss << "Error: replacing module at position " << module->position
                       << " with a different type/release of module\n";
                    return tl::make_unexpected<std::string>(ss.str());
                }
            }
        }
        else {
            std::stringstream ss;
            ss << "Error: adding module " << module->position << " out of order\n";
            return tl::make_unexpected<std::string>(ss.str());
        }
    }
    else {
        return tl::make_unexpected<std::string>(std::string(__FUNCTION__) +
                                                " null module cannot be added");
    }
    return true;
}

std::vector<ec_slave_info_t> ECInterface::listSlaves() {
#ifdef USE_KERNEL_ETHERCAT
    if (kernelBus && kernelBus->isOpen()) {
        return kernelBus->listSlaves();
    }
#endif
    if (!master) {
        return {};
    }
    std::vector<ec_slave_info_t> slaves;
    unsigned int pos = 0;
    int res = 0;
    ec_master_info_t master_info;
    DBG_ETHERCAT_CALLS << "ecrt_master\n";
    res = ecrt_master(master, &master_info);
    while (res >= 0 && pos < master_info.slave_count) {
        ec_slave_info_t slave_info;
        memset(&slave_info, 0, sizeof(ec_slave_info_t));
        DBG_ETHERCAT_CALLS << "ecrt_master_get_slave\n";
        res = ecrt_master_get_slave(master, pos, &slave_info);
        if (res >= 0) {
            slaves.push_back(slave_info);
        }
        else {
            std::cerr << "Error getting slave info at position " << pos << "\n";
            assert(false);
        }
        ++pos;
    }
    return slaves;
}

void addEtherCatSlave(ec_master_t *m, const ec_slave_info_t &slave) {
    ec_pdo_entry_info_t *c_entries = 0;
    ec_pdo_info_t *c_pdos = 0;
    ec_sync_info_t *c_syncs = 0;
    EntryDetails *c_entry_details = 0;

    std::cerr << "----------- Adding " << slave.name << " ---------\n";

    unsigned int total_entries = 0, total_pdos = 0, total_syncs = 0;
    if (slave.sync_count) {
        unsigned int i, j, k, pdo_pos = 0, entry_pos = 0;
        const unsigned int estimated_max_entries = 128;
        const unsigned int estimated_max_pdos = 32;
        const unsigned int estimated_max_syncs = 32;
        // add pdo entries for this slave
        // note the assumptions here about the maximum number of entries, pdos and syncs we expect
        const int c_entries_size = sizeof(ec_pdo_entry_info_t) * estimated_max_entries;
        c_entries = new ec_pdo_entry_info_t[c_entries_size];
        memset(c_entries, 0, c_entries_size);

        c_entry_details = new EntryDetails[estimated_max_entries];

        const int c_pdos_size = sizeof(ec_pdo_info_t) * estimated_max_pdos;
        c_pdos = new ec_pdo_info_t[c_pdos_size];
        memset(c_pdos, 0, c_pdos_size);

        const int c_syncs_size = sizeof(ec_sync_info_t) * estimated_max_syncs;
        c_syncs = new ec_sync_info_t[c_syncs_size];
        memset(c_syncs, 0, c_syncs_size);

        total_syncs += slave.sync_count;
        assert(total_syncs < estimated_max_syncs);
        for (i = 0; i < slave.sync_count; i++) {
            DBG_ETHERCAT << "ecrt_master_get_sync_manager\n";
            int rc = ecrt_master_get_sync_manager(m, slave.position, i, &c_syncs[i]);
            assert(rc == 0);

            // Copy pdo entries to c_pdos
            if (!c_syncs[i].n_pdos) {
                c_syncs[i].pdos = 0;
            }
            else {
                ec_pdo_info_t pdo = {};
                unsigned int pdo_count = c_syncs[i].n_pdos;
                c_syncs[i].pdos = c_pdos + pdo_pos;
                total_pdos += pdo_count;
                assert(total_pdos < estimated_max_pdos);
                if (std::string(slave.name).substr(0, 6) == "EL2535" && i == 3 && pdo_count == 2) {
                    std::cerr << "******* detected EL2535 with 2 pdos (need 4)\n";
                    std::flush(std::cerr);
                }
                for (j = 0; j < pdo_count; j++) {
                    DBG_ETHERCAT << "ecrt_master_get_pdo(..., sm: " << i << ", pdo: " << j << ")"
                                 << "\n";
                    ecrt_master_get_pdo(m, slave.position, i, j, &pdo);
                    c_pdos[j + pdo_pos].index = pdo.index;
                    c_pdos[j + pdo_pos].n_entries = (unsigned int)pdo.n_entries;
                    if (!pdo.n_entries) {
                        c_pdos[j + pdo_pos].entries = 0;
                    }
                    else {
                        char pdo_name[40];
                        char index_str[40];
                        snprintf(pdo_name, 40, "pdo-%04X", pdo.index);
                        snprintf(index_str, 40, "0x%04X (%d)", c_syncs[i].index, c_syncs[i].index);
                        c_pdos[j + pdo_pos].entries = c_entries + entry_pos;
                        total_entries += pdo.n_entries;
                        assert(total_entries < estimated_max_entries);
                        ec_pdo_entry_info_t entry = {};
                        for (k = 0; k < pdo.n_entries; k++) {
                            DBG_ETHERCAT << "ecrt_master_get_pdo_entry\n";
                            ecrt_master_get_pdo_entry(m, slave.position, i, j, k, &entry);
                            char entry_name[100];
                            snprintf(entry_name, 100, "entry-%X-%X", entry.index, entry.subindex);

                            DBG_ETHERCAT << " entry: " << k << "{" << entry_pos << ", " << std::hex
                                         << (int)entry.index << ", " << std::dec
                                         << (int)entry.subindex << ", " << (int)entry.bit_length
                                         << ", " << entry_name << "}";
                            c_entries[entry_pos].index = entry.index;
                            c_entries[entry_pos].subindex = entry.subindex;
                            c_entries[entry_pos].bit_length = entry.bit_length;
                            c_entry_details[entry_pos].name = pdo_name;
                            c_entry_details[entry_pos].name += " ";
                            c_entry_details[entry_pos].name += entry_name;
                            c_entry_details[entry_pos].entry_index = entry_pos;
                            c_entry_details[entry_pos].pdo_index = j + pdo_pos;
                            c_entry_details[entry_pos].sm_index = i;

                            ++entry_pos;
                        }
                    }
                }
            }
            pdo_pos += c_syncs[i].n_pdos;
        }
        c_syncs[slave.sync_count].index = 0xff;
    }
    else {
        c_syncs = 0;
        c_pdos = 0;
        c_entries = 0;
    }
    DBG_ETHERCAT << "ECInterface adding module " << slave.name << "\n";
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
    auto res = ECInterface::instance()->addModule(module, true);
    if (!res) {
        delete module; // module may be already registered
        std::cerr << "Failed to add module " << slave.name << " " << res.error() << "\n";
    }
};

void collectEtherCatModules() {
    auto slaves = ECInterface::instance()->listSlaves();
    if (slaves.empty()) {
        DBG_ETHERCAT << "No slaves found on bus\n";
        return;
    }
    std::stringstream ss;
#ifdef USE_KERNEL_ETHERCAT
    // seed ECModule slots in bus order so XML can replace by position.
    // Full PDO discovery via ecrt is deferred; identity comes from elc_list_slaves.
    if (ECInterface::instance()->getKernelBus() &&
        ECInterface::instance()->getKernelBus()->isOpen()) {
        for (const ec_slave_info_t &slave : slaves) {
            ss << "Seeding kernel slave " << slave.name << " pos " << slave.position
               << " product 0x" << std::hex << slave.product_code << std::dec << "\n";
            ECModule *module = new ECModule();
            module->name = slave.name;
            module->alias = slave.alias;
            module->position = slave.position;
            module->vendor_id = slave.vendor_id;
            module->product_code = slave.product_code;
            module->revision_no = slave.revision_number;
            module->sync_count = 0;
            module->num_entries = 0;
            auto res = ECInterface::instance()->addModule(module, true);
            if (!res) {
                std::cerr << "Failed to seed module " << slave.name << " " << res.error() << "\n";
                delete module;
            }
        }
        DBG_ETHERCAT << ss.str() << "\n";
        return;
    }
    DBG_ETHERCAT << "collectEtherCatModules: kernel bus not open; no modules seeded\n";
    return;
#else
    for (const ec_slave_info_t &slave : slaves) {
        ss << "Adding slave " << std::hex << std::setw(8) << slave.product_code << " "
           << std::setw(8) << slave.revision_number << " at position " << std::dec
           << slave.position << "\n";
        addEtherCatSlave(ECInterface::master, slave);
    }
    DBG_ETHERCAT << ss.str() << "\n";
#endif
}



bool ECInterface::deactivate() {
#ifdef USE_KERNEL_ETHERCAT
    if (kernelBus && kernelBus->isOpen()) {
        kernelBus->disarmOutput();
        kernelBus->cycleDeactivate();
        active = false;
        activated_cycle_period_us_ = 0;
        g_output_lease_enabled = false;
        g_output_lease_timeout_ms = 0;
        g_output_lease_publish_renew = false;
        g_kernel_outputs_armed = false;
        domain1 = nullptr;
        if (domain1_pd) {
            delete[] domain1_pd;
            domain1_pd = nullptr;
        }
        data.setDataSize(0);
        data.setProcessData(nullptr, 0);
        {
            boost::recursive_mutex::scoped_lock lock(modules_mutex);
            for (ECModule *m : modules) {
                delete m;
            }
            modules.clear();
        }
        DBG_ETHERCAT << "Kernel transport deactivated\n";
        return true;
    }
#endif
    char buf[200];
    snprintf(buf, 200, "EtherCAT interface: Deactivating the EtherCAT master");
    MessageLog::instance()->add(buf);
    DBG_ETHERCAT << buf << "\n";
    active = false;
    if (master) {
        domain1 = 0;
        DBG_ETHERCAT_CALLS << "ecrt_master_deactivate\n";
        ecrt_master_deactivate(master);
        snprintf(buf, 200, "EtherCAT interface: recreating domain");
        MessageLog::instance()->add(buf);
        DBG_ETHERCAT << buf << "\n";
        DBG_ETHERCAT_CALLS << "ecrt_master_create_domain\n";
        domain1 = ecrt_master_create_domain(master);
        assert(domain1 != 0);
    }

    snprintf(buf, 200, "EtherCAT interface: cleaning up old io components,");
    MessageLog::instance()->add(buf);
    DBG_ETHERCAT << buf << "\n";

    data.setDataSize(0);
    data.setProcessData(nullptr, 0);
    {
        boost::recursive_mutex::scoped_lock lock(modules_mutex);
        snprintf(buf, 200, "EtherCAT interface: removing ethercat modules instances");
        MessageLog::instance()->add(buf);
        DBG_ETHERCAT << buf << "\n";
        std::vector<ECModule *>::iterator iter = modules.begin();
        while (iter != modules.end()) {
            ECModule *m = *iter++;
            snprintf(buf, 200, "EtherCAT interface: deleting module %s", m->name.c_str());
            MessageLog::instance()->add(buf);
            DBG_ETHERCAT << buf << "\n";
            delete m;
        }
        modules.clear();
    }
    domain1_pd = 0;
    snprintf(buf, 200, "EtherCAT interface: deactivate complete");
    MessageLog::instance()->add(buf);
    DBG_ETHERCAT << buf << "\n";
    return true;
}

unsigned long ECInterface::activatedCyclePeriodUs() const { return activated_cycle_period_us_; }

bool ECInterface::applyCyclePeriodUs(unsigned long period_us) {
    if (period_us < 100) {
        period_us = 100; // 0.1 ms floor (matches ELC min)
    }
    if (period_us > 1000000) {
        period_us = 1000000;
    }
    set_cycle_time(period_us);

    // Bus is fixed for the run after activate. Keep ecat userspace timer on the
    // activated period so pull cadence stays matched to the wire.
    if (active && activated_cycle_period_us_ != 0) {
        if (period_us != activated_cycle_period_us_) {
            static unsigned long last_ignored = 0;
            if (period_us != last_ignored) {
                std::cerr << "SYSTEM.CYCLE_DELAY=" << period_us
                          << " us ignored for EtherCAT bus (locked at "
                          << activated_cycle_period_us_
                          << " us at activate). Use SYSTEM.POLLING_DELAY for "
                             "Clockwork poll rate.\n";
                last_ignored = period_us;
            }
        }
        FREQUENCY = static_cast<unsigned int>(1000000UL / activated_cycle_period_us_);
        return true;
    }

    const unsigned int new_freq =
        period_us > 0 ? static_cast<unsigned int>(1000000UL / period_us) : 1;
    if (new_freq == 0) {
        return false;
    }
    FREQUENCY = new_freq;
    DBG_ETHERCAT << "EtherCAT CYCLE_DELAY set to " << period_us << " us (freq=" << FREQUENCY
                 << " Hz) — applied at next activate\n";
    return true;
}

bool ECInterface::activate() {
#ifdef USE_KERNEL_ETHERCAT
    if (kernelBus && kernelBus->isOpen()) {
        // Prefer SYSTEM.CYCLE_DELAY (µs) so startup can use 250/500 µs without
        // waiting for a later set_period.
        unsigned long period_us = get_cycle_time();
        if (period_us < 100) {
            period_us = 100;
        }
        uint32_t period_ns = static_cast<uint32_t>(period_us * 1000UL);
        if (period_ns < ELC_CYCLE_PERIOD_MIN_NS) {
            period_ns = ELC_CYCLE_PERIOD_MIN_NS;
            period_us = period_ns / 1000UL;
        }
        if (period_ns > ELC_CYCLE_PERIOD_MAX_NS) {
            period_ns = ELC_CYCLE_PERIOD_MAX_NS;
            period_us = period_ns / 1000UL;
        }
        FREQUENCY = period_us > 0 ? static_cast<unsigned int>(1000000UL / period_us) : FREQUENCY;
        set_cycle_time(period_us);

        // Output hang failsafe deferred until after cycle activate when the
        // kernel supports API 0.18 (configure-while-active + publish refill).
        // See enableKernelOutputLeaseIfRequested() below.
        g_output_lease_enabled = false;
        g_output_lease_timeout_ms = 0;
        g_output_lease_publish_renew = false;

        struct elc_cycle_activate act = {};
        int ret = kernelBus->cycleActivate(period_ns, 0, &act);
        if (ret != 0 || act.result != 0) {
            std::cerr << "elc_cycle_activate failed ret=" << ret << " result=" << act.result
                      << "\n";
            return false;
        }
        activated_cycle_period_us_ = period_us;
        size_t dsz = act.domain_size ? act.domain_size : kernelBus->domainSize();
        if (dsz == 0) {
            dsz = 1;
        }
        if (domain1_pd) {
            delete[] domain1_pd;
            domain1_pd = nullptr;
        }
        domain1_pd = new uint8_t[dsz];
        memset(domain1_pd, 0, dsz);
        g_kernel_output_image.assign(dsz, 0);
        g_kernel_output_mask.assign(dsz, 0);
        g_kernel_pub_mask.clear();
        g_kernel_pub_mask_valid = false;
        g_kernel_output_dirty = true; // publish zeros once after activate
        g_kernel_outputs_armed = false;
        // Clear live samples but do NOT force CW dual INVALID / size=0.
        // status_known=false holds last COMPLETE/INCOMPLETE until getDomainStatus.
        for (ElcDomainSlot &slot : g_domains) {
            slot.active = false;
            slot.valid = false;
            slot.armed = false;
            slot.rearm_required = false;
            slot.status_known = false;
            slot.wc = 0;
            slot.wc_state = 0;
            slot.faults = 0;
            // Keep domain_size / base_offset / published_state for hold.
        }
        g_domain_status_ok = false;
        g_all_domains_complete = false;
        g_domain_status_pending = !g_domains.empty();
        if (g_domain_status_pending) {
            std::cerr << "ECDOMAIN lifecycle: post-activate awaiting domain status"
                         " (holding prior CW states)\n";
        }
        // Non-null marker so existing domain checks pass (not an ecrt domain).
        domain1 = reinterpret_cast<ec_domain_t *>(domain1_pd);
        data.setDataSize(dsz);
        // process_data is owned separately by ProcessData; do not alias domain1_pd.
        active = true;
        initialised = true;
        all_ok = true;
#ifdef USE_DC
        // Same seed as the legacy ecrt activate path: monotonic ns aligned to
        // cycle. refreshKernelApplicationTime() then tracks the kernel clock.
        {
            const uint64_t cycle_ns = static_cast<uint64_t>(period_ns);
            dc_application_time_ns = monotonicTimeNs();
            if (cycle_ns) {
                dc_application_time_ns -= dc_application_time_ns % cycle_ns;
            }
            dc_cycle_adjustment_ns = 0;
            dc_difference_total_ns = 0;
            dc_delta_total_ns = 0;
            dc_last_difference_ns = 0;
            dc_filter_count = 0;
            dc_reference_valid = false;
        }
        refreshKernelApplicationTime();
#endif
        // MODULE / slave_states come from kernel via check_slave_config_states().
        check_slave_config_states();
        if (!ethercat_status) {
            ethercat_status = MachineInstance::find("ETHERCAT");
        }
        if (ethercat_status) {
            SetStateActionTemplate ssat = SetStateActionTemplate("SELF", "ACTIVE");
            SetStateAction *ssa = dynamic_cast<SetStateAction *>(ssat.factory(ethercat_status));
            if (ssa) {
                ethercat_status->enqueueAction(ssa);
            }
        }
        std::cout << "Kernel cycle active period_us=" << period_us
                  << " period_ns=" << period_ns << " freq=" << FREQUENCY
                  << " domain_size=" << dsz << "\n";
        // API 0.18: enable hang failsafe after OP is up (configure-while-active).
        // Prefer timeout_ms; publish/arm refill remaining (no renew storm).
        enableKernelOutputLeaseAfterActivate();
        return true;
    }
#endif
    int res;
    unsigned int pos = 0;
    ec_master_info_t master_info;
    DBG_ETHERCAT_CALLS << "ecrt_master: Activating master with configured slaves : \n";
    res = ecrt_master(ECInterface::master, &master_info);
    while (res >= 0 && pos < master_info.slave_count) {
        ECModule *module = ECInterface::findModule(pos);
        if (module) {
            DBG_ETHERCAT_CALLS << pos << " " << module->name << "\n";
        }
        else {
            DBG_ETHERCAT_CALLS << pos << " " << "no module\n";
        }
        ++pos;
    }
    DBG_ETHERCAT << "Activating master...";
    char buf[200];
#ifdef USE_DC
    // A NULL selection tells EtherLab to use the first DC-capable slave.
    res = ecrt_master_select_reference_clock(master, nullptr);
    if (res < 0) {
        snprintf(buf, 200, "EtherCAT DC: failed to select reference clock: %d", res);
        MessageLog::instance()->add(buf);
        DBG_ETHERCAT << buf << "\n";
        return false;
    }

    // Seed application time before activation so EtherLab can initialise DC
    // offsets without its "No application time received" startup warning.
    const uint64_t cycle_ns = 1000000000ULL / FREQUENCY;
    dc_application_time_ns = monotonicTimeNs();
    dc_application_time_ns -= dc_application_time_ns % cycle_ns;
    dc_cycle_adjustment_ns = 0;
    dc_difference_total_ns = 0;
    dc_delta_total_ns = 0;
    dc_last_difference_ns = 0;
    dc_filter_count = 0;
    dc_monitor_countdown = 0;
    dc_monitor_wait_cycles = 0;
    dc_reference_valid = false;
    dc_monitor_pending = false;
    dc_last_reference_result = 0;
    ecrt_master_application_time(master, dc_application_time_ns);
#endif
    DBG_ETHERCAT_CALLS << "ecrt_master_activate\n";
    if ((res = ecrt_master_activate(master))) {
        snprintf(buf, 200, "EtherCAT interface: Activating master failed with code: %d", res);
        MessageLog::instance()->add(buf);
        DBG_ETHERCAT << buf << "\n";
        return false;
    }
    active = true;
    {
        unsigned long period_us = get_cycle_time();
        if (period_us < 100) {
            period_us = 100;
        }
        activated_cycle_period_us_ = period_us;
        FREQUENCY = static_cast<unsigned int>(1000000UL / period_us);
    }
    snprintf(buf, 200, "Activated master");
    MessageLog::instance()->add(buf);
    DBG_ETHERCAT << buf << "\n";

    DBG_ETHERCAT_CALLS << "ecrt_domain_data\n";
    if (!(domain1_pd = ecrt_domain_data(domain1))) {
        snprintf(buf, 200, "EtherCAT interface: ecrt_domain_data failure");
        MessageLog::instance()->add(buf);
        DBG_ETHERCAT << buf << "\n";
        if (master) {
            DBG_ETHERCAT_CALLS << "ecrt_deactivate\n";
            ecrt_master_deactivate(master);
        }
        active = false;
        return false;
    }
    DBG_ETHERCAT_CALLS << "ecrt_domain_size\n";
    size_t domain_size = ecrt_domain_size(domain1);
    snprintf(buf, 200, "Activated master with domain size %ld", domain_size);
    DBG_ETHERCAT << buf << "\n";
    return true;
}

bool ECInterface::online() {
    boost::recursive_mutex::scoped_lock lock(modules_mutex);
    std::vector<ECModule *>::iterator iter = modules.begin();
    size_t n = modules.size();
    while (iter != modules.end()) {
        ECModule *m = *iter++;
        if (m->online()) {
            if (!online_modules.count(m)) {
                online_modules.insert(m);
                DBG_ETHERCAT << "Module: " << m->getName() << " online\n";
            }
        }
        else {
            if (online_modules.count(m)) {
                online_modules.erase(m);
                DBG_ETHERCAT << "Module: " << m->getName() << " not online\n";
            }
        }
    }
    return n == online_modules.size();
}

bool ECInterface::operational() {
    boost::recursive_mutex::scoped_lock lock(modules_mutex);
    std::vector<ECModule *>::iterator iter = modules.begin();
    size_t n = modules.size();
    while (iter != modules.end()) {
        ECModule *m = *iter++;
        if (m->operational()) {
            if (!operational_modules.count(m)) {
                operational_modules.insert(m);
                DBG_ETHERCAT << "Module: " << m->getName() << " operational\n";
            }
        }
        else {
            if (operational_modules.count(m)) {
                operational_modules.erase(m);
                DBG_ETHERCAT << "Module: " << m->getName() << " not operational\n";
            }
        }
    }
    return n == operational_modules.size();
}

#endif

void ECInterface::init() {
    DBG_ETHERCAT << "Linking to EtherCAT master and preparing domain\n";
    if (initialised) {
        return;
    }
#ifdef EC_SIMULATOR
    master = new ec_master_t;
    initialised = true;
    return;
#elif defined(USE_KERNEL_ETHERCAT)
    if (!kernelBus) {
        kernelBus.reset(new KernelEthercatBus());
    }
    int ret = kernelBus->open();
    if (ret != 0) {
        std::cerr << "Failed to open kernel EtherCAT transport: " << ret
                  << " (" << strerror(-ret) << ")\n";
        initialised = false;
        return;
    }
    initialised = true;
    all_ok = true;
    master_state.link_up = 1;
    master_state.al_states = 0x2; // PREOP until cycle activate
    ethercat_status = MachineInstance::find("ETHERCAT");
    if (ethercat_status) {
        ethercat_status->setValue("slave_states", Value{static_cast<uint64_t>(2)});
    }
    DBG_ETHERCAT << "KernelEthercatBus opened on /dev/elc_ethercat0\n";
    return;
#else
    DBG_ETHERCAT_CALLS << "ecrt_request_master\n";
    master = ecrt_request_master(0);
    if (!master) {
        DBG_MSG << "Failed to obtain access to the EtherCAT master\n";
        initialised = false;
        assert(master);
        return;
    }
    /*  trying to work out how to increase the master debug level
        {
        int res = ec_master_debug_level(master, 10);
        if (res != 0) {
            std::cerr << "Warning EtherCAT master debug level not changed (err " << res << ")\n";
        }
        }
    */

#if 0
    int res = 0;
    DBG_ETHERCAT_CALLS << "ecrt_master\n";
    if ((res = ecrt_master(master, &master_info)) < 0) {
        std::cerr << "Error " << res << " getting master info\n";
        master_into_time = 0; // master info information is invalid
    }
    else {
        master_info_time = microsecs();
    }
#endif

    char buf[200];
    DBG_ETHERCAT_CALLS << "ecrt_master_create_domain\n";
    domain1 = ecrt_master_create_domain(master);
    if (!domain1) {
        snprintf(buf, 200, "EtherCAT interface: failed to create domain");
        initialised = false;
        return;
    }
    else {
        DBG_ETHERCAT_CALLS << "ecrt_domain_size\n";
        snprintf(buf, 200, "EtherCAT interface: domain1 successfully created with size %ld",
                 ecrt_domain_size(domain1));
        MessageLog::instance()->add(buf);
        DBG_ETHERCAT << buf << "\n";
    }

    all_ok = true; // ok to try to start processing
    failure_count = 0;

    check_master_state();

#if 0
    IODCommandThread::registerCommand("EC", new IODCommandEtherCATTool);
    IODCommandThread::registerCommand("MASTER", new IODCommandMasterInfo);
    IODCommandThread::registerCommand("SLAVES", new IODCommandGetSlaveConfig);
#endif
#if 0
    ethercat_status = MachineInstance::find("ETHERCAT");
    if (ethercat_status) {

        const char *next_state = master_state.link_up ? "CONNECTED" : "DISCONNECTED";
        SetStateActionTemplate ssat = SetStateActionTemplate("SELF", next_state);
        SetStateAction *ssa = dynamic_cast<SetStateAction *>(ssat.factory(ethercat_status));
        ethercat_status->enqueueAction(ssa);

        const Value &tolerance_v = ethercat_status->getValue("tolerance");
        if (tolerance_v.kind != Value::t_integer) {
            failure_tolerance = &default_tolerance;
        }
        else {
            failure_tolerance = &tolerance_v.iValue;
        }

        std::cerr << "EtherCAT interface using " << *failure_tolerance
                << " tries before marking the master state as bad\n";
    }
    else {
        std::cerr << "EtherCAT interface could not find a clockwork bridge object\n";
    }
#endif

    initialised = true;
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

ECInterface *ECInterface::instance_ = 0;

ECInterface *ECInterface::instance() {
    if (!instance_) {
        instance_ = new ECInterface();
    }
    return instance_;
}

#ifndef EC_SIMULATOR

// copy interesting bits that have changed from the supplied
// data into the process data and the saved copy of the process data.
// the latter is because we want to properly detect changes in the
// next read cycle

#if VERBOSE_DEBUG
static void display(uint8_t *p, size_t n) {
    for (unsigned int i = 0; i < n; ++i) {
        DBG_ETHERCAT_PACKETS << std::setw(2) << std::setfill('0') << std::hex << (unsigned int)p[i];
    }
    DBG_ETHERCAT_PACKETS << std::dec;
}
#endif

#ifdef USE_KERNEL_ETHERCAT
void ECInterface::applyKernelOutputBit(unsigned int io_offset, unsigned int bitpos, bool on) {
    if (!active || !domain1_pd) {
        return;
    }
    size_t dsz = kernelBus ? kernelBus->domainSize() : 0;
    if (dsz == 0) {
        dsz = data.getProcessDataSize();
    }
    if (dsz == 0) {
        return;
    }
    if (g_kernel_output_image.size() < dsz) {
        g_kernel_output_image.resize(dsz, 0);
        g_kernel_output_mask.resize(dsz, 0);
    }
    unsigned int byte = io_offset + bitpos / 8;
    unsigned int bit = bitpos % 8;
    if (byte >= dsz) {
        return;
    }
    const uint8_t m = static_cast<uint8_t>(1u << bit);
    g_kernel_output_mask[byte] = static_cast<uint8_t>(g_kernel_output_mask[byte] | m);
    if (on) {
        g_kernel_output_image[byte] = static_cast<uint8_t>(g_kernel_output_image[byte] | m);
        domain1_pd[byte] = static_cast<uint8_t>(domain1_pd[byte] | m);
    }
    else {
        g_kernel_output_image[byte] = static_cast<uint8_t>(g_kernel_output_image[byte] & ~m);
        domain1_pd[byte] = static_cast<uint8_t>(domain1_pd[byte] & ~m);
    }
    g_kernel_output_dirty = true;
}

void ECInterface::applyKernelOutputValue(unsigned int io_offset, unsigned int bitpos,
                                         unsigned int bitlen, uint32_t value) {
    if (!active || !domain1_pd || bitlen == 0) {
        return;
    }
    size_t dsz = kernelBus ? kernelBus->domainSize() : 0;
    if (dsz == 0) {
        dsz = data.getProcessDataSize();
    }
    if (g_kernel_output_image.size() < dsz) {
        g_kernel_output_image.resize(dsz, 0);
        g_kernel_output_mask.resize(dsz, 0);
    }
    // Little-endian multi-byte write starting at io_offset (bitpos for sub-byte).
    if (bitlen == 1) {
        applyKernelOutputBit(io_offset, bitpos, value != 0);
        return;
    }
    unsigned int nbytes = (bitlen + 7) / 8;
    if (io_offset + nbytes > dsz) {
        return;
    }
    for (unsigned int i = 0; i < nbytes; ++i) {
        uint8_t byte_mask = 0xff;
        if (bitlen < 8 && i == 0) {
            byte_mask = static_cast<uint8_t>((1u << bitlen) - 1u) << (bitpos % 8);
        }
        g_kernel_output_mask[io_offset + i] =
            static_cast<uint8_t>(g_kernel_output_mask[io_offset + i] | byte_mask);
        uint8_t vb = static_cast<uint8_t>((value >> (8 * i)) & 0xff);
        g_kernel_output_image[io_offset + i] = static_cast<uint8_t>(
            (g_kernel_output_image[io_offset + i] & ~byte_mask) | (vb & byte_mask));
        domain1_pd[io_offset + i] = static_cast<uint8_t>(
            (domain1_pd[io_offset + i] & ~byte_mask) | (vb & byte_mask));
    }
    g_kernel_output_dirty = true;
}
#endif

void ECInterface::updateDomain(uint32_t size, uint8_t *data, uint8_t *mask) {
#ifdef USE_KERNEL_ETHERCAT
    // domain1 is a non-ecrt marker; the real images are domain1_pd + g_kernel_output_*.
    // ecrt_domain_data() is a stub — never use it here.
    if (!domain1_pd || !data || !mask || size == 0) {
        return;
    }
    if (g_kernel_output_image.size() < size) {
        g_kernel_output_image.resize(size, 0);
        g_kernel_output_mask.resize(size, 0);
    }
    uint8_t *pd = domain1_pd;
    uint8_t *out = g_kernel_output_image.data();
    uint8_t *omask = g_kernel_output_mask.data();
#else
    DBG_ETHERCAT_CALLS << "ecrt_domain_data\n";
    uint8_t *pd = ecrt_domain_data(domain1);
    if (!pd || !data || !mask) {
        return;
    }
#endif

#ifdef USE_KERNEL_ETHERCAT
    bool changed = false;
#endif
    for (unsigned int i = 0; i < size; ++i) {
        if (*mask) {
#ifdef USE_KERNEL_ETHERCAT
            // Always merge commanded bits into the output shadow (even if
            // domain1_pd already matches — first write after a wiped snapshot).
            if (*mask) {
                const uint8_t prev = out[i];
                omask[i] = static_cast<uint8_t>(omask[i] | *mask);
                out[i] = static_cast<uint8_t>((out[i] & ~*mask) | (*data & *mask));
                pd[i] = static_cast<uint8_t>((pd[i] & ~*mask) | (*data & *mask));
                if (out[i] != prev) {
                    changed = true;
                }
            }
#else
            if (*data != *pd) {
                uint8_t bitmask = 0x01;
                while (bitmask) {
                    if (*mask & bitmask) {
                        uint8_t pdb = *pd & bitmask;
                        uint8_t db = *data & bitmask;
                        if (pdb != db) {
                            if (db) {
                                *pd |= bitmask;
                            }
                            else {
                                *pd &= static_cast<uint8_t>(0xff - bitmask);
                            }
                        }
                    }
                    bitmask = static_cast<uint8_t>(bitmask << 1);
                }
            }
#endif
        }
        ++pd;
        ++mask;
        ++data;
#ifdef USE_KERNEL_ETHERCAT
        ++out;
        ++omask;
#endif
    }
#ifdef USE_KERNEL_ETHERCAT
    if (changed) {
        g_kernel_output_dirty = true;
    }
#endif
}

#ifdef USE_KERNEL_ETHERCAT
// Merge commanded outputs into a full-domain snapshot. Kernel snapshots copy
// the live domain layout with output bytes zeroed when disarmed; without this
// merge, turnOn/VALUE commands vanish before collect/publish.
static void mergeKernelOutputShadow(uint8_t *domain, size_t dsz) {
    if (!domain || g_kernel_output_image.empty()) {
        return;
    }
    size_t n = std::min(dsz, g_kernel_output_image.size());
    for (size_t i = 0; i < n; ++i) {
        const uint8_t m = g_kernel_output_mask[i];
        if (m) {
            domain[i] = static_cast<uint8_t>((domain[i] & ~m) | (g_kernel_output_image[i] & m));
        }
    }
}

// Refresh cached per-domain validity/arm. Returns true if primary domain status
// was obtained (multi-domain layout in use).
static bool refreshKernelDomainHealth(KernelEthercatBus *bus, bool log_periodic) {
    if (!bus || !bus->isOpen()) {
        return false;
    }
    static uint64_t last_log_us = 0;
    const uint64_t now = microsecs();
    // Periodic summary at 1 Hz (edge COMPLETE/INCOMPLETE logs are separate).
    const bool do_log =
        log_periodic && (last_log_us == 0 || now - last_log_us >= 1000000ULL);
    if (do_log) {
        last_log_us = now;
    }

    struct elc_io_status io = {};
    if (do_log && bus->getIoStatus(&io) == 0) {
        std::cerr << "ECDOMAIN aggregate bus_healthy=" << (int)io.bus_healthy
                  << " link=" << (int)io.link_up << " armed=" << (int)io.outputs_armed
                  << " rearm=" << (int)io.rearm_required << " faults=0x" << std::hex
                  << io.current_faults << std::dec
                  << " slaves_op=" << io.configured_slaves_operational << "/"
                  << io.configured_slave_count
                  << " domain_auth=" << (bus->hasDomainOutputAuthority() ? 1 : 0)
                  << " all_ok_src="
                  << (g_domain_status_ok ? "primary_domain" : "aggregate")
                  << " pending=" << (g_domain_status_pending ? 1 : 0) << "\n";
    }

    bool got_primary = false;

    for (size_t i = 0; i < g_domains.size(); ++i) {
        ElcDomainSlot &slot = g_domains[i];
        struct elc_domain_status st = {};
        int ret = bus->getDomainStatus(slot.id, &st);
        if (ret != 0) {
            // ESTALE / not ready: hold last known sample. Do not invent
            // active=0, size=0, dual INVALID (lifecycle, not bus isolation).
            slot.status_known = false;
            if (do_log) {
                std::cerr << "ECDOMAIN id=" << slot.id
                          << " getDomainStatus failed ret=" << ret
                          << " (holding prior CW state)\n";
            }
            continue;
        }
        if (i == 0) {
            got_primary = true;
        }
        const bool was_armed = slot.armed;
        slot.status_known = true;
        slot.active = st.active != 0;
        // data_valid already incorporates WC-firewall healthy + snapshot.
        slot.valid = st.active != 0 && st.data_valid != 0;
        slot.armed = st.outputs_armed != 0;
        slot.rearm_required = st.rearm_required != 0;
        slot.wc = st.working_counter;
        slot.wc_state = st.working_counter_state;
        slot.faults = st.current_faults;
        slot.base_offset = st.base_offset;
        if (st.domain_size != 0) {
            slot.domain_size = st.domain_size;
        }
        // Lost arm on a still-valid domain → republish+rearm (fault epoch).
        if (was_armed && !slot.armed && slot.valid) {
            g_kernel_output_dirty = true;
        }
        if (do_log) {
            const char *cw = domainSlotCwState(slot);
            std::cerr << "ECDOMAIN id=" << st.domain_config_id
                      << (i == 0 ? " (primary)" : "")
                      << " active=" << (int)st.active << " data_valid=" << (int)st.data_valid
                      << " wc=" << st.working_counter
                      << " wc_state=" << (unsigned)st.working_counter_state
                      << " armed=" << (int)st.outputs_armed
                      << " rearm=" << (int)st.rearm_required << " faults=0x" << std::hex
                      << st.current_faults << std::dec << " base=" << st.base_offset
                      << " size=" << st.domain_size
                      << " cw=" << (cw ? cw : "HOLD") << "\n";
        }
    }
    g_domain_status_ok = got_primary;
    // Quiet-publish path: primary domain arm is "outputs live".
    g_kernel_outputs_armed =
        !g_domains.empty() && g_domains.front().status_known && g_domains.front().armed;
    publishKernelEthercatClockworkMachines();
    return got_primary;
}

static void logKernelDomainHealth(KernelEthercatBus *bus) {
    (void)refreshKernelDomainHealth(bus, true);
}

// Hang failsafe after cycle is up (elc API 0.18 configure-while-active).
// ELC_OUTPUT_LEASE=0 forces off; =1 forces on; unset = on when CAP present.
static void enableKernelOutputLeaseAfterActivate() {
    g_output_lease_enabled = false;
    g_output_lease_timeout_ms = 0;
    g_output_lease_publish_renew = false;
    if (!ECInterface::instance() || !ECInterface::instance()->getKernelBus()) {
        return;
    }
    KernelEthercatBus *bus = ECInterface::instance()->getKernelBus();
    if (!bus->isOpen() || !bus->hasOutputLease() || bus->configGeneration() == 0) {
        std::cerr << "elc output lease: OFF (no CAP_OUTPUT_LEASE / generation)\n";
        return;
    }
    const char *lease_env = getenv("ELC_OUTPUT_LEASE");
    if (lease_env && lease_env[0] == '0' && lease_env[1] == '\0') {
        std::cerr << "elc output lease: OFF (ELC_OUTPUT_LEASE=0)\n";
        return;
    }
    const bool force_on = lease_env && lease_env[0] == '1' && lease_env[1] == '\0';
    // Auto-enable when 0.18 publish-renew is available; otherwise require =1
    // (legacy cycle_budget path was plant-noisy without publish refill).
    if (!force_on && !bus->hasOutputLeasePublishRenew()) {
        std::cerr << "elc output lease: OFF (no PUBLISH_RENEW CAP; set "
                     "ELC_OUTPUT_LEASE=1 for legacy renew path)\n";
        return;
    }
    // Wall hang: default 2000 ms (docs 500–2000). Override ELC_OUTPUT_LEASE_MS.
    uint32_t timeout_ms = 2000;
    if (const char *ms_env = getenv("ELC_OUTPUT_LEASE_MS")) {
        char *end = nullptr;
        unsigned long v = strtoul(ms_env, &end, 10);
        if (end != ms_env && v > 0 && v <= 600000UL) {
            timeout_ms = static_cast<uint32_t>(v);
        }
    }
    int lret = bus->configureOutputLease(timeout_ms, 0 /* all domains */);
    if (lret != 0) {
        std::cerr << "elc output lease: configure failed ret=" << lret << "\n";
        return;
    }
    g_output_lease_enabled = true;
    g_output_lease_timeout_ms = timeout_ms;
    g_output_lease_publish_renew = bus->hasOutputLeasePublishRenew();
    std::cerr << "elc output lease: ON timeout_ms=" << timeout_ms
              << " publish_renew=" << (g_output_lease_publish_renew ? 1 : 0)
              << " (hang = no publish for ~timeout; not POLLING_DELAY)\n";
    // Legacy: seed remaining if kernel did not (0.18 seeds on configure).
    if (!g_output_lease_publish_renew) {
        (void)bus->renewOutputLease();
    }
}

// Arm one domain authority (flags = domain_config_id). Requires a prior
// publish with matching generation/sequence. Returns 0 on success.
static int armKernelDomain(KernelEthercatBus *bus, uint32_t domain_config_id,
                           uint64_t config_generation, uint64_t output_sequence) {
    if (!bus) {
        return -EINVAL;
    }
    struct elc_output_arm arm = {};
    elc_init_api_header(&arm, sizeof(arm));
    arm.flags = domain_config_id; // 0 = all; non-zero = that domain only (API 0.17)
    arm.config_generation = config_generation;
    arm.output_sequence = output_sequence;
    return bus->armOutput(&arm);
}

// Pull the kernel's latest coherent process image into domain1_pd.
// The kmod double-buffers: each successful RT cycle publishes into the
// inactive buffer and swaps; GET_INPUT_SNAPSHOT always returns the current
// active buffer (input_sequence advances per publish). Failed ioctl keeps the
// previous domain1_pd (last known current), never zeros it.
static int pullLatestKernelSnapshot(KernelEthercatBus *bus, uint8_t *domain1_pd, size_t dsz,
                                    long &warned) {
    if (!bus || !domain1_pd || dsz == 0) {
        return -EINVAL;
    }
    struct elc_input_snapshot snap = {};
    int ret = bus->getInputSnapshot(domain1_pd, dsz, &snap);
    if (ret != 0) {
        if (warned++ % 200 == 0) {
            std::cerr << "elc_get_input_snapshot failed: " << ret
                      << " (keeping previous domain image)\n";
        }
        return ret;
    }
    mergeKernelOutputShadow(domain1_pd, dsz);
    return 0;
}

void ECInterface::refreshKernelApplicationTime() {
#ifdef USE_DC
    if (!kernelBus || !kernelBus->isOpen()) {
        return;
    }
    // Prefer the kernel DC motion-clock contract (same field the legacy ecrt
    // path exposes as dc_application_time_ns). Fall back to the cycle schedule
    // time, then to a monotonic seed advanced by cycle index * period.
    struct elc_cycle_dc_info dc = {};
    if (kernelBus->cycleDcInfo(&dc) == 0) {
        if (dc.application_time_ns != 0) {
            dc_application_time_ns = dc.application_time_ns;
            return;
        }
        if (dc.scheduled_time_ns != 0) {
            dc_application_time_ns = dc.scheduled_time_ns;
            return;
        }
        if (dc.cycle_period_ns != 0 && dc.cycle_index != 0) {
            // Reconstruct a DC-like ns clock: seed once, then index * period.
            static uint64_t kernel_app_seed_ns = 0;
            static uint64_t kernel_app_seed_cycle = 0;
            if (kernel_app_seed_ns == 0) {
                kernel_app_seed_ns = monotonicTimeNs();
                kernel_app_seed_ns -= kernel_app_seed_ns % dc.cycle_period_ns;
                kernel_app_seed_cycle = dc.cycle_index;
            }
            const uint64_t delta_cycles =
                (dc.cycle_index >= kernel_app_seed_cycle)
                    ? (dc.cycle_index - kernel_app_seed_cycle)
                    : 0;
            dc_application_time_ns =
                kernel_app_seed_ns + delta_cycles * dc.cycle_period_ns;
            return;
        }
    }
    struct elc_cycle_info info = {};
    if (kernelBus->cycleInfo(&info) == 0) {
        if (info.scheduled_time_ns != 0) {
            dc_application_time_ns = info.scheduled_time_ns;
            return;
        }
        if (info.cycle_period_ns != 0 && info.cycle_index != 0) {
            static uint64_t kernel_info_seed_ns = 0;
            static uint64_t kernel_info_seed_cycle = 0;
            if (kernel_info_seed_ns == 0) {
                kernel_info_seed_ns = monotonicTimeNs();
                kernel_info_seed_ns -= kernel_info_seed_ns % info.cycle_period_ns;
                kernel_info_seed_cycle = info.cycle_index;
            }
            const uint64_t delta_cycles =
                (info.cycle_index >= kernel_info_seed_cycle)
                    ? (info.cycle_index - kernel_info_seed_cycle)
                    : 0;
            dc_application_time_ns =
                kernel_info_seed_ns + delta_cycles * info.cycle_period_ns;
            return;
        }
    }
    // Last resort: wall-adjacent monotonic ns (still /1000 → µs for IOTIME).
    if (dc_application_time_ns == 0) {
        dc_application_time_ns = monotonicTimeNs();
    }
#endif
}
#endif

void ECInterface::receiveState(bool pull_process_image) {
    static long warned = 0;
#ifdef USE_KERNEL_ETHERCAT
    if (kernelBus && kernelBus->isOpen()) {
        if (!initialised) {
            return;
        }
        if (active && domain1_pd && pull_process_image) {
            size_t dsz = kernelBus->domainSize();
            if (dsz == 0) {
                dsz = data.getProcessDataSize();
            }
            if (dsz > 0) {
                // Latest coherent image from kernel double-buffer. Callers
                // should only request this at POLLING_DELAY (or first run);
                // intermediate bus ticks keep the previous domain1_pd.
                if (pullLatestKernelSnapshot(kernelBus.get(), domain1_pd, dsz, warned) == 0) {
                    // Keep IOTIME / getApplicationTimeNs() on the same µs scale
                    // as the legacy ecrt DC application clock.
                    refreshKernelApplicationTime();
                }
            }
            last_receive = microsecs();
        }
        // IO / domain status is not needed every bus tick once armed and healthy.
        // Rate-limit: 10 ms when all domains complete + armed; 1 ms while any
        // domain is incomplete/invalid (so CW ECDomain_* tracks power-off quickly).
        {
            static uint64_t last_io_status = 0;
            const uint64_t io_period_us =
                (g_kernel_outputs_armed && all_ok && g_all_domains_complete &&
                 !g_domain_status_pending)
                    ? 10000ULL
                    : 1000ULL;
            uint64_t t = microsecs();
            if (last_io_status == 0 || t - last_io_status >= io_period_us) {
                last_io_status = t;
                struct elc_io_status st = {};
                if (kernelBus->getIoStatus(&st) == 0) {
                    master_state.slaves_responding = st.slaves_responding;
                    master_state.link_up = st.link_up ? 1 : 0;
                    // Refresh domain caches (and 1 Hz ECDOMAIN log when active).
                    if (active) {
                        (void)refreshKernelDomainHealth(kernelBus.get(), true);
                    }
                    // Stage 2: gate all_ok on *primary* domain validity so a
                    // secondary domain incomplete does not freeze primary IO/CW.
                    // Fall back to aggregate bus_healthy when domains absent.
                    if (g_domain_status_ok && !g_domains.empty()) {
                        all_ok = (st.link_up != 0 && g_domains.front().valid) ||
                                 (!active && st.link_up != 0);
                    }
                    else {
                        all_ok = st.bus_healthy != 0 || (!active && st.link_up);
                    }
                    if (!active) {
                        g_kernel_outputs_armed = true;
                    }
                    else if (!g_domain_status_ok) {
                        // Single-domain / no domain status: keep aggregate path.
                        if (!st.outputs_armed) {
                            g_kernel_outputs_armed = false;
                            if (st.bus_healthy) {
                                g_kernel_output_dirty = true;
                            }
                        }
                    }
                    // Multi-domain: armed flags updated inside refreshKernelDomainHealth.
                    // If a healthy domain needs rearm, mark dirty so sendUpdates arms it.
                    if (g_domain_status_ok && kernelBus->hasDomainOutputAuthority()) {
                        for (const ElcDomainSlot &slot : g_domains) {
                            if (slot.valid && !slot.armed) {
                                g_kernel_output_dirty = true;
                                break;
                            }
                        }
                    }
                }
            }
        }
        // AL/MODULE state: rate-limit (not every bus cycle). 34 slaves × ioctl
        // at 500–2kHz is a large fraction of ecat CPU; 10 ms is enough for STARTUP.
        {
            static uint64_t last_al_check = 0;
            const uint64_t al_period_us = 10000; // 10 ms
            uint64_t t = microsecs();
            if (last_al_check == 0 || t - last_al_check >= al_period_us) {
                last_al_check = t;
                check_slave_config_states();
            }
        }
#ifdef USE_SDO
        if (checkSDOInitialisation()) {
            checkSDOUpdates();
        }
#endif
        return;
    }
#endif
    if (!master || !initialised) {
        if (warned++ % 100 == 0)
            std::cerr << "master not ready to receive state "
                      << ((!master) ? "(no master)" : "(!initialised)") << "\n"
                      << std::flush;
        return;
    }

    if (active) {
#ifdef KEEP_STATS
        if (keep_stats) {
            uint64_t now = microsecs();
            int64_t dt = now - last_update;
            if (last_update != 0) {
                update_to_recv.add(dt);
            }
            last_receive = now;
            if (update_to_recv.getCount() >= 1000) {
                update_to_recv.report(std::cout);
                update_to_recv.reset();
            }
        }
#endif
        // receive process data
        DBG_ETHERCAT_CALLS << "ecrt_master_receive\n";
        ecrt_master_receive(master);
        DBG_ETHERCAT_CALLS << "ecrt_domain_process\n";
        ecrt_domain_process(domain1);
#ifdef USE_DC
        processDistributedClock();
#endif
        check_domain1_state();
    }
    // check for master state (optional)
    check_master_state();
    // check for slave configuration state(s) (optional)
    check_slave_config_states();

#ifdef USE_SDO
    if (checkSDOInitialisation()) {
        checkSDOUpdates();
    }
#endif
}

void ECInterface::receivePendingDomainState() {
#ifdef USE_KERNEL_ETHERCAT
    // Kernel RT task may publish a newer snapshot while we handled CW/output
    // messages. Re-pull so sendUpdates and any late collect see the absolute
    // latest coherent image (same double-buffer contract as receiveState).
    if (kernelBus && kernelBus->isOpen()) {
        if (!initialised || !active || !domain1_pd) {
            return;
        }
        size_t dsz = kernelBus->domainSize();
        if (dsz == 0) {
            dsz = data.getProcessDataSize();
        }
        if (dsz > 0) {
            static long late_warned = 0;
            (void)pullLatestKernelSnapshot(kernelBus.get(), domain1_pd, dsz, late_warned);
            last_receive = microsecs();
        }
        return;
    }
#endif
    if (!master || !initialised || !active || !domain1) {
        return;
    }

#ifndef EC_SIMULATOR
    // A cyclic LRW response can occasionally arrive after the receive at the
    // start of the cycle. Give EtherLab another opportunity to complete and
    // dequeue it before the same domain is queued with a new index.
    DBG_ETHERCAT_CALLS << "ecrt_master_receive (late)\n";
    ecrt_master_receive(master);
    DBG_ETHERCAT_CALLS << "ecrt_domain_process (late)\n";
    ecrt_domain_process(domain1);
    check_domain1_state();
#endif
}

bool ECInterface::domainHasDigitalChange(const uint8_t *prev_domain, size_t prev_len) {
#ifdef USE_KERNEL_ETHERCAT
    if (!kernelBus || !kernelBus->isOpen() || !initialised || !active || !domain1_pd) {
        return false;
    }
    size_t domain_size = kernelBus->domainSize();
    if (domain_size == 0) {
        domain_size = data.getProcessDataSize();
    }
    if (domain_size == 0) {
        return false;
    }
    (void)prev_len; // dig_shadow_size; walk domain_size so first frame works
    return IOComponent::domainHasDigitalChange(domain1_pd, prev_domain, domain_size);
#elif !defined(EC_SIMULATOR)
    if (!master || !initialised || !active || !domain1) {
        return false;
    }
    size_t domain_size = ecrt_domain_size(domain1);
    uint8_t *pd = ecrt_domain_data(domain1);
    if (!pd || domain_size == 0) {
        return false;
    }
    (void)prev_len;
    return IOComponent::domainHasDigitalChange(pd, prev_domain, domain_size);
#else
    (void)prev_domain;
    (void)prev_len;
    return false;
#endif
}

size_t ECInterface::copyDomainData(uint8_t *dst, size_t dst_len) {
#ifdef USE_KERNEL_ETHERCAT
    if (!kernelBus || !kernelBus->isOpen() || !initialised || !active || !domain1_pd) {
        return 0;
    }
    size_t domain_size = kernelBus->domainSize();
    if (domain_size == 0) {
        domain_size = data.getProcessDataSize();
    }
    if (domain_size == 0) {
        return 0;
    }
    if (!dst) {
        return domain_size; // size query
    }
    size_t n = domain_size < dst_len ? domain_size : dst_len;
    memcpy(dst, domain1_pd, n);
    return n;
#elif !defined(EC_SIMULATOR)
    if (!master || !initialised || !active || !domain1) {
        return 0;
    }
    size_t domain_size = ecrt_domain_size(domain1);
    uint8_t *pd = ecrt_domain_data(domain1);
    if (!pd || domain_size == 0) {
        return 0;
    }
    if (!dst) {
        return domain_size;
    }
    size_t n = domain_size < dst_len ? domain_size : dst_len;
    memcpy(dst, pd, n);
    return n;
#else
    (void)dst;
    (void)dst_len;
    return 0;
#endif
}

int ECInterface::collectState() {
#ifdef USE_KERNEL_ETHERCAT
    if (kernelBus && kernelBus->isOpen()) {
        if (!initialised || !active || !domain1_pd) {
            return 0;
        }
        size_t domain_size = kernelBus->domainSize();
        if (domain_size == 0) {
            domain_size = data.getProcessDataSize();
        }
        if (domain_size == 0) {
            return 0;
        }
        uint8_t *pd_src = domain1_pd;
        unsigned int max = data.max_io_index;
        unsigned int min = data.min_io_index;
        if (domain_size < (size_t)max - min + 1 && max >= min) {
            return 0;
        }
        data.reallocate_update_data_and_mask(domain_size);
        int affected_bits = 0;
        uint8_t *last_pd = instance()->data.getProcessData();
        uint8_t *pm = data.getProcessMask();
        uint8_t *q = data.update_data;
        uint8_t *pd = pd_src;
        if (!pm) {
            return 0;
        }
        // Same diff algorithm as the legacy ecrt path: mask bits that changed
        // vs last image. update_data must be the full current domain (legacy
        // memcpy at end); ProcessingThread reads multi-bit values from it.
        for (unsigned int i = 0; i < domain_size; ++i) {
            data.update_mask[i] = 0;
            if (!last_pd) {
                data.update_data[i] = pd_src[i];
                data.update_mask[i] = *pm;
                ++affected_bits;
            }
            else if (last_pd[i] != pd_src[i]) {
                uint8_t bitmask = 0x01;
                while (bitmask) {
                    if (*pm & bitmask) {
                        if (((*pd) & bitmask) != ((last_pd[i]) & bitmask)) {
                            if ((*pd) & bitmask) {
                                *q |= bitmask;
                            }
                            else {
                                *q &= static_cast<uint8_t>(0xff - bitmask);
                            }
                            data.update_mask[i] |= bitmask;
                            ++affected_bits;
                        }
                    }
                    bitmask = static_cast<uint8_t>(bitmask << 1);
                }
            }
            ++pd;
            ++q;
            ++pm;
        }
        // Full image for CW (multi-bit values) — matches legacy ecrt path.
        // Without this, only sparse bit updates sat in update_data (zero-filled
        // reallocate) and multi-bit analogs/encoders were corrupted, causing
        // thrashing processAll work on every poll.
        memcpy(data.update_data, pd_src, domain_size);
        // Keep last domain image for the next diff; reuse buffer when possible.
        if (last_pd) {
            memcpy(last_pd, pd_src, domain_size);
        }
        else {
            uint8_t *copy = new uint8_t[domain_size];
            memcpy(copy, pd_src, domain_size);
            instance()->data.setDataSize(domain_size);
            instance()->data.setProcessData(copy, domain_size);
        }
        return affected_bits;
    }
#endif
    if (!master || !initialised || !active || !domain1) {
        std::cerr << "master not ready to collect state ";
        if (!master) {
            std::cerr << "(no master)";
        }
        else if (!initialised) {
            std::cerr << "(not initialised)";
        }
        else if (!active) {
            std::cerr << "(not active)";
        }
        else if (!domain1) {
            std::cerr << "(no domain)";
        }
        std::cerr << "\n";
        return 0;
    }
#ifndef EC_SIMULATOR

    DBG_ETHERCAT_CALLS << "ecrt_domain_size\n";
    size_t domain_size = ecrt_domain_size(domain1);
    DBG_ETHERCAT_CALLS << "ecrt_domain_data\n";
    uint8_t *domain1_pd = ecrt_domain_data(domain1);

    unsigned int max = data.max_io_index;
    unsigned int min = data.min_io_index;
    // we have seen the domain size have a value between zero and the expected max-min+1
    // this is to try to understand how that happens
    if (domain_size < (size_t)max - min + 1) {
        char buf[200];
        snprintf(buf, 200, "Warning: domain size %ld less than expected: %ld", domain_size,
                 (size_t)max - min + 1);
        MessageLog::instance()->add(buf);
        return 0;
    }
#if 0
    uint8_t *p = domain1_pd;
    for (unsigned int i = 0; i < domain_size; ++i) {
        std::cout << std::hex << std::setfill('0') << std::setw(2) << (int)*p++;
    }
    std::cout << std::dec << "\n";
#endif

    if (!domain1_pd) {
        assert(instance()->data.getProcessData() == 0);
        return 0;
    }
    uint8_t *pd = domain1_pd;
    if ((long)domain_size < 0) {
        return 0;
    }

    data.reallocate_update_data_and_mask(domain_size);

    int affected_bits = 0;
    // the result of the following is a list of data bits to be changed and
    // a mask indicating which bits are important

    // first time through, copy the domain process data to our local copy
    // and set the process mask to include every bit we care about

    // after that, look at all bits we care about and if the bit has changed
    // copy its new value to the update data and include the bit in the
    // update mask
    uint8_t *last_pd = instance()->data.getProcessData();
    uint8_t *pm = data.getProcessMask(); // these are the important bits
    uint8_t *q = data.update_data;       // convenience pointer

#if VERBOSE_DEBUG
    if (last_pd) {
        DBG_ETHERCAT_PACKETS << "last:";
        display(last_pd, domain_size);
    }
    DBG_ETHERCAT_PACKETS << "\ncurr:";
    display(pd, domain_size);
    DBG_ETHERCAT_PACKETS << "\n";
#endif

    assert(pm);
    assert(min == 0);
    for (unsigned int i = 0; i < domain_size; ++i) {
        data.update_mask[i] = 0;                 // assume no updates in this octet
        if (!last_pd) {                     // first time through, copy all the domain data and mask
            data.update_data[i] = domain1_pd[i]; //TBD & *pm;
            data.update_mask[i] = *pm;
            affected_bits++;
#if VERBOSE_DEBUG
            DBG_ETHERCAT_PACKETS << "init update data from process byte " << i << ": " << std::hex
                                 << (int)domain1_pd[i] << std::dec << "\n";
#endif
        }
        else if (last_pd[i] != domain1_pd[i]) {
            uint8_t bitmask = 0x01;
            int count = 0;
#if VERBOSE_DEBUG
            DBG_ETHERCAT_PACKETS << " offset " << i << " data 0x" << std::hex << (int)*pd
                                 << " (was " << (int)last_pd[i] << ")" << " process mask: 0x"
                                 << (int)*pm << std::dec << "\n";
#endif
            while (bitmask) {
                if (*pm & bitmask) { // we care about this bit
                    if (((*pd) & bitmask) != ((last_pd[i]) & bitmask)) { // changed
#if VERBOSE_DEBUG
                        DBG_ETHERCAT_PACKETS << "incoming bit " << i << ":" << count
                                             << " changed to " << (((*pd) & bitmask) ? 1 : 0)
                                             << "\n";
#endif
                        if ((*pd) & bitmask) {
                            *q |= bitmask;
                        }
                        else {
                            *q &= ((uint8_t)0xff - bitmask);
                        }
                        data.update_mask[i] |= bitmask;
                        ++affected_bits;
                    }
                }
                bitmask = bitmask << 1;
                ++count;
            }
        }
        ++pd;
        ++q;
        ++pm; //if (last_pd)++last_pd;
    }
#if 0
    if (affected_bits) {
        std::cout << "data: ";
        display(update_data, domain_size);
        std::cout << "\nmask: ";
        display(update_mask, domain_size);
        std::cout << " " << affected_bits << " bits changed (size=" << domain_size << ")\n";
    }
#endif

    // save the domain data for the next check
#if VERBOSE_DEBUG
    DBG_ETHERCAT_PACKETS << "setting process data\n";
#endif
    pd = new uint8_t[domain_size];
    memcpy(pd, domain1_pd, domain_size);
    instance()->data.setDataSize(domain_size);
    instance()->data.setProcessData(pd, domain_size);
#if VERBOSE_DEBUG
    DBG_ETHERCAT_PACKETS << "copied new domain data: ";
    display(pd, domain_size);
    DBG_ETHERCAT_PACKETS << "\n";
#endif
    memcpy(data.update_data, domain1_pd, domain_size);
#endif //EC_SIMULATOR

    return affected_bits;
}
void ECInterface::sendUpdates() {
    static uint64_t last_warning = 0;
    uint64_t now = microsecs();
#ifdef USE_KERNEL_ETHERCAT
    if (kernelBus && kernelBus->isOpen()) {
        if (!initialised || !active || !domain1_pd) {
            return;
        }
        size_t dsz = kernelBus->domainSize();
        if (dsz == 0) {
            dsz = data.getProcessDataSize();
        }
        if (dsz == 0) {
            return;
        }
        if (g_kernel_output_image.size() < dsz) {
            g_kernel_output_image.resize(dsz, 0);
            g_kernel_output_mask.resize(dsz, 0);
            g_kernel_pub_mask_valid = false;
            g_kernel_output_dirty = true;
        }

        // API 0.18 publish-renew: successful publishOutput refills remaining —
        // do not ioctl renew every tick. Legacy kernels without that CAP still
        // get a slow explicit renew so quiet (non-dirty) paths stay alive.
        if (g_output_lease_enabled && !g_output_lease_publish_renew) {
            static uint64_t last_lease_renew_us = 0;
            const uint64_t renew_period_us = 50000;
            if (last_lease_renew_us == 0 ||
                now - last_lease_renew_us >= renew_period_us) {
                last_lease_renew_us = now;
                int rret = kernelBus->renewOutputLease();
                if (rret != 0 && now - last_warning > 2000000) {
                    last_warning = now;
                    std::cerr << "elc_renew_output_lease failed ret=" << rret << "\n";
                }
            }
        }

        // Kernel RT task keeps applying the last published image while armed.
        // Only publish when the commanded shadow changed, or until a valid
        // domain still needs arm (e.g. secondary group recovered after drop).
        bool need_domain_rearm = false;
        if (g_domain_status_ok && kernelBus->hasDomainOutputAuthority()) {
            for (const ElcDomainSlot &slot : g_domains) {
                if (slot.valid && !slot.armed) {
                    need_domain_rearm = true;
                    break;
                }
            }
        }
        if (!g_kernel_output_dirty && g_kernel_outputs_armed && !need_domain_rearm) {
            last_update = now;
            return;
        }

        // Cache full-domain publish mask (process I/O | written bits | 0xff).
        if (!g_kernel_pub_mask_valid || g_kernel_pub_mask.size() != dsz) {
            g_kernel_pub_mask.assign(dsz, 0);
            uint8_t *proc_mask = IOComponent::getProcessMask();
            size_t proc_len = 0;
            if (proc_mask) {
                int max_off = IOComponent::getMaxIOOffset();
                if (max_off >= 0) {
                    proc_len = static_cast<size_t>(max_off) + 1;
                }
            }
            for (size_t i = 0; i < dsz; ++i) {
                uint8_t m = g_kernel_output_mask[i];
                if (proc_mask && i < proc_len) {
                    m = static_cast<uint8_t>(m | proc_mask[i]);
                }
                if (!m) {
                    m = 0xff;
                }
                g_kernel_pub_mask[i] = m;
            }
            g_kernel_pub_mask_valid = true;
        }

        struct elc_output_publish pub = {};
        int ret = kernelBus->publishOutput(g_kernel_output_image.data(), g_kernel_pub_mask.data(),
                                           dsz, &pub);
        if (ret == 0) {
            g_kernel_output_dirty = false;
            struct elc_io_status st = {};
            const bool got_io = (kernelBus->getIoStatus(&st) == 0);
            const bool link_up = got_io && st.link_up;

            // Prefer fresh domain status before arm decisions.
            if (link_up) {
                (void)refreshKernelDomainHealth(kernelBus.get(), false);
            }

            if (kernelBus->hasDomainOutputAuthority() && g_domain_status_ok) {
                // Stage 2: arm each healthy domain independently. Offline groups
                // stay disarmed; primary remains armable when valid.
                int last_err = 0;
                for (ElcDomainSlot &slot : g_domains) {
                    if (link_up && slot.valid && !slot.armed) {
                        int aret =
                            armKernelDomain(kernelBus.get(), slot.id, pub.config_generation,
                                            pub.output_sequence);
                        if (aret == 0) {
                            slot.armed = true;
                        }
                        else if (last_err == 0) {
                            last_err = aret;
                        }
                    }
                }
                g_kernel_outputs_armed = !g_domains.empty() && g_domains.front().armed;
                for (const ElcDomainSlot &slot : g_domains) {
                    if (slot.valid && !slot.armed) {
                        g_kernel_output_dirty = true; // retry arm with a fresh sequence
                        break;
                    }
                }
                if (last_err != 0 && now - last_warning > 2000000) {
                    last_warning = now;
                    std::cerr << "elc_arm_output (per-domain) failed: " << last_err
                              << " domains=" << g_domains.size()
                              << " primary_valid="
                              << (g_domains.empty() ? 0 : (int)g_domains.front().valid)
                              << " primary_armed="
                              << (g_domains.empty() ? 0 : (int)g_domains.front().armed)
                              << " link=" << (int)link_up << " agg_faults=0x" << std::hex
                              << (got_io ? st.current_faults : 0) << std::dec << "\n";
                }
                else if (!g_domains.empty() && !g_domains.front().valid &&
                         now - last_warning > 2000000) {
                    last_warning = now;
                    std::cerr << "outputs published; primary domain not valid yet (link="
                              << (int)link_up
                              << " agg_healthy=" << (got_io ? (int)st.bus_healthy : -1)
                              << " faults=0x" << std::hex << (got_io ? st.current_faults : 0)
                              << std::dec << ")\n";
                }
            }
            else {
                // Compatibility: single authority / no multi-domain status.
                const bool healthy = got_io && st.bus_healthy && st.link_up;
                if (healthy) {
                    if (!st.outputs_armed || !g_kernel_outputs_armed) {
                        int aret =
                            armKernelDomain(kernelBus.get(), 0, pub.config_generation,
                                            pub.output_sequence);
                        if (aret == 0) {
                            g_kernel_outputs_armed = true;
                        }
                        else if (now - last_warning > 2000000) {
                            last_warning = now;
                            std::cerr << "elc_arm_output failed: " << aret
                                      << " (faults=0x" << std::hex << st.current_faults
                                      << std::dec << " latched=0x" << st.last_latched_faults
                                      << " rearm=" << (int)st.rearm_required << ")\n";
                        }
                    }
                    else {
                        g_kernel_outputs_armed = true;
                    }
                }
                else {
                    g_kernel_outputs_armed = false;
                    g_kernel_output_dirty = true; // retry when healthy
                    if (now - last_warning > 2000000) {
                        last_warning = now;
                        std::cerr << "outputs published but not armed yet (bus_healthy="
                                  << (got_io ? (int)st.bus_healthy : -1)
                                  << " link=" << (got_io ? (int)st.link_up : -1)
                                  << " faults=0x" << std::hex
                                  << (got_io ? st.current_faults : 0)
                                  << " latched=0x" << (got_io ? st.last_latched_faults : 0)
                                  << std::dec;
                        if (got_io) {
                            std::cerr << " op_slaves=" << st.configured_slaves_operational
                                      << "/" << st.configured_slave_count;
                            if (st.current_faults & 0x20u) {
                                std::cerr << " DOMAIN_INCOMPLETE";
                            }
                            if (st.current_faults & 0x10u) {
                                std::cerr << " SLAVE_NOT_OP";
                            }
                            if (st.current_faults & 0x40u) {
                                std::cerr << " CONTROLLER_STALE";
                            }
                        }
                        struct elc_cycle_status cst = {};
                        if (kernelBus->cycleStatus(&cst) == 0) {
                            std::cerr << " wc=" << cst.working_counter
                                      << " wc_state=" << (unsigned)cst.working_counter_state
                                      << " overruns=" << cst.cycle_overrun_count
                                      << " cyc_err=" << cst.cycle_error_count
                                      << " max_late_ns=" << cst.maximum_lateness_ns;
                        }
                        std::cerr << ")\n";
                        if (got_io && (st.current_faults & 0x20u)) {
                            std::cerr << "hint: DOMAIN_INCOMPLETE usually means the elc_cycle "
                                         "kthread missed the bus window — load with "
                                         "insmod … cycle_cpu=1 cycle_fifo_priority=90\n";
                        }
                    }
                }
            }
        }
        else if (now - last_warning > 2000000) {
            last_warning = now;
            std::cerr << "elc_publish_output failed: " << ret << " (dsz=" << dsz << ")\n";
        }
        last_update = now;
        return;
    }
#endif
    if (!master || !initialised || !active || !all_ok || !domain1) {
        if (now + 5000000 < last_warning) {
            std::cerr << "master not ready to send updates\n" << std::flush;
            char buf[100];
            snprintf(buf, 100, "EtherCAT master is not ready to send updates\n");
            MessageLog::instance()->add(buf);
            DBG_ETHERCAT_PACKETS << buf << "\n";
        }
        return;
    }

    if (keep_stats) {
        uint64_t t = microsecs();
        int64_t dt = t - last_receive;
        if (last_receive != 0) {
            recv_to_update.add(dt);
        }
        last_update = t;
        if (recv_to_update.getCount() >= 1000) {
            recv_to_update.report(std::cout);
            recv_to_update.reset();
        }

    }

#ifndef EC_SIMULATOR
#ifdef USE_DC
    queueDistributedClockSync();
#endif
    DBG_ETHERCAT_CALLS << "ecrt_domain_queue\n";
    ecrt_domain_queue(domain1);
    DBG_ETHERCAT_CALLS << "ecrt_master_send\n";
    ecrt_master_send(master);
#endif
}
#endif

/*****************************************************************************/

void ECInterface::check_domain1_state(void) {
#ifndef EC_SIMULATOR
    ec_domain_state_t ds;
    memset(&ds, 0, sizeof(ec_domain_state_t));

    DBG_ETHERCAT_CALLS << "ecrt_domain_state\n";
    ecrt_domain_state(domain1, &ds);

#if 0
    if (ds.working_counter != domain1_state.working_counter) {
        std::cout << "Domain1: WC " << ds.working_counter << "\n";
    }
    if (ds.wc_state != domain1_state.wc_state) {
        std::cout << "Domain1: State " << ds.wc_state << ".\n";
    }
#endif

    domain1_state = ds;
#endif
}

/*****************************************************************************/

void ECInterface::check_master_state(void) {
#ifndef EC_SIMULATOR
    uint64_t now = microsecs();

    ec_master_state_t ms;
    memset(&ms, 0, sizeof(ec_master_state_t));

#if 0
    // obtain master state info and reports number of slaves if
    // all slaves are not yet responding
    int res = ecrt_master(master, &master_info);
    if (res < 0) {
        master_info_time = 0;
    }
    else {
        master_info_time = now;
        if (master_info.slave_count != expected_slaves) {
            std::cout << "EtherCAT slaves on bus: " << master_info.slave_count;
        }
    }
#endif

    //DBG_ETHERCAT_CALLS << "ecrt_master_state\n";
    ecrt_master_state(master, &ms);

    if (ms.slaves_responding != master_state.slaves_responding) {
        master_state_changed = now;
        std::cout << ms.slaves_responding << " slave(s)\n";
        char buf[100];
        if (ms.slaves_responding >= expected_slaves) {
            snprintf(buf, 100, "Number of slaves has changed from %d to %d",
                     master_state.slaves_responding, ms.slaves_responding);
        }
        else {
            snprintf(buf, 100, "Number of slaves %d less than expected %d", ms.slaves_responding,
                     expected_slaves);
        }
        MessageLog::instance()->add(buf);
        std::cout << buf << "\n";
    }
    if (ms.slaves_responding != master_state.slaves_responding ||
        ms.slaves_responding < expected_slaves) {
        if (ms.slaves_responding > expected_slaves) {
            expected_slaves = ms.slaves_responding;
        }
        else {
            if (failure_tolerance && ++failure_count > *failure_tolerance) {
                all_ok = false; // lost a slave
#if 0
                if (ethercat_status) {
                    SetStateActionTemplate ssat = SetStateActionTemplate("SELF", "ERROR");
                    SetStateAction *ssa = dynamic_cast<SetStateAction *>(ssat.factory(ethercat_status));
                    ethercat_status->enqueueAction(ssa);
                }
#endif
            }
        }
        ethercat_status = MachineInstance::find("ETHERCAT");
        if (ethercat_status) {
            ethercat_status->setValue("slave_count", Value{(uint64_t)ms.slaves_responding});
        }
    }
    if (ms.al_states != master_state.al_states) {
        master_state_changed = now;
        std::cout << "AL states: 0x" << std::ios::hex << ms.al_states << std::ios::dec << "\n";
        char buf[100];
        snprintf(buf, 100, "EtherCAT state change: was 0x%04X now 0x%04X", master_state.al_states,
                 ms.al_states);
        MessageLog::instance()->add(buf);
        std::cout << buf << "\n";
        ethercat_status = MachineInstance::find("ETHERCAT");
        if (ethercat_status) {
            const Value &states_v = ethercat_status->getValue("slave_states");
            ethercat_status->setValue("slave_states", Value{(uint64_t)ms.al_states});
        }
    }
    if (master_was_running && ms.al_states != 0x8) {
        if (failure_tolerance && all_ok && ++failure_count > *failure_tolerance) {
            all_ok = false;
#if 0
            if (ethercat_status) {
                SetStateActionTemplate ssat = SetStateActionTemplate("SELF", "ERROR");
                SetStateAction *ssa = dynamic_cast<SetStateAction *>(ssat.factory(ethercat_status));
                ethercat_status->enqueueAction(ssa);
            }
#endif
        }
    }
    else if (ms.al_states == 0x8) {
        master_was_running = true;
    }

    // log all changes of link state
    if (ms.link_up != master_state.link_up) {
        master_state_changed = now;
        std::cout << "Link is " << (ms.link_up ? "up" : "down") << "\n";
        char buf[100];
        snprintf(buf, 100, "EtherCAT link state change was %s now %s",
                 master_state.link_up ? "up" : "down", ms.link_up ? "up" : "down");
        MessageLog::instance()->add(buf);
        std::cout << buf << "\n";

#if 1
        // copy link state change through to clockwork
        MachineInstance *link_status = MachineInstance::find("ETHERCAT_LS");
        if (link_status) {
            if (!link_status->enabled()) {
                link_status->enable();
            }
            const char *state = ms.link_up ? "UP" : "DOWN";
            SetStateActionTemplate ssat = SetStateActionTemplate("SELF", Value{state});
            SetStateAction *ssa = dynamic_cast<SetStateAction *>(ssat.factory(link_status));
            link_status->enqueueAction(ssa);

            char buf[100];
            snprintf(buf, 100, "Info: asked link status machine to change to state %s", state);
            MessageLog::instance()->add(buf);
            std::cout << buf << "\n";
        }
        else {
            char buf[100];
            snprintf(buf, 100, "Warning: no machine found to track EtherCAT link status");
            MessageLog::instance()->add(buf);
            std::cout << buf << "\n";
        }
#endif
    }
    // if the link changes state or if it was once up but is now down, check state
    if (ms.link_up != master_state.link_up || (link_was_up && !ms.link_up)) {
        if (!ms.link_up) {
            if (failure_tolerance && all_ok && ++failure_count > *failure_tolerance) {
                all_ok = false;
#if 0
                if (ethercat_status) {
                    SetStateActionTemplate ssat = SetStateActionTemplate("SELF", "ERROR");
                    SetStateAction *ssa = dynamic_cast<SetStateAction *>(ssat.factory(ethercat_status));
                    ethercat_status->enqueueAction(ssa);
                }
#endif
            }
#if 0
            if (master) {
                SetStateActionTemplate ssat = SetStateActionTemplate("SELF", "DISCONNECTED");
                SetStateAction *ssa = dynamic_cast<SetStateAction *>(ssat.factory(ethercat_status));
                ethercat_status->enqueueAction(ssa);
            }
#endif
        }
        else { // assume that now the link is back up we can reset the errors
            failure_count = 0;
            link_was_up = true;
            //ecrt_master_deactivate(master);
            //MasterDevice m(0);
            //m.open(MasterDevice::ReadWrite);
            //m.rescan();

            // if the master was active before the link went down,
            // automatically activate it again (with reset) now the link is back
            if (active) {
                char buf[100];
                snprintf(buf, 100, "Automatically reactivating the EtherCAT master");
                MessageLog::instance()->add(buf);
                DBG_MSG << buf << "\n";
                activate();
            }
            else {
                std::cout << "not activating master on link up\n";
            }
        }
    }

    master_state = ms;
    master_last_checked = now;
#endif
}

/*****************************************************************************/

#ifndef EC_SIMULATOR
void ECInterface::report_module_state_change(ECModule *m, int i) {
#ifdef USE_KERNEL_ETHERCAT
    if (!m || !kernelBus || !kernelBus->isOpen()) {
        return;
    }

    const int BUFSIZE = 200;
    char buf[BUFSIZE];
    ec_slave_config_state_t s = {};
    bool got = false;

    // Prefer configured-slave status once topology is applied (live AL while cycling).
    if (m->elc_config_id != 0) {
        struct elc_config_slave_status st = {};
        int ret = kernelBus->getConfigSlaveStatus(m->elc_config_id, &st);
        if (ret == 0 && st.active && st.state_result == 0) {
            s.online = st.online ? 1 : 0;
            s.operational = st.operational ? 1 : 0;
            s.al_state = st.al_state;
            got = true;
        }
        else if (ret == 0 && st.active && st.state_result != 0) {
            // Query glitch only — do NOT force offline. Transient state_result
            // failures used to mark every slave offline and storm SDO
            // recommission (mailbox blocks → output lease expiry → SM WD).
            got = false;
        }
        // ret==0 && !st.active → fall through to discovery AL (PREOP path for STARTUP).
    }

    // Before cycle activate (or no config_id): discovery AL from bus position.
    if (!got) {
        struct elc_slave_info info = {};
        int ret = kernelBus->getSlaveInfo(m->position, &info);
        if (ret == 0) {
            // Discovery path has no separate online bit; treat non-zero AL as online
            // so ControlSystemMachine does not map modules to ERROR.
            s.al_state = info.al_state;
            s.online = (info.al_state != 0 && !info.error_flag) ? 1 : 0;
            s.operational = (info.al_state == 8) ? 1 : 0;
            got = true;
        }
    }

    if (!got) {
        // Keep previous snapshot if the ioctl fails this cycle.
        if (!m->slave_config_state.online) {
            ++slaves_not_operational;
            ++slaves_offline;
        }
        else if (!m->slave_config_state.operational) {
            ++slaves_not_operational;
        }
        return;
    }

    if (!s.online) {
        ++slaves_not_operational;
        ++slaves_offline;
    }
    else if (!s.operational) {
        ++slaves_not_operational;
    }

    if (s.al_state != m->slave_config_state.al_state) {
        DBG_ETHERCAT << "kernel ecat: " << m->name << ": AL 0x" << std::hex
                     << (unsigned)m->slave_config_state.al_state << " -> 0x"
                     << (unsigned)s.al_state << std::dec << "\n";
        snprintf(buf, BUFSIZE, "Slave %d (%s) AL 0x%x -> 0x%x (kernel)", i, m->name.c_str(),
                 m->slave_config_state.al_state, s.al_state);
        MessageLog::instance()->add(buf);
        std::cout << buf << "\n";
    }
    if (s.online != m->slave_config_state.online) {
        snprintf(buf, BUFSIZE, "Slave %d (%s) online %s -> %s (kernel)", i, m->name.c_str(),
                 m->slave_config_state.online ? "yes" : "no", s.online ? "yes" : "no");
        MessageLog::instance()->add(buf);
        std::cout << buf << "\n";
#ifdef USE_SDO
        // Recommission only on return (offline→online) after we have seen the
        // module online once. Cold start uses the normal first-read/default path.
        // Never storm on the offline edge or on query glitches.
        if (s.online && !m->slave_config_state.online) {
            if (m->sdo_seen_online) {
                SDOEntry::recommissionModule(m, microsecs());
            }
            m->sdo_seen_online = true;
        }
#endif
    }
    if (s.operational != m->slave_config_state.operational) {
        snprintf(buf, BUFSIZE, "Slave %d (%s) operational %s -> %s (kernel)", i,
                 m->name.c_str(), m->slave_config_state.operational ? "yes" : "no",
                 s.operational ? "yes" : "no");
        MessageLog::instance()->add(buf);
        std::cout << buf << "\n";
    }

    m->slave_config_state = s;
#else
    ec_slave_config_state_t s;
    const int BUFSIZE = 200;
    char buf[BUFSIZE];

    // check for errors
    uint8_t errbuf[EC_COE_EMERGENCY_MSG_SIZE];
    int res = ecrt_slave_config_emerg_pop(m->slave_config, errbuf);
    if (res == 0) {
        char buf[200];
        snprintf(buf, 200, "Slave %d (%s) reported error: %0x%0x%0x%0x %0x%0x%0x%0x", i,
                 m->name.c_str(), errbuf[0], errbuf[1], errbuf[2], errbuf[3], errbuf[4],
                 errbuf[5], errbuf[6], errbuf[7]);
        MessageLog::instance()->add(buf);
    }

    ecrt_slave_config_state(m->slave_config, &s);
    if (!s.online) {
        ++slaves_not_operational;
        ++slaves_offline;
    }

    if (s.al_state != m->slave_config_state.al_state) {
        DBG_ETHERCAT << "ecat_thread: " << m->name << ": State 0x" << std::ios::hex
                     << s.al_state << ".\n";
        snprintf(buf, BUFSIZE, "Slave %d (%s) changed state was 0x%x now 0x%x", i,
                 m->name.c_str(), m->slave_config_state.al_state, s.al_state);
        MessageLog::instance()->add(buf);
    }
    if (s.online != m->slave_config_state.online) {
        DBG_ETHERCAT << "ecat_thread: " << m->name << ": " << (s.online ? "online" : "offline")
                  << "\n";
        snprintf(buf, BUFSIZE, "Slave %d (%s) changed online state: was %s, now %s", i,
                 m->name.c_str(), m->slave_config_state.online ? "online" : "offline",
                 s.online ? "online" : "offline");
        MessageLog::instance()->add(buf);
#ifdef USE_SDO
        if (s.online && !m->slave_config_state.online) {
            if (m->sdo_seen_online) {
                SDOEntry::recommissionModule(m, microsecs());
            }
            m->sdo_seen_online = true;
        }
#endif
    }
    if (s.operational != m->slave_config_state.operational) {
        DBG_ETHERCAT << m->name << ": " << (s.operational ? "" : "Not ") << "operational\n";
        snprintf(
            buf, BUFSIZE,
            "Slave %d (%s) changed operational state: was %s operational, now %s operational",
            i, m->name.c_str(), m->slave_config_state.operational ? "" : "not ",
            s.operational ? "" : "not ");
        MessageLog::instance()->add(buf);
    }

    m->slave_config_state = s;
#endif
}
#endif

void ECInterface::check_slave_config_states(void) {
#ifndef EC_SIMULATOR
    boost::recursive_mutex::scoped_lock lock(modules_mutex);
    std::vector<ECModule *>::iterator iter = modules.begin();
    int i = 0;
    slaves_not_operational = 0;
    slaves_offline = 0;
#ifdef USE_KERNEL_ETHERCAT
    unsigned int al_or_all = 0;
    unsigned int al_or_primary = 0;
    unsigned int modules_seen = 0;
    unsigned int modules_op = 0;
    unsigned int modules_primary = 0;
    unsigned int modules_primary_op = 0;
    bool saw_domain_ids = false;
    // Per-domain AL OR (aligned with g_domains slots).
    std::vector<unsigned int> al_per_domain(g_domains.size(), 0);
#endif
    while (iter != modules.end()) {
        ECModule *m = *iter++;
        if (!m) {
            char buf[100];
            snprintf(buf, 100, "null module in module list at position %d", i);
            MessageLog::instance()->add(buf);
            std::cout << buf << "\n";
            assert(m != 0);
        }
#ifdef USE_KERNEL_ETHERCAT
        // Kernel path has no ecrt slave_config; poll real AL from libelcethercat.
        if (kernelBus && kernelBus->isOpen()) {
            report_module_state_change(m, i);
            const unsigned al = (m->slave_config_state.al_state & 0x0f);
            al_or_all |= al;
            if (m->elc_domain_id != 0) {
                saw_domain_ids = true;
            }
            // Primary = first declared domain (or unassigned → primary for legacy).
            const bool is_primary =
                m->elc_domain_id == 0 ||
                (g_primary_domain_id != 0 && m->elc_domain_id == g_primary_domain_id) ||
                (g_domains.empty());
            if (is_primary) {
                al_or_primary |= al;
                ++modules_primary;
                if (m->slave_config_state.operational || al == 8) {
                    ++modules_primary_op;
                }
            }
            for (size_t di = 0; di < g_domains.size(); ++di) {
                if (m->elc_domain_id == g_domains[di].id ||
                    (m->elc_domain_id == 0 && di == 0)) {
                    al_per_domain[di] |= al;
                }
            }
            ++modules_seen;
            if (m->slave_config_state.operational || al == 8) {
                ++modules_op;
            }
            ++i;
            continue;
        }
#endif
        if (!m->slave_config) {
            //std::cout << "module " << m->name << " not active yet..skipping\n";
            continue;
        }
        report_module_state_change(m, i);
        ++i;
    }
#ifdef USE_KERNEL_ETHERCAT
    if (kernelBus && kernelBus->isOpen() && modules_seen > 0) {
        // Multi-domain: ETHERCAT.slave_states = OR of *primary* domain AL only so
        // M_Startup does not SHUTDOWN when an isolatable group leaves OP.
        // Per-domain AL is on ECDomain_<id>.slave_states.
        const unsigned int al_for_startup =
            saw_domain_ids ? al_or_primary : al_or_all;
        master_state.al_states = al_for_startup;
        bool al_changed = false;
        for (size_t di = 0; di < g_domains.size(); ++di) {
            if (g_domains[di].slave_states != al_per_domain[di]) {
                al_changed = true;
            }
            g_domains[di].slave_states = al_per_domain[di];
        }
        if (ethercat_status) {
            ethercat_status->setValue("slave_states",
                                      Value{static_cast<uint64_t>(al_for_startup)});
            ethercat_status->setValue("all_slave_states",
                                      Value{static_cast<uint64_t>(al_or_all)});
        }
        // Push per-domain AL onto ECDomain_* promptly (do not wait for next WC poll).
        if (al_changed) {
            publishKernelEthercatClockworkMachines();
        }
        static unsigned last_log_al = 0xffffffffu;
        static unsigned last_log_op = 0xffffffffu;
        if (al_for_startup != last_log_al || modules_op != last_log_op) {
            std::cout << "Kernel slave AL startup(primary)=0x" << std::hex << al_for_startup
                      << " all=0x" << al_or_all << std::dec
                      << " modules_op=" << modules_op << "/" << modules_seen
                      << " primary_op=" << modules_primary_op << "/" << modules_primary
                      << " domains=" << g_domains.size()
                      << " offline=" << slaves_offline << "\n";
            last_log_al = al_for_startup;
            last_log_op = modules_op;
        }
    }
#endif
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
        std::cerr << "Failed to install signal handler!\n";
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

// in a real environment we can look for devices on the bus

ec_pdo_entry_info_t *c_entries = 0;
ec_pdo_info_t *c_pdos = 0;
ec_sync_info_t *c_syncs = 0;
EntryDetails *c_entry_details = 0;

#ifndef USE_KERNEL_ETHERCAT
cJSON *generateSlaveCStruct(ec_master_t *m, ECModule *xml_module, const ec_slave_info_t &slave,
                            bool reconfigure) {
    unsigned int i, j, k, pdo_pos = 0, entry_pos = 0;

    const unsigned int estimated_max_entries = 128;
    const unsigned int estimated_max_pdos = 32;
    const unsigned int estimated_max_syncs = 32;
    unsigned int total_entries = 0, total_pdos = 0, total_syncs = 0;

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "position", slave.position);
    cJSON_AddNumberToObject(root, "vendor_id", slave.vendor_id);
    cJSON_AddNumberToObject(root, "product_code", slave.product_code);
    cJSON_AddNumberToObject(root, "revision_number", slave.revision_number);
    cJSON_AddNumberToObject(root, "alias", slave.alias);
    cJSON_AddNumberToObject(root, "drawn_current", slave.current_on_ebus);
    cJSON_AddStringToObject(root, "tab", "Modules");
    cJSON_AddStringToObject(root, "class", "MODULE");
    cJSON_AddNumberToObject(root, "error_flag", slave.error_flag);
    bool isEL2535 = false;
    {
        std::string name(slave.name);
        int name_len = name.length();
        for (int i = 0; i < name_len; ++i) {
            name[i] &= 127;
        }
        cJSON_AddStringToObject(root, "name", name.c_str());
        if (name.substr(6) == "EL2535") {
            isEL2535 = true;
        }
    }

    /*
     * Legacy Clockwork entry positions refer to the configured PDO entry
     * array.  This is not necessarily identical to the master's discovery
     * view: EtherLab may coalesce adjacent 0x0000:00 alignment entries.
     * Export the configured view separately so conversion and audit tools do
     * not mistake discovery ordinals for configured ordinals.
     */
    if (xml_module && xml_module->syncs && xml_module->pdo_entries) {
        cJSON *json_configured_syncs = cJSON_CreateArray();
        unsigned int configured_entry_pos = 0;
        for (i = 0; i < xml_module->sync_count; ++i) {
            const ec_sync_info_t &sync = xml_module->syncs[i];
            cJSON *json_sync = cJSON_CreateObject();
            cJSON_AddNumberToObject(json_sync, "index", sync.index);
            cJSON_AddStringToObject(json_sync, "direction",
                                    sync.dir == EC_DIR_OUTPUT ? "Output" : "Input");
            cJSON *json_pdos = cJSON_CreateArray();
            for (j = 0; j < sync.n_pdos; ++j) {
                const ec_pdo_info_t &pdo = sync.pdos[j];
                cJSON *json_pdo = cJSON_CreateObject();
                cJSON_AddNumberToObject(json_pdo, "index", pdo.index);
                cJSON_AddNumberToObject(json_pdo, "entry_count", pdo.n_entries);
                cJSON *json_entries = cJSON_CreateArray();
                for (k = 0; k < pdo.n_entries; ++k, ++configured_entry_pos) {
                    const ec_pdo_entry_info_t &entry = pdo.entries[k];
                    cJSON *json_entry = cJSON_CreateObject();
                    cJSON_AddNumberToObject(json_entry, "pos", configured_entry_pos);
                    cJSON_AddNumberToObject(json_entry, "index", entry.index);
                    cJSON_AddNumberToObject(json_entry, "subindex", entry.subindex);
                    cJSON_AddNumberToObject(json_entry, "bit_length", entry.bit_length);
                    if (xml_module->entry_details &&
                        configured_entry_pos < xml_module->num_entries) {
                        cJSON_AddStringToObject(
                            json_entry, "name",
                            xml_module->entry_details[configured_entry_pos].name.c_str());
                    }
                    cJSON_AddItemToArray(json_entries, json_entry);
                }
                cJSON_AddItemToObject(json_pdo, "entries", json_entries);
                cJSON_AddItemToArray(json_pdos, json_pdo);
            }
            cJSON_AddItemToObject(json_sync, "pdos", json_pdos);
            cJSON_AddItemToArray(json_configured_syncs, json_sync);
        }
        cJSON_AddItemToObject(root, "configured_sync_managers", json_configured_syncs);
    }

    if (slave.sync_count) {
        // add pdo entries for this slave
        // note the assumptions here about the maximum number of entries, pdos and syncs we expect
        const int c_entries_size = sizeof(ec_pdo_entry_info_t) * estimated_max_entries;
        c_entries = new ec_pdo_entry_info_t[c_entries_size];
        memset(c_entries, 0, c_entries_size);

        c_entry_details = new EntryDetails[estimated_max_entries];

        const int c_pdos_size = sizeof(ec_pdo_info_t) * estimated_max_pdos;
        c_pdos = new ec_pdo_info_t[c_pdos_size];
        memset(c_pdos, 0, c_pdos_size);

        const int c_syncs_size = sizeof(ec_sync_info_t) * estimated_max_syncs;
        c_syncs = new ec_sync_info_t[c_syncs_size];
        memset(c_syncs, 0, c_syncs_size);

        total_syncs += slave.sync_count;
        assert(total_syncs < estimated_max_syncs);
        cJSON *json_syncs = cJSON_CreateArray();
        for (i = 0; i < slave.sync_count; i++) {
            cJSON *json_sync = cJSON_CreateObject();
            DBG_ETHERCAT_CALLS << "ecrt_master_get_sync_manager\n";
            int rc = ecrt_master_get_sync_manager(m, slave.position, i, &c_syncs[i]);
            assert(rc == 0);
            char index_str[40];
            char pdo_name[40];
            snprintf(index_str, 40, "0x%04X (%d)", c_syncs[i].index, c_syncs[i].index);
            cJSON_AddStringToObject(json_sync, "index", index_str);
            cJSON_AddStringToObject(json_sync, "direction",
                                    (c_syncs[i].dir == EC_DIR_OUTPUT) ? "Output" : "Input");

            // Copy pdo entries to c_pdos
            if (!c_syncs[i].n_pdos) {
                c_syncs[i].pdos = 0;
            }
            else {
                ec_pdo_info_t pdo = {};
                unsigned int pdo_count = c_syncs[i].n_pdos;
                c_syncs[i].pdos = c_pdos + pdo_pos;
                total_pdos += pdo_count;
                assert(total_pdos < estimated_max_pdos);
                cJSON *json_pdos = cJSON_CreateArray();
                if (isEL2535 && i == 3 && pdo_count == 2) {
                    std::cerr << "******* detected EL2535 with 2 pdos (need 4)\n";
                    std::flush(std::cerr);
                    // assert(false); // TODO: what is the above comment about?
                }
                for (j = 0; j < pdo_count; j++) {
                    DBG_ETHERCAT_CALLS << "ecrt_master_get_pdo(..., sm: " << i << ", pdo: " << j
                                       << ")" << "\n";
                    ecrt_master_get_pdo(m, slave.position, i, j, &pdo);
                    cJSON *json_pdo = cJSON_CreateObject();
                    snprintf(index_str, 40, "0x%04X (%d)", pdo.index, pdo.index);
                    cJSON_AddStringToObject(json_pdo, "index", index_str);
                    cJSON_AddNumberToObject(json_pdo, "entry_count", pdo.n_entries);
                    snprintf(pdo_name, 40, "pdo-%04X", pdo.index);
                    cJSON_AddStringToObject(json_pdo, "name", pdo_name); // TODO: Find the name
                    DBG_ETHERCAT << "sync: " << i << " pdo: " << j << " " << ": ";
                    c_pdos[j + pdo_pos].index = pdo.index;
                    c_pdos[j + pdo_pos].n_entries = (unsigned int)pdo.n_entries;
                    if (!pdo.n_entries) {
                        c_pdos[j + pdo_pos].entries = 0;
                    }
                    else {
                        c_pdos[j + pdo_pos].entries = c_entries + entry_pos;
                        cJSON *json_entries = cJSON_CreateArray();
                        total_entries += pdo.n_entries;
                        assert(total_entries < estimated_max_entries);
                        ec_pdo_entry_info_t entry = {};
                        for (k = 0; k < pdo.n_entries; k++) {
                            cJSON *json_entry = cJSON_CreateObject();

                            DBG_ETHERCAT_CALLS << "ecrt_master_get_pdo_entry\n";
                            ecrt_master_get_pdo_entry(m, slave.position, i, j, k, &entry);
                            char entry_name[40];
                            if (xml_module && xml_module->entry_details &&
                                entry_pos < xml_module->num_entries &&
                                xml_module->pdo_entries[entry_pos].index == entry.index &&
                                xml_module->pdo_entries[entry_pos].subindex == entry.subindex &&
                                xml_module->pdo_entries[entry_pos].bit_length ==
                                    entry.bit_length) {
                                snprintf(entry_name, 40, "%s",
                                         xml_module->entry_details[entry_pos].name.c_str());
                            }
                            else {
                                snprintf(entry_name, 40, "entry-%X-%X", entry.index,
                                         entry.subindex);
                            }
                            DBG_ETHERCAT << " entry: " << k << "{" << entry_pos << ", " << std::hex
                                         << (int)entry.index << ", " << std::dec
                                         << (int)entry.subindex << ", " << (int)entry.bit_length
                                         << ", " << entry_name << "}";
                            c_entries[entry_pos].index = entry.index;
                            c_entries[entry_pos].subindex = entry.subindex;
                            c_entries[entry_pos].bit_length = entry.bit_length;
                            c_entry_details[entry_pos].name = pdo_name;
                            c_entry_details[entry_pos].name += " ";
                            c_entry_details[entry_pos].name += entry_name;
                            c_entry_details[entry_pos].entry_index = entry_pos;
                            c_entry_details[entry_pos].pdo_index = j + pdo_pos;
                            c_entry_details[entry_pos].sm_index = i;

                            cJSON_AddNumberToObject(json_entry, "pos", entry_pos);
                            snprintf(index_str, 40, "0x%04X (%d)", entry.index, entry.index);
                            cJSON_AddStringToObject(json_entry, "index", index_str);
                            cJSON_AddStringToObject(json_entry, "name",
                                                    c_entry_details[entry_pos].name.c_str());
                            cJSON_AddNumberToObject(json_entry, "subindex", entry.subindex);
                            cJSON_AddNumberToObject(json_entry, "bit_length", entry.bit_length);
                            ++entry_pos;

                            cJSON_AddItemToArray(json_entries, json_entry);
                        }
                        cJSON_AddItemToObject(json_pdo, "entries", json_entries);
                    }
                    DBG_ETHERCAT << "\n";
                    cJSON_AddItemToArray(json_pdos, json_pdo);
                }
                cJSON_AddItemToObject(json_sync, "pdos", json_pdos);
            }
            pdo_pos += c_syncs[i].n_pdos;
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
    if (c_entries) {
        delete[] c_entries;
#warning removed what i think is dead code
#if 0
    if (reconfigure) {
                DBG_ETHERCAT << "defining module " << slave.name << " sync_count: "
                                << (int)slave.sync_count << " num entries: " << total_entries << "\n";
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
        auto res = ECInterface::instance()->addModule(module, reconfigure);
        if (!res) {
            delete module; // module may be already registered
            std::cerr << "Failed to add module " << slave.name << " " << res.error() << "\n";
        }
#endif
    }
    if (c_pdos) {
        delete[] c_pdos;
    }
    if (c_syncs) {
        delete[] c_syncs;
    }
    //delete[] c_entry_details;

    return root;
}
#endif // !USE_KERNEL_ETHERCAT


#ifdef USE_KERNEL_ETHERCAT
char *collectSlaveConfig(bool reconfigure) {
    // Kernel transport (): simple stub. Full slave config/PDO mapping in Phase 9.
    // This allows iod.sh and IODCommandGetSlaveConfig to work without ecrt calls.
    cJSON *root = cJSON_CreateArray();
    cJSON *entry = cJSON_CreateObject();
    cJSON_AddStringToObject(entry, "status", "kernel-transport");
    cJSON_AddStringToObject(entry, "note", "discovery + SDO mailbox only (outputs in Phase 11)");
    cJSON_AddItemToArray(root, entry);
    char *json = cJSON_Print(root);
    cJSON_Delete(root);
    return json;
}
#else
char *collectSlaveConfig(bool reconfigure) {
#if 1
    cJSON *root = cJSON_CreateArray();
    unsigned int pos = 0;
    int res = 0;
    ec_master_info_t master_info;
    DBG_ETHERCAT_CALLS << "ecrt_master\n";
    res = ecrt_master(ECInterface::master, &master_info);
    while (res >= 0 && pos < master_info.slave_count) {
        ECModule *module = ECInterface::findModule(pos);
        {
            ec_slave_info_t slave_info;
            memset(&slave_info, 0, sizeof(ec_slave_info_t));
            DBG_ETHERCAT_CALLS << "ecrt_master_get_slave\n";
            res = ecrt_master_get_slave(ECInterface::master, pos, &slave_info);

            DBG_ETHERCAT << "generating JSON slave description for: " << "pos: "
                         << slave_info.position << ", " << "syncs: " << slave_info.sync_count
                         << ", " << "sdos: " << slave_info.sdo_count << ")\n";
            cJSON_AddItemToArray(
                root, generateSlaveCStruct(ECInterface::master, module, slave_info, reconfigure));
        }
        ++pos;
    }
#else

    cJSON *root = cJSON_CreateArray();
    MasterDevice m(0);
    m.open(MasterDevice::Read);

    ec_ioctl_master_t master;
    ec_ioctl_slave_t slave;

    memset(&master, 0, sizeof(ec_ioctl_master_t));
    memset(&slave, 0, sizeof(ec_ioctl_slave_t));
    m.getMaster(&master);

    for (unsigned int i = 0; i < master.slave_count; i++) {
        ECModule *module = ECInterface::findModule(i);
        if (!module) {
            m.getSlave(&slave, i);
            cJSON_AddItemToArray(root, generateSlaveCStruct(m, slave, true));
        }
        else {
            std::cout << "Skipped scanning of module at position " << i << " already loaded\n";
        }
    }
#endif
    char *json = cJSON_Print(root);
    cJSON_Delete(root);

    /* save a description of the bus configuration */
    std::ofstream logfile;
    logfile.open("/tmp/ecat.log", std::ofstream::out /* | std::ofstream::app */);
    logfile << json << "\n";
    logfile.close();

    return json;
}
#endif // USE_KERNEL_ETHERCAT

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
