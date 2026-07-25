/*
 * ecrt_stubs_elc.cpp -- Link stubs for iod-elc (USE_KERNEL_ETHERCAT builds only).
 *
 * iod-elc intentionally does not link against the EtherLab userspace library.
 * Phase 8 runtime uses KernelEthercatBus / libelcethercat for open, discovery,
 * and SDO mailbox. Legacy ecrt symbols still appear in shared ECInterface.cpp
 * sources (cyclic, DC, PDO config, SDO request objects). These stubs satisfy
 * the linker with safe no-ops so the binary can run the kernel path without
 * opening a legacy master.
 *
 * Do not link this file into iod or iod_sdo.
 */

#include <ecrt.h>
#include <cstring>
#include <cstdint>

namespace {

/* Opaque-ish placeholders so non-null pointers can flow through legacy code. */
struct StubMaster {
    int dummy;
};
struct StubDomain {
    int dummy;
};
struct StubSlaveConfig {
    int dummy;
};
struct StubSdoRequest {
    uint8_t data[64];
    int state;
};

StubMaster g_master;
StubDomain g_domain;
StubSlaveConfig g_sc;
StubSdoRequest g_sdo;
uint8_t g_domain_pd[4096];
ec_master_state_t g_master_state = {};
ec_domain_state_t g_domain_state = {};
ec_slave_config_state_t g_sc_state = {};

} // namespace

extern "C" {

ec_master_t *ecrt_request_master(unsigned int /*master_index*/)
{
    return reinterpret_cast<ec_master_t *>(&g_master);
}

int ecrt_master(ec_master_t * /*master*/, ec_master_info_t *master_info)
{
    if (master_info) {
        std::memset(master_info, 0, sizeof(*master_info));
    }
    return 0;
}

int ecrt_master_get_slave(ec_master_t * /*master*/, uint16_t /*slave_position*/,
                         ec_slave_info_t *slave_info)
{
    if (slave_info) {
        std::memset(slave_info, 0, sizeof(*slave_info));
    }
    return -1;
}

int ecrt_master_get_sync_manager(ec_master_t * /*master*/, uint16_t /*slave_position*/,
                                uint8_t /*sync_index*/, ec_sync_info_t *sync)
{
    if (sync) {
        std::memset(sync, 0, sizeof(*sync));
    }
    return -1;
}

int ecrt_master_get_pdo(ec_master_t * /*master*/, uint16_t /*slave_position*/,
                       uint8_t /*sync_index*/, uint16_t /*pos*/, ec_pdo_info_t *pdo)
{
    if (pdo) {
        std::memset(pdo, 0, sizeof(*pdo));
    }
    return -1;
}

int ecrt_master_get_pdo_entry(ec_master_t * /*master*/, uint16_t /*slave_position*/,
                             uint8_t /*sync_index*/, uint16_t /*pdo_pos*/,
                             uint16_t /*entry_pos*/, ec_pdo_entry_info_t *entry)
{
    if (entry) {
        std::memset(entry, 0, sizeof(*entry));
    }
    return -1;
}

ec_domain_t *ecrt_master_create_domain(ec_master_t * /*master*/)
{
    return reinterpret_cast<ec_domain_t *>(&g_domain);
}

ec_slave_config_t *ecrt_master_slave_config(ec_master_t * /*master*/, uint16_t /*alias*/,
                                           uint16_t /*position*/, uint32_t /*vendor_id*/,
                                           uint32_t /*product_code*/)
{
    return reinterpret_cast<ec_slave_config_t *>(&g_sc);
}

int ecrt_master_select_reference_clock(ec_master_t * /*master*/,
                                      ec_slave_config_t * /*sc*/)
{
    return 0;
}

int ecrt_master_activate(ec_master_t * /*master*/)
{
    return 0;
}

int ecrt_master_deactivate(ec_master_t * /*master*/)
{
    return 0;
}

int ecrt_master_send(ec_master_t * /*master*/)
{
    return 0;
}

int ecrt_master_receive(ec_master_t * /*master*/)
{
    return 0;
}

int ecrt_master_state(const ec_master_t * /*master*/, ec_master_state_t *state)
{
    if (state) {
        *state = g_master_state;
    }
    return 0;
}

int ecrt_master_application_time(ec_master_t * /*master*/, uint64_t /*app_time*/)
{
    return 0;
}

int ecrt_master_sync_slave_clocks(ec_master_t * /*master*/)
{
    return 0;
}

int ecrt_master_reference_clock_time(const ec_master_t * /*master*/, uint32_t *time)
{
    if (time) {
        *time = 0;
    }
    return -1;
}

int ecrt_master_sync_monitor_queue(ec_master_t * /*master*/)
{
    return 0;
}

uint32_t ecrt_master_sync_monitor_process(const ec_master_t * /*master*/)
{
    return 0;
}

uint8_t *ecrt_domain_data(const ec_domain_t * /*domain*/)
{
    return g_domain_pd;
}

size_t ecrt_domain_size(const ec_domain_t * /*domain*/)
{
    return 0;
}

int ecrt_domain_process(ec_domain_t * /*domain*/)
{
    return 0;
}

int ecrt_domain_queue(ec_domain_t * /*domain*/)
{
    return 0;
}

int ecrt_domain_state(const ec_domain_t * /*domain*/, ec_domain_state_t *state)
{
    if (state) {
        *state = g_domain_state;
    }
    return 0;
}

int ecrt_slave_config_pdos(ec_slave_config_t * /*sc*/, unsigned int /*n_syncs*/,
                          const ec_sync_info_t /*syncs*/[])
{
    return 0;
}

int ecrt_slave_config_state(const ec_slave_config_t * /*sc*/,
                           ec_slave_config_state_t *state)
{
    if (state) {
        *state = g_sc_state;
        state->online = 1;
        state->operational = 1;
        state->al_state = 8;
    }
    return 0;
}

int ecrt_slave_config_sync_manager(ec_slave_config_t * /*sc*/, uint8_t /*sync_index*/,
                                  ec_direction_t /*dir*/, ec_watchdog_mode_t /*wd*/)
{
    return 0;
}

int ecrt_slave_config_pdo_assign_clear(ec_slave_config_t * /*sc*/, uint8_t /*sync_index*/)
{
    return 0;
}

int ecrt_slave_config_pdo_assign_add(ec_slave_config_t * /*sc*/, uint8_t /*sync_index*/,
                                    uint16_t /*index*/)
{
    return 0;
}

int ecrt_slave_config_pdo_mapping_clear(ec_slave_config_t * /*sc*/, uint16_t /*pdo_index*/)
{
    return 0;
}

int ecrt_slave_config_pdo_mapping_add(ec_slave_config_t * /*sc*/, uint16_t /*pdo_index*/,
                                     uint16_t /*entry_index*/, uint8_t /*entry_subindex*/,
                                     uint8_t /*bit_length*/)
{
    return 0;
}

int ecrt_slave_config_reg_pdo_entry_pos(ec_slave_config_t * /*sc*/, uint8_t /*sync_index*/,
                                       unsigned int /*pdo_pos*/, unsigned int /*entry_pos*/,
                                       ec_domain_t * /*domain*/,
                                       unsigned int *bit_position)
{
    if (bit_position) {
        *bit_position = 0;
    }
    return 0;
}

int ecrt_slave_config_emerg_pop(ec_slave_config_t * /*sc*/, uint8_t * /*target*/)
{
    return -1; /* empty */
}

ec_sdo_request_t *ecrt_slave_config_create_sdo_request(ec_slave_config_t * /*sc*/,
                                                      uint16_t /*index*/,
                                                      uint8_t /*subindex*/,
                                                      size_t /*size*/)
{
    std::memset(g_sdo.data, 0, sizeof(g_sdo.data));
    g_sdo.state = EC_REQUEST_UNUSED;
    return reinterpret_cast<ec_sdo_request_t *>(&g_sdo);
}

uint8_t *ecrt_sdo_request_data(const ec_sdo_request_t *req)
{
    if (!req) {
        return g_sdo.data;
    }
    return const_cast<uint8_t *>(reinterpret_cast<const StubSdoRequest *>(req)->data);
}

int ecrt_sdo_request_timeout(ec_sdo_request_t * /*req*/, uint32_t /*timeout*/)
{
    return 0;
}

ec_request_state_t ecrt_sdo_request_state(ec_sdo_request_t *req)
{
    if (!req) {
        return EC_REQUEST_UNUSED;
    }
    return static_cast<ec_request_state_t>(reinterpret_cast<StubSdoRequest *>(req)->state);
}

int ecrt_sdo_request_read(ec_sdo_request_t *req)
{
    if (req) {
        reinterpret_cast<StubSdoRequest *>(req)->state = EC_REQUEST_SUCCESS;
    }
    return 0;
}

int ecrt_sdo_request_write(ec_sdo_request_t *req)
{
    if (req) {
        reinterpret_cast<StubSdoRequest *>(req)->state = EC_REQUEST_SUCCESS;
    }
    return 0;
}

} // extern "C"
