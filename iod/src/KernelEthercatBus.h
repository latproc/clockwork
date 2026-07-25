/*
 * KernelEthercatBus.h -- Thin adapter for libelcethercat (Phase 8: discovery + SDO mailbox)
 * 
 * This is the new kernel transport path for iod-elc. It provides:
 * - elc_open / elc_close with API negotiation and EBUSY guard (mutual exclusivity with legacy ecrt).
 * - listSlaves() mapping to existing ec_slave_info_t for compatibility with ECModule, XML parser, etc.
 * - SDO mailbox (elc_setup_* and elc_sdo_upload) for sdo.lpc, servo setup, and iod.sh boot script.
 * 
 * No cyclic, config, or output calls (those are Phase 9+).
 * Legacy iod/iod_sdo remain unchanged.
 */

#ifndef KERNEL_ETHERCAT_BUS_H
#define KERNEL_ETHERCAT_BUS_H

#include "elc_ethercat.h"
#include <vector>
#include "ECInterface.h"  // for ec_slave_info_t compatibility

class KernelEthercatBus {
public:
    KernelEthercatBus();
    ~KernelEthercatBus();

    int open(const char* device_path = "/dev/elc_ethercat0");
    void close();

    bool isOpen() const { return handle != nullptr; }

    // Discovery (Phase 8 core)
    std::vector<ec_slave_info_t> listSlaves();

    // SDO Mailbox (required for sdo.lpc and servo setup)
    int setupBegin();
    int setupAddSDO(const struct elc_setup_sdo* sdo);
    int setupApply(struct elc_setup_apply* result);
    int setupReset();
    int sdoUpload(struct elc_sdo_upload* req);

private:
    elc_handle* handle = nullptr;
    bool apiNegotiated = false;
};

#endif // KERNEL_ETHERCAT_BUS_H
