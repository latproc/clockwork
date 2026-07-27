/*
 * KernelEthercatBus — libelcethercat adapter implementation.
 */

#include "KernelEthercatBus.h"
#include "DebugExtra.h"
#include "MessageLog.h"
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <iostream>
#include <sched.h>
#include <unistd.h>
#include <vector>

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

    // Prefer 0.18 (timeout_ms + publish-renew lease); fall back to 0.16.
    ret = elc_require_api(handle, 0, 18);
    if (ret != 0) {
        ret = elc_require_api(handle, 0, 16);
    }
    if (ret != 0) {
        elc_close(handle);
        handle = nullptr;
        char buf[128];
        snprintf(buf, sizeof(buf), "KernelEthercatBus API negotiation failed: %d", ret);
        MessageLog::instance()->add(buf);
        DBG_ETHERCAT << buf << "\n";
        return ret;
    }

    domain_output_authority_ = false;
    capabilities_ = 0;
    struct elc_capabilities caps = {};
    elc_init_api_header(&caps, sizeof(caps));
    if (elc_get_capabilities(handle, &caps) == 0) {
        capabilities_ = caps.capabilities;
#ifdef ELC_CAP_DOMAIN_OUTPUT_AUTHORITY
        domain_output_authority_ =
            (caps.capabilities & ELC_CAP_DOMAIN_OUTPUT_AUTHORITY) != 0;
#endif
    }

    apiNegotiated = true;
    std::cerr << "KernelEthercatBus opened (" << device_path
              << ") caps=0x" << std::hex << capabilities_ << std::dec
              << " domain_output_authority=" << (domain_output_authority_ ? 1 : 0)
#ifdef ELC_CAP_OUTPUT_LEASE_PUBLISH_RENEW
              << " lease_publish_renew="
              << ((capabilities_ & ELC_CAP_OUTPUT_LEASE_PUBLISH_RENEW) ? 1 : 0)
#endif
              << "\n";
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
    domain_output_authority_ = false;
    capabilities_ = 0;
    domain_size_ = 0;
    config_generation_ = 0;
    last_input_sequence_ = 0;
    last_input_cycle_ = 0;
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

int KernelEthercatBus::sdoDownload(uint16_t position, uint16_t index, uint8_t subindex,
                                   uint8_t type, const uint8_t *data, uint16_t data_len) {
    if (!handle || !data || data_len == 0 || data_len > ELC_SETUP_SDO_DATA_MAX) {
        return -EINVAL;
    }

    int ret = elc_setup_begin(handle);
    if (ret) {
        return ret;
    }

    struct elc_setup_sdo sdo = {};
    sdo.struct_size = sizeof(sdo);
    sdo.api_major = ELC_API_VERSION_MAJOR;
    sdo.sequence = 1;
    sdo.position = position;
    sdo.index = index;
    sdo.subindex = subindex;
    sdo.type = type;
    sdo.data_len = data_len;
    memcpy(sdo.data, data, data_len);

    ret = elc_setup_add_sdo(handle, &sdo);
    if (ret) {
        elc_setup_reset(handle);
        return ret;
    }

    struct elc_setup_apply apply = {};
    apply.struct_size = sizeof(apply);
    apply.api_major = ELC_API_VERSION_MAJOR;
    ret = elc_setup_apply(handle, &apply);
    // Begin on the next call clears applied state; explicit reset keeps the
    // batch list empty if apply failed mid-way.
    if (ret) {
        elc_setup_reset(handle);
        return ret;
    }
    return 0;
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
        // Module-load FIFO priority only applies at insmod. If the module was
        // loaded soft-RT, elc_cycle is SCHED_OTHER and WC stays incomplete
        // under load — promote immediately after the thread exists.
        (void)ensureCycleThreadRealtime(/*cpu=*/1, /*fifo_priority=*/90);
    }
    return ret;
}

int KernelEthercatBus::ensureCycleThreadRealtime(int cpu, int fifo_priority) {
    if (fifo_priority < 1) {
        fifo_priority = 1;
    }
    if (fifo_priority > 99) {
        fifo_priority = 99;
    }

    DIR *proc = opendir("/proc");
    if (!proc) {
        return -errno;
    }

    std::vector<pid_t> tids;
    while (dirent *de = readdir(proc)) {
        if (de->d_name[0] < '1' || de->d_name[0] > '9') {
            continue;
        }
        char path[64];
        snprintf(path, sizeof(path), "/proc/%s/comm", de->d_name);
        int fd = ::open(path, O_RDONLY | O_CLOEXEC);
        if (fd < 0) {
            continue;
        }
        char comm[32] = {};
        const ssize_t n = ::read(fd, comm, sizeof(comm) - 1);
        ::close(fd);
        if (n <= 0) {
            continue;
        }
        // comm is "elc_cycle\n"
        if (strncmp(comm, "elc_cycle", 9) == 0 &&
            (comm[9] == '\0' || comm[9] == '\n')) {
            tids.push_back(static_cast<pid_t>(atoi(de->d_name)));
        }
    }
    closedir(proc);

    if (tids.empty()) {
        return -ESRCH;
    }

    struct sched_param sp = {};
    sp.sched_priority = fifo_priority;
    cpu_set_t set;
    CPU_ZERO(&set);
    if (cpu >= 0) {
        CPU_SET(static_cast<unsigned>(cpu), &set);
    }

    int promoted = 0;
    for (pid_t tid : tids) {
        if (sched_setscheduler(tid, SCHED_FIFO, &sp) != 0) {
            char buf[160];
            snprintf(buf, sizeof(buf),
                     "elc_cycle tid=%d sched_setscheduler(FIFO,%d) failed: %s",
                     (int)tid, fifo_priority, strerror(errno));
            MessageLog::instance()->add(buf);
            std::cerr << buf << "\n";
            continue;
        }
        if (cpu >= 0 && sched_setaffinity(tid, sizeof(set), &set) != 0) {
            char buf[160];
            snprintf(buf, sizeof(buf), "elc_cycle tid=%d sched_setaffinity(cpu %d) failed: %s",
                     (int)tid, cpu, strerror(errno));
            MessageLog::instance()->add(buf);
            std::cerr << buf << "\n";
            // Priority already raised; affinity is best-effort.
        }
        char buf[128];
        snprintf(buf, sizeof(buf),
                 "elc_cycle tid=%d promoted SCHED_FIFO prio=%d cpu=%d", (int)tid,
                 fifo_priority, cpu);
        MessageLog::instance()->add(buf);
        std::cout << buf << "\n";
        ++promoted;
    }
    return promoted > 0 ? 0 : -EPERM;
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

int KernelEthercatBus::cycleSetPeriod(uint32_t period_ns, struct elc_cycle_period_update *out) {
    if (!handle) {
        return -EINVAL;
    }
    if (period_ns < ELC_CYCLE_PERIOD_MIN_NS) {
        period_ns = ELC_CYCLE_PERIOD_MIN_NS;
    }
    if (period_ns > ELC_CYCLE_PERIOD_MAX_NS) {
        period_ns = ELC_CYCLE_PERIOD_MAX_NS;
    }
    struct elc_cycle_period_update local = {};
    elc_init_api_header(&local, sizeof(local));
    local.config_generation = config_generation_;
    local.cycle_period_ns = period_ns;
    int ret = elc_cycle_set_period(handle, &local);
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

int KernelEthercatBus::cycleInfo(struct elc_cycle_info *info) {
    if (!handle || !info) {
        return -EINVAL;
    }
    elc_init_api_header(info, sizeof(*info));
    if (config_generation_) {
        info->config_generation = config_generation_;
    }
    return elc_cycle_info(handle, info);
}

int KernelEthercatBus::cycleDcInfo(struct elc_cycle_dc_info *info) {
    if (!handle || !info) {
        return -EINVAL;
    }
    elc_init_api_header(info, sizeof(*info));
    if (config_generation_) {
        info->config_generation = config_generation_;
    }
    return elc_cycle_dc_info(handle, info);
}

int KernelEthercatBus::getInputSnapshot(void *buf, size_t len, struct elc_input_snapshot *snap) {
    if (!handle || !buf || !snap) {
        return -EINVAL;
    }
    elc_init_api_header(snap, sizeof(*snap));
    // Bind to the generation we applied so the lib skips an extra IO-status
    // round-trip and the kernel returns the current active double-buffer for
    // that generation (always the latest published coherent image).
    if (config_generation_) {
        snap->config_generation = config_generation_;
    }
    snap->data_ptr = reinterpret_cast<uint64_t>(buf);
    snap->data_capacity = static_cast<uint32_t>(len);
    int ret = elc_get_input_snapshot(handle, snap, buf, len);
    if (ret == 0) {
        if (snap->config_generation) {
            config_generation_ = snap->config_generation;
        }
        last_input_sequence_ = snap->input_sequence;
        last_input_cycle_ = snap->cycle_count;
    }
    return ret;
}

int KernelEthercatBus::publishOutput(const void *image, const void *mask, size_t len,
                                     struct elc_output_publish *pub) {
    if (!handle || !image || !mask || !pub) {
        return -EINVAL;
    }
    elc_init_api_header(pub, sizeof(*pub));
    // Always publish against the generation we activated so arm() can use the
    // returned output_sequence as the exact latest publication for that gen.
    if (config_generation_) {
        pub->config_generation = config_generation_;
    }
    // domain_config_id 0 = full global image (fans out to every domain authority
    // under API 0.17). Callers may override before invoke if segment publish is
    // needed; default remains global so IO + drives stay in one shadow.
    pub->domain_config_id = 0;
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

int KernelEthercatBus::configureOutputLease(uint32_t timeout_ms, uint32_t domain_config_id) {
    if (!handle) {
        return -EINVAL;
    }
    if (!hasOutputLease()) {
        return -ENOTSUP;
    }
    if (!config_generation_) {
        return -EINVAL;
    }
#ifdef ELC_OUTPUT_LEASE_TIMEOUT_MS_MAX
    if (timeout_ms > ELC_OUTPUT_LEASE_TIMEOUT_MS_MAX) {
        return -EINVAL;
    }
#endif
    // timeout_ms==0 and cycle_budget==0 disables lease on target domains.
    struct elc_output_lease_config cfg = {};
    elc_init_api_header(&cfg, sizeof(cfg));
    cfg.config_generation = config_generation_;
    cfg.flags = domain_config_id; // 0 = all domains
    cfg.timeout_ms = timeout_ms;
    cfg.cycle_budget = 0; // derive from timeout_ms when non-zero (API 0.18)
    return elc_configure_output_lease(handle, &cfg);
}

int KernelEthercatBus::renewOutputLease(struct elc_output_lease_renew *renew) {
    if (!handle) {
        return -EINVAL;
    }
    if (!hasOutputLease()) {
        return -ENOTSUP;
    }
    struct elc_output_lease_renew local = {};
    elc_init_api_header(&local, sizeof(local));
    if (config_generation_) {
        local.config_generation = config_generation_;
    }
    int ret = elc_renew_output_lease(handle, &local);
    if (renew) {
        *renew = local;
    }
    return ret;
}

int KernelEthercatBus::getOutputLeaseStatus(struct elc_output_lease_status *st) {
    if (!handle || !st) {
        return -EINVAL;
    }
    elc_init_api_header(st, sizeof(*st));
    if (config_generation_) {
        st->config_generation = config_generation_;
    }
    return elc_get_output_lease_status(handle, st);
}

int KernelEthercatBus::getIoStatus(struct elc_io_status *st) {
    if (!handle || !st) {
        return -EINVAL;
    }
    elc_init_api_header(st, sizeof(*st));
    return elc_get_io_status(handle, st);
}

int KernelEthercatBus::getSlaveInfo(uint16_t position, struct elc_slave_info *info) {
    if (!handle || !info) {
        return -EINVAL;
    }
    elc_init_api_header(info, sizeof(*info));
    info->position = position;
    return elc_get_slave_info(handle, position, info);
}

int KernelEthercatBus::getConfigSlaveStatus(uint32_t config_id,
                                            struct elc_config_slave_status *st) {
    if (!handle || !st) {
        return -EINVAL;
    }
    elc_init_api_header(st, sizeof(*st));
    st->config_id = config_id;
    st->config_generation = config_generation_;
    return elc_get_config_slave_status(handle, st);
}

int KernelEthercatBus::getDomainStatus(uint32_t domain_config_id, struct elc_domain_status *st) {
    if (!handle || !st) {
        return -EINVAL;
    }
    elc_init_api_header(st, sizeof(*st));
    st->domain_config_id = domain_config_id;
    if (config_generation_) {
        st->config_generation = config_generation_;
    }
    return elc_get_domain_status(handle, st);
}
