/*
 * KernelEthercatBus — libelcethercat adapter implementation.
 */

#include "KernelEthercatBus.h"
#include "DebugExtra.h"
#include "MessageLog.h"
#include <cassert>
#include <cstring>
#include <cerrno>

KernelEthercatBus::KernelEthercatBus() = default;

KernelEthercatBus::~KernelEthercatBus() { close(); }

int KernelEthercatBus::open(const char *device_path) {
    if (handle) {
        return 0;
    }

    int ret = elc_open(device_path, &handle);
    if (ret != 0) {
        char buf[160];
        snprintf(buf, sizeof(buf), "KernelEthercatBus::open failed: %d (%s)", ret,
                 strerror(-ret));
        MessageLog::instance()->add(buf);
        DBG_ETHERCAT << buf << "\n";
        handle = nullptr;
        return ret;
    }

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
    DBG_ETHERCAT << "KernelEthercatBus opened (" << device_path << ")\n";
    return 0;
}

void KernelEthercatBus::close() {
    if (!handle) {
        return;
    }
    struct elc_cycle_deactivate deact = {};
    elc_init_api_header(&deact, sizeof(deact));
    elc_cycle_deactivate(handle, &deact);
    elc_close(handle);
    handle = nullptr;
    apiNegotiated = false;
    domain_size_ = 0;
    config_generation_ = 0;
    DBG_ETHERCAT << "KernelEthercatBus closed\n";
}

std::vector<ec_slave_info_t> KernelEthercatBus::listSlaves() {
    std::vector<ec_slave_info_t> slaves;
    if (!handle || !apiNegotiated) {
        return slaves;
    }

    struct elc_master_info minfo = {};
    elc_init_api_header(&minfo, sizeof(minfo));
    if (elc_get_master_info(handle, &minfo) != 0) {
        return slaves;
    }

    size_t count = 0;
    std::vector<elc_slave_summary> summaries(minfo.slave_count);
    if (elc_list_slaves(handle, summaries.data(), summaries.size(), &count) != 0) {
        return slaves;
    }
    for (size_t i = 0; i < count && i < summaries.size(); ++i) {
        const auto &s = summaries[i];
        ec_slave_info_t info = {};
        info.position = s.position;
        info.alias = s.alias;
        info.vendor_id = s.vendor_id;
        info.product_code = s.product_code;
        info.revision_number = s.revision_number;
        info.serial_number = s.serial_number;
        info.al_state = s.al_state;
        strncpy(info.name, s.name, sizeof(info.name) - 1);
        info.name[sizeof(info.name) - 1] = '\0';
        slaves.push_back(info);
    }
    return slaves;
}

int KernelEthercatBus::setupBegin() {
    if (!handle) {
        return -EINVAL;
    }
    return elc_setup_begin(handle);
}

int KernelEthercatBus::setupAddSDO(const struct elc_setup_sdo *sdo) {
    if (!handle || !sdo) {
        return -EINVAL;
    }
    return elc_setup_add_sdo(handle, sdo);
}

int KernelEthercatBus::setupApply(struct elc_setup_apply *result) {
    if (!handle || !result) {
        return -EINVAL;
    }
    return elc_setup_apply(handle, result);
}

int KernelEthercatBus::setupReset() {
    if (!handle) {
        return -EINVAL;
    }
    return elc_setup_reset(handle);
}

int KernelEthercatBus::sdoUpload(struct elc_sdo_upload *req) {
    if (!handle || !req) {
        return -EINVAL;
    }
    return elc_sdo_upload(handle, req);
}

int KernelEthercatBus::configBegin() {
    if (!handle) {
        return -EINVAL;
    }
    return elc_config_begin(handle);
}

int KernelEthercatBus::configAddSlave(const struct elc_config_slave *slave) {
    if (!handle || !slave) {
        return -EINVAL;
    }
    return elc_config_add_slave(handle, slave);
}

int KernelEthercatBus::configAddSync(const struct elc_config_sync *sync) {
    if (!handle || !sync) {
        return -EINVAL;
    }
    return elc_config_add_sync(handle, sync);
}

int KernelEthercatBus::configAddPdo(const struct elc_config_pdo *pdo) {
    if (!handle || !pdo) {
        return -EINVAL;
    }
    return elc_config_add_pdo(handle, pdo);
}

int KernelEthercatBus::configAddEntry(const struct elc_config_entry *entry) {
    if (!handle || !entry) {
        return -EINVAL;
    }
    return elc_config_add_entry(handle, entry);
}

int KernelEthercatBus::configAddDomain(const struct elc_config_domain *domain) {
    if (!handle || !domain) {
        return -EINVAL;
    }
    return elc_config_add_domain(handle, domain);
}

int KernelEthercatBus::configAddDomainAssignment(
    const struct elc_config_domain_assignment *asgn) {
    if (!handle || !asgn) {
        return -EINVAL;
    }
    return elc_config_add_domain_assignment(handle, asgn);
}

int KernelEthercatBus::configValidate(struct elc_config_validate *result) {
    if (!handle || !result) {
        return -EINVAL;
    }
    return elc_config_validate(handle, result);
}

int KernelEthercatBus::configApply(struct elc_config_apply *result) {
    if (!handle || !result) {
        return -EINVAL;
    }
    return elc_config_apply(handle, result);
}

int KernelEthercatBus::domainCreate(struct elc_domain_create *result) {
    if (!handle || !result) {
        return -EINVAL;
    }
    int ret = elc_domain_create(handle, result);
    if (ret == 0 && result->result == 0) {
        struct elc_io_status st = {};
        elc_init_api_header(&st, sizeof(st));
        if (elc_get_io_status(handle, &st) == 0) {
            domain_size_ = st.domain_size;
            config_generation_ = st.config_generation;
        }
    }
    return ret;
}

int KernelEthercatBus::getEntryOffset(struct elc_entry_offset *io) {
    if (!handle || !io) {
        return -EINVAL;
    }
    return elc_get_entry_offset(handle, io);
}

int KernelEthercatBus::cycleActivate(uint32_t period_ns, uint32_t flags,
                                     struct elc_cycle_activate *out) {
    if (!handle) {
        return -EINVAL;
    }
    struct elc_cycle_activate local = {};
    elc_init_api_header(&local, sizeof(local));
    local.cycle_period_ns = period_ns;
    local.flags = flags;
    int ret = elc_cycle_activate(handle, period_ns, flags, &local);
    if (out) {
        *out = local;
    }
    if (ret == 0 && local.result == 0) {
        domain_size_ = local.domain_size;
    }
    return ret;
}

int KernelEthercatBus::cycleDeactivate(struct elc_cycle_deactivate *out) {
    if (!handle) {
        return -EINVAL;
    }
    struct elc_cycle_deactivate local = {};
    elc_init_api_header(&local, sizeof(local));
    int ret = elc_cycle_deactivate(handle, &local);
    if (out) {
        *out = local;
    }
    return ret;
}

int KernelEthercatBus::cycleWait(struct elc_cycle_wait *wait) {
    if (!handle || !wait) {
        return -EINVAL;
    }
    return elc_cycle_wait(handle, wait);
}

int KernelEthercatBus::cycleStatus(struct elc_cycle_status *st) {
    if (!handle || !st) {
        return -EINVAL;
    }
    return elc_cycle_status(handle, st);
}

int KernelEthercatBus::getInputSnapshot(void *buf, size_t len, struct elc_input_snapshot *snap) {
    if (!handle || !buf || !snap) {
        return -EINVAL;
    }
    elc_init_api_header(snap, sizeof(*snap));
    snap->data_ptr = reinterpret_cast<uint64_t>(buf);
    snap->data_capacity = static_cast<uint32_t>(len);
    return elc_get_input_snapshot(handle, snap, buf, len);
}

int KernelEthercatBus::publishOutput(const void *image, const void *mask, size_t len,
                                     struct elc_output_publish *pub) {
    if (!handle || !image || !mask || !pub) {
        return -EINVAL;
    }
    elc_init_api_header(pub, sizeof(*pub));
    pub->data_ptr = reinterpret_cast<uint64_t>(image);
    pub->mask_ptr = reinterpret_cast<uint64_t>(mask);
    pub->data_size = static_cast<uint32_t>(len);
    return elc_publish_output(handle, image, mask, len, pub);
}

int KernelEthercatBus::armOutput(struct elc_output_arm *arm) {
    if (!handle || !arm) {
        return -EINVAL;
    }
    return elc_arm_output(handle, arm);
}

int KernelEthercatBus::disarmOutput(struct elc_output_disarm *disarm) {
    if (!handle) {
        return -EINVAL;
    }
    struct elc_output_disarm local = {};
    elc_init_api_header(&local, sizeof(local));
    int ret = elc_disarm_output(handle, &local);
    if (disarm) {
        *disarm = local;
    }
    return ret;
}

int KernelEthercatBus::getIoStatus(struct elc_io_status *st) {
    if (!handle || !st) {
        return -EINVAL;
    }
    elc_init_api_header(st, sizeof(*st));
    return elc_get_io_status(handle, st);
}
