/*
 * KernelEthercatBus — thin adapter for libelcethercat.
 * One master owner: open/close, discovery, setup SDO, config, cyclic, process image.
 * No Clockwork policy.
 */

#ifndef KERNEL_ETHERCAT_BUS_H
#define KERNEL_ETHERCAT_BUS_H

#include "elc_ethercat.h"
#include <cstdint>
#include <vector>
#include "ECInterface.h" // ec_slave_info_t

class KernelEthercatBus {
  public:
    KernelEthercatBus();
    ~KernelEthercatBus();

    int open(const char *device_path = "/dev/elc_ethercat0");
    void close();

    bool isOpen() const { return handle != nullptr; }

    std::vector<ec_slave_info_t> listSlaves();

    int setupBegin();
    int setupAddSDO(const struct elc_setup_sdo *sdo);
    int setupApply(struct elc_setup_apply *result);
    int setupReset();
    int sdoUpload(struct elc_sdo_upload *req);

    int configBegin();
    int configAddSlave(const struct elc_config_slave *slave);
    int configAddSync(const struct elc_config_sync *sync);
    int configAddPdo(const struct elc_config_pdo *pdo);
    int configAddEntry(const struct elc_config_entry *entry);
    int configAddDomain(const struct elc_config_domain *domain);
    int configAddDomainAssignment(const struct elc_config_domain_assignment *asgn);
    int configValidate(struct elc_config_validate *result);
    int configApply(struct elc_config_apply *result);
    int domainCreate(struct elc_domain_create *result);
    int getEntryOffset(struct elc_entry_offset *io);
    elc_handle *rawHandle() { return handle; }

    int cycleActivate(uint32_t period_ns, uint32_t flags, struct elc_cycle_activate *out);
    int cycleDeactivate(struct elc_cycle_deactivate *out = nullptr);
    /** Change period while cycling (outputs must be disarmed; API 0.15+). */
    int cycleSetPeriod(uint32_t period_ns, struct elc_cycle_period_update *out = nullptr);
    int cycleWait(struct elc_cycle_wait *wait);
    int cycleStatus(struct elc_cycle_status *st);
    int cycleInfo(struct elc_cycle_info *info);
    int cycleDcInfo(struct elc_cycle_dc_info *info);

    /**
     * If the module was loaded with cycle_fifo_priority=0, the elc_cycle
     * kthread is SCHED_OTHER and domain WC flaps incomplete under load.
     * Promote any live elc_cycle to SCHED_FIFO on the preferred CPU.
     * Call after cycleActivate (thread does not exist before then).
     */
    static int ensureCycleThreadRealtime(int cpu = 1, int fifo_priority = 90);

    /**
     * Copy the kernel's latest coherent process image into buf.
     * Always binds to config_generation_ when known so the ioctl does not
     * re-query IO status and cannot attach to a stale generation.
     * On success, snap->input_sequence / cycle_count identify the buffer;
     * lastInputSequence() is updated.
     */
    int getInputSnapshot(void *buf, size_t len, struct elc_input_snapshot *snap);
    int publishOutput(const void *image, const void *mask, size_t len,
                      struct elc_output_publish *pub);
    int armOutput(struct elc_output_arm *arm);
    int disarmOutput(struct elc_output_disarm *disarm = nullptr);
    int getIoStatus(struct elc_io_status *st);

    /** Per bus-position discovery status (works before cycle activate). */
    int getSlaveInfo(uint16_t position, struct elc_slave_info *info);
    /**
     * Per configured-slave cyclic status (online/operational/al_state).
     * Only valid after config apply; live AL fields require active cycle.
     */
    int getConfigSlaveStatus(uint32_t config_id, struct elc_config_slave_status *st);

    uint32_t domainSize() const { return domain_size_; }
    uint64_t configGeneration() const { return config_generation_; }
    /** Monotonic sequence of the last successful getInputSnapshot (0 if none). */
    uint64_t lastInputSequence() const { return last_input_sequence_; }
    uint64_t lastInputCycle() const { return last_input_cycle_; }

  private:
    elc_handle *handle = nullptr;
    bool apiNegotiated = false;
    uint32_t domain_size_ = 0;
    uint64_t config_generation_ = 0;
    uint64_t last_input_sequence_ = 0;
    uint64_t last_input_cycle_ = 0;
};

#endif
