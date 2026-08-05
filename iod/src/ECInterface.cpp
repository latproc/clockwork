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
#include <atomic>
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
#include "cw_ethercat_types.h"
#include "process_data.h"
#include "KernelEthercatBus.h"
#include "ElcConfigFile.h"
#include "ElcSetupRecipe.h"
#include "EtherCATSetup.h"
#include "options.h"
#include "IOComponent.h"
#include "elc_ethercat.h"
#endif

#define VERBOSE_DEBUG 0
#if VERBOSE_DEBUG
static void display(uint8_t *p, size_t n);
#endif

extern Statistics *statistics;
void signal_handler(int signum);

// While true, pause ecat userspace get_io_status/publish so blocking setup
// SDO / setup-hold can own the master without multi-minute deadlock.
static std::atomic<bool> g_setup_mailbox_busy{false};

void ECInterface::setSetupMailboxExclusive(bool on) {
    g_setup_mailbox_busy.store(on);
}

namespace {
struct SetupMailboxGuard {
    SetupMailboxGuard() { ECInterface::setSetupMailboxExclusive(true); }
    ~SetupMailboxGuard() { ECInterface::setSetupMailboxExclusive(false); }
};
} // namespace

unsigned int ECInterface::FREQUENCY = 2000;
unsigned long ECInterface::activated_cycle_period_us_ = 0;

// Commanded process image for outputs. receiveState() overwrites domain1_pd with
// the input snapshot; without this shadow, turnOn bits are wiped before publish.
static std::vector<uint8_t> g_kernel_output_image;
static std::vector<uint8_t> g_kernel_output_mask;
// Publish only when the shadow changes (or until first successful arm).
static bool g_kernel_output_dirty = true;
static bool g_kernel_outputs_armed = false;
// Once all valid domains have been armed, re-push plant output defaults once.
static bool g_output_defaults_after_arm_done = false;
static std::vector<uint8_t> g_kernel_pub_mask; // cached full-domain publish mask
static bool g_kernel_pub_mask_valid = false;
// CAP_OUTPUT_LEASE: 0.18 uses timeout_ms + publish/arm refill; no renew loop.
static bool g_output_lease_enabled = false;
static uint32_t g_output_lease_timeout_ms = 0;
static bool g_output_lease_publish_renew = false;
// microsecs() of last successful publishOutput while lease is on (publish-renew
// refill). Quiet output paths must still publish before timeout or the kernel
// latches CONTROLLER_STALE and drops arm — sampler sees armed 0↔1 thrash.
static uint64_t g_output_lease_last_publish_us = 0;
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
    // EC_WC_*: ZERO=0, INCOMPLETE=1, COMPLETE=2 (domain working counter).
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
      size_(size), sync_done(false), error_count(0), op(READ),
      machine_instance(0), next_poll_time(0), read_pending(true) {
    if (data && size != 0) {
        data_ = new uint8_t[size];
        assert(data_);
        memcpy(data_, data, size);
    }
    new_sdo_entries.push_back(this);
}


SDOEntry::~SDOEntry() {
    if (data_) {
        delete[] data_;
        data_ = 0;
    }
}

/** Local mailbox buffer (replaces ecrt SDO request payload). */
uint8_t *SDOEntry::sdoBuffer() {
    size_t bytes = ((size_ + offset_ + 7) / 8);
    if (bytes < 8) {
        bytes = 8;
    }
    if (!data_) {
        data_ = new uint8_t[bytes];
        memset(data_, 0, bytes);
    }
    return data_;
}

void SDOEntry::setData(bool val) {
    uint8_t *data = sdoBuffer();
    if (data) {
        EC_WRITE_BIT(data, offset_, ((val) ? 1 : 0));
    }
}

void SDOEntry::setData(uint8_t val) {
    EC_WRITE_U8(sdoBuffer(), val);
}

void SDOEntry::setData(int8_t val) {
    EC_WRITE_S8(sdoBuffer(), val);
}

void SDOEntry::setData(uint16_t val) {
    EC_WRITE_U16(sdoBuffer(), val);
}

void SDOEntry::setData(int16_t val) {
    EC_WRITE_S16(sdoBuffer(), val);
}

void SDOEntry::setData(uint32_t val) {
    EC_WRITE_U32(sdoBuffer(), val);
}

void SDOEntry::setData(int32_t val) {
    EC_WRITE_S32(sdoBuffer(), val);
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
#ifndef EC_SIMULATOR
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
    // Kernel cyclic master owns DC; refresh application time from the bus.
    refreshKernelApplicationTime();
}

void ECInterface::queueDistributedClockSync() {
    // DC sync is owned by the kernel cyclic task.
}
#endif

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
bool SDOEntry::prepareRequest(ECModule *module) {
    assert(module);
    module_ = module;
    prepared_sdo_entries.remove(this);
    (void)sdoBuffer();
    DBG_ETHERCAT_SDO << "Prepared SDO entry " << module->getName() << " 0x" << std::hex << index_
                     << ":" << (int)subindex_ << std::dec << " (" << size_ << " bits)\n";
    prepared_sdo_entries.push_back(this);
    return true;
}
#endif //USE_SDO

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

// Debug helper (unused on plant path).
static void readValueDebug(const uint8_t *data, unsigned int size, int offset = 0) {
    if (!data) {
        return;
    }
    if (size == 32) {
        fprintf(stderr, "SDO value: 0x%08X\n", EC_READ_U32(data));
    }
    else if (size == 16) {
        fprintf(stderr, "SDO value: 0x%04X\n", EC_READ_U16(data));
    }
    else if (size == 8) {
        fprintf(stderr, "SDO value: 0x%02X\n", EC_READ_U8(data));
    }
    else if (size == 1) {
        fprintf(stderr, "SDO value: 0x%01X\n", EC_READ_BIT(data, offset));
    }
}

void SDOEntry::syncValue() {
    const uint8_t *data = sdoBuffer();
    if (!data || !machine_instance) {
        return;
    }
    if (size_ == 32) {
        machine_instance->setValue("VALUE", EC_READ_U32(data));
    }
    else if (size_ == 16) {
        machine_instance->setValue("VALUE", EC_READ_U16(data));
    }
    else if (size_ == 8) {
        machine_instance->setValue("VALUE", EC_READ_U8(data));
    }
    else if (size_ == 1) {
        machine_instance->setValue("VALUE", EC_READ_BIT(data, offset_));
    }
}

Value SDOEntry::readValue() {
    const uint8_t *data = sdoBuffer();
    if (!data) {
        return SymbolTable::Null;
    }
    if (size_ == 32) {
        return EC_READ_U32(data);
    }
    if (size_ == 16) {
        return EC_READ_U16(data);
    }
    if (size_ == 8) {
        return EC_READ_U8(data);
    }
    if (size_ == 1) {
        return EC_READ_BIT(data, offset_);
    }
    return SymbolTable::Null;
}

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
    uint8_t *dst = entry->sdoBuffer();
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
    // Mirror written value into the local buffer so confirmation/readValue agree.
    uint8_t *dst = entry->sdoBuffer();
    if (dst) {
        memset(dst, 0, 8);
        memcpy(dst, data, len);
    }
    return true;
}

void ECInterface::checkSDOUpdates() {
    const uint64_t now = microsecs();
    if (now < sdo_not_before) {
        return;
    }
    if (current_update_entry == sdo_update_entries.end()) {
        if (sdo_update_entries.empty()) {
            return;
        }
        current_update_entry = sdo_update_entries.begin();
        sdo_entry_state = e_None;
    }
    if (current_update_entry == sdo_update_entries.end()) {
        return;
    }
    SDOEntry *entry = *current_update_entry;
    if (!entry) {
        current_update_entry++;
        return;
    }
    if (sdo_entry_state == e_None && entry->machineInstance() &&
        !entry->machineInstance()->enabled() && !entry->readPending()) {
        current_update_entry++;
        return;
    }
    if (entry->operation() == SDOEntry::WRITE) {
        assert(!initialisation_entries.empty());
        return; // let initialisation handle writes
    }
    if (entry->operation() != SDOEntry::READ) {
        current_update_entry++;
        return;
    }
    if (!entry->pollDue(now)) {
        current_update_entry++;
        return;
    }
    if (!(kernelBus && kernelBus->isOpen())) {
        entry->failure();
        entry->requestRead(now, 250);
        sdo_not_before = now + 250000;
        current_update_entry++;
        return;
    }
    // Synchronous mailbox upload (one op per call).
    if (kernelSdoUploadEntry(entry)) {
        bool first_read = !entry->ready();
        entry->syncValue();
        entry->success();
        entry->schedulePoll(now);
        if (first_read && entry->machineInstance() &&
            entry->machineInstance()->properties.exists("default")) {
            const Value &val = entry->machineInstance()->properties.lookup("default");
            DBG_ETHERCAT_SDO << "Applying SDO default after initial/recommission read for "
                             << entry->getName() << ": " << val << "\n";
            queueInitialisationRequest(entry, val);
        }
        sdo_not_before = now + (g_kernel_outputs_armed ? 10000 : 2000);
    }
    else {
        entry->failure();
        entry->requestRead(now, 250);
        sdo_not_before = now + 250000;
    }
    current_update_entry++;
    sdo_entry_state = e_None;
}


bool ECInterface::checkSDOInitialisation() {
    acceptPendingSDOWrites();
    const uint64_t now = microsecs();
    if (now < sdo_not_before) {
        return false;
    }
    if (sdo_entry_state == e_Busy_Update) {
        return true;
    }
    if (current_init_entry == initialisation_entries.end()) {
        if (initialisation_entries.empty()) {
            return true;
        }
        current_init_entry = initialisation_entries.begin();
        sdo_entry_state = e_None;
    }
    if (current_init_entry == initialisation_entries.end()) {
        return true;
    }
    std::pair<SDOEntry *, Value> curr = *current_init_entry;
    SDOEntry *entry = curr.first;
    if (!entry) {
        DBG_ETHERCAT_SDO << "Skipping null entry when checking SDO\n";
        current_init_entry++;
        return false;
    }
    // A write must never overtake the entry's initial upload.
    if (!entry->ready()) {
        return true;
    }
    if (!(kernelBus && kernelBus->isOpen())) {
        return false;
    }
    entry->setOperation(SDOEntry::WRITE);
    DBG_ETHERCAT_SDO << "SDO entry - kernel write " << curr.second << "\n";
    if (kernelSdoDownloadEntry(entry, curr.second)) {
        entry->syncValue();
        entry->success();
        entry->requestRead(now, 1);
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
    if (kernelBus && kernelBus->isOpen()) {
        // Offsets already resolved while populating modules from topology conf.
        DBG_ETHERCAT << "registerModules: kernel transport offsets already set from topology\n";
        return;
    }
}

void ECInterface::configureModules() {
    if (kernelBus && kernelBus->isOpen()) {
        // Most Clockwork MODULE entries have no ESI XML; legacy iod filled PDOs via
        // bus scan. Use the captured full-bus topology conf instead.
        const char *topo = elcDefaultTopologyConfigPath();
        if (!topo) {
            std::cerr << "Failed to apply ELC topology: ELC_TOPOLOGY_CONFIG is not set\n";
            return;
        }
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
        // Ordered setup recipes: plant ECSETUPRECIPE + optional CLI.
        // Applied under mailbox exclusive *before* cycle activate (see activate()).
        // Do not block here: ecat thread is already running; exclusive apply
        // happens once on the activate path so PREOP CoE completes first.
        std::cerr << "ElcSetupRecipe: will apply on cycle activate (pre-OP CoE)\n";
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
    if (kernelBus && kernelBus->isOpen()) {
        return kernelBus->listSlaves();
    }
    return {};
}

;

void collectEtherCatModules() {
    auto slaves = ECInterface::instance()->listSlaves();
    if (slaves.empty()) {
        DBG_ETHERCAT << "No slaves found on bus\n";
        return;
    }
    std::stringstream ss;
    // seed ECModule slots in bus order so XML can replace by position.
    // Identity from elc_list_slaves; PDO map from topology/XML.
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
}



bool ECInterface::deactivate() {
    if (kernelBus && kernelBus->isOpen()) {
        kernelBus->disarmOutput();
        kernelBus->cycleDeactivate();
        active = false;
        activated_cycle_period_us_ = 0;
        g_output_lease_enabled = false;
        g_output_lease_timeout_ms = 0;
        g_output_lease_publish_renew = false;
        g_output_lease_last_publish_us = 0;
        g_kernel_outputs_armed = false;
        g_output_defaults_after_arm_done = false;
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
    return false;
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
    if (kernelBus && kernelBus->isOpen()) {
        // Already cyclic: further elc_cycle_activate → EBUSY. Treat as success
        // so STARTUP/ENTER-op re-SEND activate does not spam the ecat path.
        if (active) {
            std::cerr << "ECInterface::activate: already active (noop)\n";
            return true;
        }
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

        // Do NOT run blocking SETUP_APPLY here on the ecat thread: CoE to OP
        // slaves can hang for minutes and freezes ACTIVATE_REQUEST completion.
        // Cycle activate first; setup-hold reapply runs on the worker after.

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
        g_output_defaults_after_arm_done = false;
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
        // Non-null marker so existing domain checks pass.
        domain1 = reinterpret_cast<ec_domain_t *>(domain1_pd);
        data.setDataSize(dsz);
        // process_data is owned separately by ProcessData; do not alias domain1_pd.
        active = true;
        initialised = true;
        all_ok = true;
        // initialiseOutputs() ran before activate; this path zeroed the shadow.
        // Re-push plant ANALOGOUTPUT/POINT defaults and publish immediately so
        // the first cyclic frames are not all-zero for objects plant mapped.
        reapplyOutputDefaults();
        {
            size_t pdsz = dsz;
            if (g_kernel_output_image.size() < pdsz) {
                g_kernel_output_image.resize(pdsz, 0);
                g_kernel_output_mask.resize(pdsz, 0);
            }
            std::vector<uint8_t> pub_mask(pdsz, 0xff);
            uint8_t *proc_mask = IOComponent::getProcessMask();
            size_t proc_len = 0;
            if (proc_mask) {
                int max_off = IOComponent::getMaxIOOffset();
                if (max_off >= 0) {
                    proc_len = static_cast<size_t>(max_off) + 1;
                }
            }
            for (size_t i = 0; i < pdsz; ++i) {
                uint8_t m = g_kernel_output_mask[i];
                if (proc_mask && i < proc_len) {
                    m = static_cast<uint8_t>(m | proc_mask[i]);
                }
                if (m) {
                    pub_mask[i] = m;
                }
            }
            struct elc_output_publish pub = {};
            int pret = kernelBus->publishOutput(g_kernel_output_image.data(), pub_mask.data(),
                                                pdsz, &pub);
            if (pret != 0) {
                std::cerr << "WARNING: post-activate defaults publish failed ret=" << pret
                          << "\n";
            }
            else {
                g_kernel_output_dirty = false;
                g_kernel_pub_mask = std::move(pub_mask);
                g_kernel_pub_mask_valid = true;
                std::cerr << "post-activate: published plant output defaults (domain_size="
                          << pdsz << " seq=" << pub.output_sequence << ")\n";
            }
        }
#ifdef USE_DC
        // Seed application time: monotonic ns aligned to
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
        // Queue plant ECSETUPRECIPE for all matching positions (PDO map, accel,
        // Pn001). Worker uses setup-hold (0.19) so CoE runs in PREOP without
        // blocking this thread.
        {
            boost::recursive_mutex::scoped_lock lock(modules_mutex);
            for (ECModule *m : modules) {
                if (m && ElcSetupRecipe::positionWantsReapply(m->position)) {
                    ElcSetupRecipe::requestReapply(m->position);
                }
            }
        }
        ElcSetupRecipe::scheduleProcessPending(kernelBus.get());
        return true;
    }
    return false;
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
#else
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

void ECInterface::applyKernelOutputBit(unsigned int io_offset, unsigned int bitpos, bool on) {
    if (!domain1_pd) {
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
    // Allow writes as soon as the process-image shadow exists (after activate
    // allocates domain1_pd), so plant defaults can be applied before first arm.
    if (!domain1_pd || bitlen == 0) {
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

void ECInterface::updateDomain(uint32_t size, uint8_t *data, uint8_t *mask) {
    // domain1 is a marker pointer; images are domain1_pd + g_kernel_output_*.
    // domain1_pd is owned process image buffer (kernel snapshot).
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

}

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
    g_output_lease_last_publish_us = 0; // force first keepalive publish soon
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
    // Prefer the kernel DC motion-clock contract (application time
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

void ECInterface::receiveState(bool pull_process_image) {
    if (g_setup_mailbox_busy.load()) {
        return;
    }
    static long warned = 0;
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
                    // as DC application time in ns.
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
        // Blocking CoE must not run before cycle activate: ecrt_master_sdo_upload
        // can hang for minutes with no cyclic PDI, which freezes the ecat thread
        // so ACTIVATE_REQUEST is never handled (update_state stuck, domains INVALID).
        if (active) {
            if (checkSDOInitialisation()) {
                checkSDOUpdates();
            }
        }
#endif
        return;
    }
}

void ECInterface::receivePendingDomainState() {
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
}

bool ECInterface::domainHasDigitalChange(const uint8_t *prev_domain, size_t prev_len) {
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
}

size_t ECInterface::copyDomainData(uint8_t *dst, size_t dst_len) {
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
}

int ECInterface::collectState() {
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
        // Diff vs last image: mask bits that changed
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
        // Full image for CW (multi-bit values).
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
    return 0;
}
void ECInterface::sendUpdates() {
    static uint64_t last_warning = 0;
    uint64_t now = microsecs();
    if (g_setup_mailbox_busy.load()) {
        return;
    }
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

        // Offline→online re-apply: never block sendUpdates with mailbox SDO.
        // Worker thread runs processPending (setup-hold + SETUP_APPLY).
        ElcSetupRecipe::scheduleProcessPending(kernelBus.get());

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
        // Only publish when the commanded shadow changed, a valid domain still
        // needs arm, or the output lease needs a publish-renew keepalive.
        // Without keepalive, quiet paths skip publish → lease expires →
        // CONTROLLER_STALE → disarm/rearm thrash that glitches outputs.
        bool need_domain_rearm = false;
        if (g_domain_status_ok && kernelBus->hasDomainOutputAuthority()) {
            for (const ElcDomainSlot &slot : g_domains) {
                if (slot.valid && !slot.armed) {
                    need_domain_rearm = true;
                    break;
                }
            }
        }
        bool lease_keepalive_due = false;
        if (g_output_lease_enabled && g_output_lease_publish_renew &&
            g_output_lease_timeout_ms > 0) {
            // Half timeout (floor 50 ms): refill well before kernel expiry.
            uint64_t keep_us =
                (static_cast<uint64_t>(g_output_lease_timeout_ms) * 1000ULL) / 2ULL;
            if (keep_us < 50000ULL) {
                keep_us = 50000ULL;
            }
            if (g_output_lease_last_publish_us == 0 ||
                now - g_output_lease_last_publish_us >= keep_us) {
                lease_keepalive_due = true;
            }
        }
        if (!g_kernel_output_dirty && g_kernel_outputs_armed && !need_domain_rearm &&
            !lease_keepalive_due) {
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
            if (g_output_lease_enabled) {
                g_output_lease_last_publish_us = now;
            }
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
                bool all_valid_armed = true;
                for (const ElcDomainSlot &slot : g_domains) {
                    if (slot.valid && !slot.armed) {
                        g_kernel_output_dirty = true; // retry arm with a fresh sequence
                        all_valid_armed = false;
                        break;
                    }
                }
                // First time every valid domain is armed: re-push plant output
                // defaults (generic — whatever ANALOGOUTPUT/POINT `default` says).
                if (all_valid_armed && g_kernel_outputs_armed &&
                    !g_output_defaults_after_arm_done) {
                    g_output_defaults_after_arm_done = true;
                    reapplyOutputDefaults();
                    g_kernel_output_dirty = true;
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
}
#endif

/*****************************************************************************/

void ECInterface::check_domain1_state(void) {
    // Domain WC/state comes from elc domain status (refreshKernelDomainHealth).
}

/*****************************************************************************/

void ECInterface::check_master_state(void) {
    // Master/link/slave counts are updated from elc_io_status in receiveState().
}

/*****************************************************************************/

#ifndef EC_SIMULATOR
void ECInterface::report_module_state_change(ECModule *m, int i) {
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
#ifdef USE_SDO
        // Entering PREOP/SAFEOP after cold start: (re)queue setup so PDO map
        // runs in that window. processPending only applies while AL is
        // PREOP/SAFEOP — not OP — so OP flaps do not burn failed attempts.
        if (m->sdo_seen_online && ElcSetupRecipe::positionWantsReapply(m->position) &&
            (s.al_state == 0x02 || s.al_state == 0x04)) {
            ElcSetupRecipe::requestReapply(m->position);
        }
#endif
    }
    if (s.online != m->slave_config_state.online) {
        snprintf(buf, BUFSIZE, "Slave %d (%s) online %s -> %s (kernel)", i, m->name.c_str(),
                 m->slave_config_state.online ? "yes" : "no", s.online ? "yes" : "no");
        MessageLog::instance()->add(buf);
        std::cout << buf << "\n";
#ifdef USE_SDO
        // Return after power/link loss: queue ECSETUPRECIPE re-apply (client
        // debounce/retry in ElcSetupRecipe::processPending). First-ever online
        // is cold start — configure-time applyAllConfigured already ran.
        // Return after power/link loss: queue plant setup recipes / L_SDO.
        // Do not reapplyOutputDefaults() here — every slave online edge would
        // spam the full image; defaults are pushed at activate, first arm, and
        // after a successful setup-recipe re-apply.
        if (s.online && !m->slave_config_state.online) {
            if (m->sdo_seen_online) {
                if (ElcSetupRecipe::positionWantsReapply(m->position)) {
                    ElcSetupRecipe::requestReapply(m->position);
                }
                else {
                    SDOEntry::recommissionModule(m, microsecs());
                }
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
}
#endif

void ECInterface::check_slave_config_states(void) {
#ifndef EC_SIMULATOR
    boost::recursive_mutex::scoped_lock lock(modules_mutex);
    std::vector<ECModule *>::iterator iter = modules.begin();
    int i = 0;
    slaves_not_operational = 0;
    slaves_offline = 0;
    unsigned int al_or_all = 0;
    unsigned int al_or_primary = 0;
    unsigned int modules_seen = 0;
    unsigned int modules_op = 0;
    unsigned int modules_primary = 0;
    unsigned int modules_primary_op = 0;
    bool saw_domain_ids = false;
    // Per-domain AL OR (aligned with g_domains slots).
    std::vector<unsigned int> al_per_domain(g_domains.size(), 0);
    while (iter != modules.end()) {
        ECModule *m = *iter++;
        if (!m) {
            char buf[100];
            snprintf(buf, 100, "null module in module list at position %d", i);
            MessageLog::instance()->add(buf);
            std::cout << buf << "\n";
            assert(m != 0);
        }
        // Poll AL state from libelcethercat.
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
        if (!m->slave_config) {
            //std::cout << "module " << m->name << " not active yet..skipping\n";
            continue;
        }
        report_module_state_change(m, i);
        ++i;
    }
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



char *collectSlaveConfig(bool reconfigure) {
    // Kernel transport (): simple stub. Full slave config/PDO mapping in Phase 9.
    // IODCommandGetSlaveConfig / status for plant tooling.
    cJSON *root = cJSON_CreateArray();
    cJSON *entry = cJSON_CreateObject();
    cJSON_AddStringToObject(entry, "status", "kernel-transport");
    cJSON_AddStringToObject(entry, "note", "discovery + SDO mailbox only (outputs in Phase 11)");
    cJSON_AddItemToArray(root, entry);
    char *json = cJSON_Print(root);
    cJSON_Delete(root);
    return json;
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
