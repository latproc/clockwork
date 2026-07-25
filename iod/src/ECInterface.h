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

#ifndef __ECInterface
#define __ECInterface

#include <sys/types.h>

#include "IODCommand.h"
#include "MachineInstance.h"
#include "tl/expected.hpp"
#include "process_data.h"
#ifndef EC_SIMULATOR
#include "value.h"

#include <ecrt.h>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <time.h>
#include <vector>
#include <memory>  // for std::unique_ptr

#ifdef USE_KERNEL_ETHERCAT
class KernelEthercatBus;
#endif

class MachineInstance;

/*  the entry details structure is used to gather extra data about
    an entry in a module that the Etherlab master structures doesn't
    normally give us.
*/
class EntryDetails {
  public:
    std::string name;
    unsigned int entry_index;
    unsigned int sm_index;
    unsigned int pdo_index;
};

#ifdef USE_SDO
class SDOEntry;
#endif //USE_SDO
class ECModule {
  public:
    ECModule();
    ~ECModule();
    bool ecrtMasterSlaveConfig(ec_master_t *master);
    bool ecrtSlaveConfigPdos();
    bool online();
    bool operational();
    int state();
    std::ostream &operator<<(std::ostream &) const;
    const std::string &getName() const { return name; }

    void link_to_machine(MachineInstance *m);
    MachineInstance *machine();
    void update();

  public:
    ec_slave_config_t *slave_config;
    ec_slave_config_state_t slave_config_state;
    uint16_t alias;
    uint16_t position;
    uint32_t vendor_id;
    uint32_t product_code;
    uint32_t revision_no;
    /** ELC topology slave config_id (0 when not from kernel topology conf). */
    uint32_t elc_config_id;
    unsigned int *offsets;
    unsigned int *bit_positions;
    unsigned int sync_count;

    ec_pdo_entry_info_t *pdo_entries;
    ec_pdo_info_t *pdos;
    ec_sync_info_t *syncs;
    std::string name;
    unsigned int num_entries;
    EntryDetails *entry_details;
    MachineInstance *machine_instance;
};

#else // EC_SIMULATOR
typedef struct ECMaster {
    unsigned int reserved;
    unsigned int config_changed;
    unsigned int slave_count;
} ec_master_t;
typedef struct ECMasterState {
    unsigned int link_up;
    unsigned int al_states;
} ec_master_state_t;
typedef struct ECDomain {
} ec_domain_t;
typedef struct ECDomainState {
} ec_domain_state_t;
typedef struct ECSlaveConfig {
} ec_slave_config_t;
typedef struct ECSlaveConfigState {
} ec_slave_config_state_t;
typedef struct ECPDOEntryReg {
} ec_pdo_entry_reg_t;

#endif // EC_SIMULATOR

class ECInterface {
  public:
    static unsigned int FREQUENCY;
    /** Kernel/legacy bus period locked at activate (µs). 0 = not yet activated. */
    static unsigned long activated_cycle_period_us_;
    static ec_master_t *master;
    static uint64_t master_last_checked;  // time the master status was last checked
    static uint64_t master_state_changed; // time a last state change was detected in the master

    static ec_master_state_t master_state;

    static ec_domain_t *domain1;
    static ec_domain_state_t domain1_state;
    static uint8_t *domain1_pd;

    bool initialised;
    static bool active;
    ProcessData data;

    static ECInterface *instance();

    // Timer
    static unsigned int sig_alarms;

    void check_domain1_state(void);
    void check_master_state(void);
    void check_slave_config_states(void);
    /**
     * Receive EtherCAT state. On the kernel path, pull_process_image controls
     * whether a full coherent snapshot is copied (expensive). Status/AL still
     * refresh on their own rate limits. Call with true before collectState /
     * POLLING_DELAY feed; false on pure bus timer ticks between CW pulls.
     */
    void receiveState(bool pull_process_image = true);
    void receivePendingDomainState(); // collect a response that arrived late in this cycle
    int collectState(); // returns non-zero if there are machines that are affected by the new state
    void sendUpdates();
    void updateDomain(uint32_t size, uint8_t *data, uint8_t *mask);
#ifdef USE_KERNEL_ETHERCAT
    /** Write a digital output bit into the kernel output shadow (turnOn/turnOff). */
    void applyKernelOutputBit(unsigned int io_offset, unsigned int bitpos, bool on);
    /** Write multi-bit/analogue value into the kernel output shadow. */
    void applyKernelOutputValue(unsigned int io_offset, unsigned int bitpos, unsigned int bitlen,
                                uint32_t value);
#endif

    bool start();
    bool stop();
    void init(); // prepare the master
    static void
    setup(void *data); // call init and link to the clockwork machine instance for ethercat
    //void add_io_entry(const char *name, unsigned int io_offset, unsigned int bit_offset);
    const ec_master_t *getMaster() { return master; }
    const ec_master_state_t *getMasterState() { return &master_state; }

#ifndef EC_SIMULATOR
    std::vector<ec_slave_info_t> listSlaves();
    bool activate();   // attempt to activate the master
    bool deactivate(); // deactivate the master
    /**
     * SYSTEM.CYCLE_DELAY (microseconds) → EtherCAT clocking only.
     * Before activate: sets FREQUENCY / get_cycle_time for activate().
     * After activate: bus period is frozen (kernel set_period not used while
     * live); FREQUENCY may track for the ecat userspace timer only.
     * Clockwork poll rate is SYSTEM.POLLING_DELAY (ProcessingThread).
     */
    bool applyCyclePeriodUs(unsigned long period_us);
    /** Bus period frozen at last successful activate (µs); 0 if not active. */
    unsigned long activatedCyclePeriodUs() const;
    void configureModules();
    void registerModules();
    tl::expected<bool, std::string> addModule(ECModule *m, bool reset_io);
    bool online();
    bool operational();
    static ECModule *findModule(unsigned int position);

#ifdef USE_KERNEL_ETHERCAT
    bool initialiseKernelTransport();  // open kernel bus for discovery + SDO
    KernelEthercatBus* getKernelBus();
#endif

    uint32_t getReferenceTime();
    void setReferenceTime(uint32_t now);
#ifdef USE_DC
    uint64_t getApplicationTimeNs() const { return dc_application_time_ns; }
    /** IOTIME resolution matches legacy ecrt path: application time in µs
     *  (getApplicationTimeNs() / 1000). */
    uint64_t getApplicationTimeUs() const { return dc_application_time_ns / 1000ULL; }
#endif
#ifdef USE_KERNEL_ETHERCAT
    /** Refresh dc_application_time_ns from the kernel cycle DC contract so
     *  updateClock() / IOTIME use the same µs scale as the legacy ecrt path. */
    void refreshKernelApplicationTime();
#endif

    void report_module_state_change(ECModule *m, int i);

#ifdef USE_SDO
    void beginModulePreparation();    // load the first SDO initialisation entry
    bool finishedModulePreparation(); // are all the SDO init entries completed
    bool checkSDOInitialisation();
    void checkSDOUpdates();

    void addSDOEntry(SDOEntry *);
    static SDOEntry *createSDORequest(std::string name, ECModule *module, uint16_t index,
                                      uint8_t subindex, size_t size);

    void queueInitialisationRequest(SDOEntry *entry, Value val);
    void queueRuntimeRequest(SDOEntry *entry);
    void acceptPendingSDOWrites();
#endif //USE_SDO

#endif // ifndef EC_SIMULATOR
  private:
    ECInterface();
    static ECInterface *instance_;
    uint32_t reference_time;
#ifdef USE_DC
    uint64_t dc_application_time_ns;
    int64_t dc_cycle_adjustment_ns;
    int64_t dc_difference_total_ns;
    int64_t dc_delta_total_ns;
    int32_t dc_last_difference_ns;
    unsigned int dc_filter_count;
    unsigned int dc_monitor_countdown;
    unsigned int dc_monitor_wait_cycles;
    bool dc_reference_valid;
    bool dc_monitor_pending;
    int dc_last_reference_result;

    static uint64_t monotonicTimeNs();
    void processDistributedClock();
    void queueDistributedClockSync();
#endif
#ifndef EC_SIMULATOR
    static std::vector<ECModule *> modules;
    std::set<ECModule *> online_modules;
    std::set<ECModule *> operational_modules;
#ifdef USE_SDO
    std::list<std::pair<SDOEntry *, Value>> initialisation_entries;
    std::list<std::pair<SDOEntry *, Value>> pending_sdo_writes;
    std::mutex pending_sdo_mutex;
    std::list<std::pair<SDOEntry *, Value>>::iterator current_init_entry;
    std::list<SDOEntry *> sdo_update_entries;
    std::list<SDOEntry *>::iterator current_update_entry;
    enum SDOEntryState { e_None, e_Busy_Initialisation, e_Busy_Update };
    SDOEntryState sdo_entry_state;
    uint64_t sdo_not_before;
#endif //USE_SDO
#endif
    MachineInstance *ethercat_status;
    static long default_tolerance;
    const long *failure_tolerance;
    int failure_count;

#ifdef USE_KERNEL_ETHERCAT
    std::unique_ptr<KernelEthercatBus> kernelBus;
#endif
};

#ifndef EC_SIMULATOR
#ifdef USE_ETHERCAT
void collectEtherCatModules();
char *collectSlaveConfig(bool reconfigure);
#endif
#endif



struct IODCommandMasterInfo : public IODCommand {
    bool run(std::vector<Value> &params);
};

struct IODCommandGetSlaveConfig : public IODCommand {
    bool run(std::vector<Value> &params);
};

#endif
