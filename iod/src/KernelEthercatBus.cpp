/*
 * KernelEthercatBus.cpp -- Implementation for libelcethercat adapter (Phase 8 only)
 * 
 * Implements discovery and SDO mailbox using the library API from TRANSPORT.md.
 * Maps results to existing ec_slave_info_t for seamless integration with ECInterface, 
 * EtherCATSetup, XML parser, and iod.cpp.
 * 
 * Never calls config, cycle, publish, or arm functions (those are later phases).
 * Enforces exclusivity with legacy ecrt path.
 */

#include "KernelEthercatBus.h"
#include "DebugExtra.h"
#include "MessageLog.h"
#include <cstring>
#include <cassert>

KernelEthercatBus::KernelEthercatBus() = default;

KernelEthercatBus::~KernelEthercatBus() {
    close();
}

int KernelEthercatBus::open(const char* device_path) {
    if (handle) return 0; // already open

    int ret = elc_open(device_path, &handle);
    if (ret != 0) {
        char buf[128];
        snprintf(buf, sizeof(buf), "KernelEthercatBus::open failed: %d (%s)", ret, strerror(-ret));
        MessageLog::instance()->add(buf);
        DBG_ETHERCAT << buf << "\n";
        handle = nullptr;
        return ret;
    }

    // Negotiate API (require 0.16 as per library docs)
    ret = elc_require_api(handle, 0, 16);
    if (ret != 0) {
        elc_close(handle);
        handle = nullptr;
        char buf[128];
        snprintf(buf, sizeof(buf), "KernelEthercatBus API negotiation failed: %d", ret);
        MessageLog::instance()->add(buf);
        DBG_ETHERCAT << buf << "\n";
        return ret;
    }

    apiNegotiated = true;
    DBG_ETHERCAT << "KernelEthercatBus opened successfully with libelcethercat (Phase 8)\n";
    return 0;
}

void KernelEthercatBus::close() {
    if (handle) {
        elc_close(handle);
        handle = nullptr;
        apiNegotiated = false;
        DBG_ETHERCAT << "KernelEthercatBus closed (master released)\n";
    }
}

std::vector<ec_slave_info_t> KernelEthercatBus::listSlaves() {
    std::vector<ec_slave_info_t> slaves;
    if (!handle || !apiNegotiated) return slaves;

    struct elc_master_info minfo = {};
    if (elc_get_master_info(handle, &minfo) != 0) return slaves;

    size_t count = 0;
    std::vector<elc_slave_summary> summaries(minfo.slave_count);
    if (elc_list_slaves(handle, summaries.data(), summaries.size(), &count) == 0) {
        for (size_t i = 0; i < count && i < summaries.size(); ++i) {
            const auto& s = summaries[i];
            ec_slave_info_t info = {};
            info.position = s.position;
            info.alias = s.alias;
            info.vendor_id = s.vendor_id;
            info.product_code = s.product_code;
            info.revision_number = s.revision_number;
            info.serial_number = s.serial_number;
            info.al_state = s.al_state;
            // name is char[ELC_SLAVE_NAME_LEN]; copy safely
            strncpy(info.name, s.name, sizeof(info.name)-1);
            info.name[sizeof(info.name)-1] = '\0';
            slaves.push_back(info);
        }
    }
    return slaves;
}

// SDO Mailbox implementations (Phase 8)
int KernelEthercatBus::setupBegin() {
    if (!handle) return -EINVAL;
    return elc_setup_begin(handle);
}

int KernelEthercatBus::setupAddSDO(const struct elc_setup_sdo* sdo) {
    if (!handle || !sdo) return -EINVAL;
    return elc_setup_add_sdo(handle, sdo);
}

int KernelEthercatBus::setupApply(struct elc_setup_apply* result) {
    if (!handle || !result) return -EINVAL;
    return elc_setup_apply(handle, result);
}

int KernelEthercatBus::setupReset() {
    if (!handle) return -EINVAL;
    return elc_setup_reset(handle);
}

int KernelEthercatBus::sdoUpload(struct elc_sdo_upload* req) {
    if (!handle || !req) return -EINVAL;
    return elc_sdo_upload(handle, req);
}
