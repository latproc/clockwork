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
    /**
     * Runtime CoE download via a one-shot setup batch (same path as elc_sdo write).
     * type is elc_sdo_type (ELC_SDO_U8/U16/U32/…). data_len must match type.
     * Safe while the cyclic domain is active; blocks until the mailbox completes.
     */
    int sdoDownload(uint16_t position, uint16_t index, uint8_t subindex, uint8_t type,
                    const uint8_t *data, uint16_t data_len);

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

    /**
     * Output hang failsafe (ELC_CAP_OUTPUT_LEASE). Must be configured after
     * config apply and *before* cycle activate (kernel rejects when active).
     * cycle_budget is in bus cycles; remaining is refilled by renewOutputLease.
     */
    int configureOutputLease(uint32_t cycle_budget);
    int renewOutputLease(struct elc_output_lease_renew *renew = nullptr);
    int getOutputLeaseStatus(struct elc_output_lease_status *st);
    bool hasOutputLease() const {
        return (capabilities_ & ELC_CAP_OUTPUT_LEASE) != 0;
    }

    /** Per bus-position discovery status (works before cycle activate). */
    int getSlaveInfo(uint16_t position, struct elc_slave_info *info);
    /**
     * Per configured-slave cyclic status (online/operational/al_state).
     * Only valid after config apply; live AL fields require active cycle.
     */
    int getConfigSlaveStatus(uint32_t config_id, struct elc_config_slave_status *st);
    /** Per-domain WC/validity/arm (API 0.12+). domain_config_id must match config. */
    int getDomainStatus(uint32_t domain_config_id, struct elc_domain_status *st);

    uint32_t domainSize() const { return domain_size_; }
    uint64_t configGeneration() const { return config_generation_; }
    /** Monotonic sequence of the last successful getInputSnapshot (0 if none). */
    uint64_t lastInputSequence() const { return last_input_sequence_; }
    uint64_t lastInputCycle() const { return last_input_cycle_; }
    /** True when module reports ELC_CAP_DOMAIN_OUTPUT_AUTHORITY (API 0.17+). */
    bool hasDomainOutputAuthority() const { return domain_output_authority_; }
    uint64_t capabilities() const { return capabilities_; }

  private:
    elc_handle *handle = nullptr;
    bool apiNegotiated = false;
    bool domain_output_authority_ = false;
    uint64_t capabilities_ = 0;
    uint32_t domain_size_ = 0;
    uint64_t config_generation_ = 0;
    uint64_t last_input_sequence_ = 0;
    uint64_t last_input_cycle_ = 0;
};

#endif
