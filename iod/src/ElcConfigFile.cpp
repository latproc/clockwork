/*
 * Load declarative elc topology/config files (tools/elc_config format).
 */

#include "ElcConfigFile.h"
#include "KernelEthercatBus.h"
#include "ECInterface.h"
#include "DebugExtra.h"
#include "MessageLog.h"

#include <unistd.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct ParsedEntry {
    uint32_t config_id = 0;
    uint32_t pdo_config_id = 0;
    uint32_t entry_id = 0;
    uint16_t index = 0;
    uint8_t subindex = 0;
    uint8_t bit_length = 0;
};

struct ParsedPdo {
    uint32_t config_id = 0;
    uint32_t sync_config_id = 0;
    uint16_t pdo_index = 0;
    std::vector<ParsedEntry> entries;
};

struct ParsedSync {
    uint32_t config_id = 0;
    uint32_t slave_config_id = 0;
    uint8_t sync_index = 0;
    uint8_t direction = ELC_DIR_OUTPUT;
    uint8_t watchdog = ELC_WD_DEFAULT;
    std::vector<ParsedPdo> pdos;
};

struct ParsedSlave {
    uint32_t config_id = 0;
    uint16_t alias = 0;
    uint16_t position = 0;
    uint32_t vendor_id = 0;
    uint32_t product_code = 0;
    uint32_t revision = 0;
    std::vector<ParsedSync> syncs;
};

struct ParsedDomain {
    uint32_t config_id = 0;
};

struct ParsedDomainAssign {
    uint32_t config_id = 0;
    uint32_t slave_config_id = 0;
    uint32_t domain_config_id = 0;
};

struct ParsedFile {
    std::vector<ParsedSlave> slaves;
    std::vector<ParsedDomain> domains;
    std::vector<ParsedDomainAssign> domain_assigns;
    // id -> index in parent container (resolved after full parse)
    std::map<uint32_t, size_t> slave_ix;
};

static uint32_t parse_u32(const std::string &s) {
    return static_cast<uint32_t>(strtoul(s.c_str(), nullptr, 0));
}
static uint16_t parse_u16(const std::string &s) {
    return static_cast<uint16_t>(strtoul(s.c_str(), nullptr, 0));
}
static uint8_t parse_u8(const std::string &s) {
    return static_cast<uint8_t>(strtoul(s.c_str(), nullptr, 0));
}

// Flat first pass: store raw records, second pass attach to parents by id.
struct FlatSync {
    ParsedSync s;
};
struct FlatPdo {
    ParsedPdo p;
};
struct FlatEntry {
    ParsedEntry e;
};

static int parse_file(const char *path, ParsedFile &out) {
    std::ifstream in(path);
    if (!in) {
        return -ENOENT;
    }

    std::vector<ParsedSlave> slaves;
    std::vector<FlatSync> syncs;
    std::vector<FlatPdo> pdos;
    std::vector<FlatEntry> entries;
    std::vector<ParsedDomain> domains;
    std::vector<ParsedDomainAssign> assigns;

    std::string line;
    unsigned lineno = 0;
    while (std::getline(in, line)) {
        ++lineno;
        auto hash = line.find('#');
        if (hash != std::string::npos) {
            line.resize(hash);
        }
        std::istringstream iss(line);
        std::string tok;
        if (!(iss >> tok)) {
            continue;
        }
        try {
            if (tok == "slave") {
                std::string a, b, c, d, e, f;
                if (!(iss >> a >> b >> c >> d >> e >> f)) {
                    return -EINVAL;
                }
                ParsedSlave s;
                s.config_id = parse_u32(a);
                s.alias = parse_u16(b);
                s.position = parse_u16(c);
                s.vendor_id = parse_u32(d);
                s.product_code = parse_u32(e);
                s.revision = parse_u32(f);
                slaves.push_back(s);
            }
            else if (tok == "sync") {
                std::string a, b, c, dir, wd;
                if (!(iss >> a >> b >> c >> dir >> wd)) {
                    return -EINVAL;
                }
                FlatSync fs;
                fs.s.config_id = parse_u32(a);
                fs.s.slave_config_id = parse_u32(b);
                fs.s.sync_index = parse_u8(c);
                fs.s.direction = (dir == "input") ? ELC_DIR_INPUT : ELC_DIR_OUTPUT;
                if (wd == "enable") {
                    fs.s.watchdog = ELC_WD_ENABLE;
                }
                else if (wd == "disable") {
                    fs.s.watchdog = ELC_WD_DISABLE;
                }
                else {
                    fs.s.watchdog = ELC_WD_DEFAULT;
                }
                syncs.push_back(fs);
            }
            else if (tok == "pdo") {
                std::string a, b, c;
                if (!(iss >> a >> b >> c)) {
                    return -EINVAL;
                }
                FlatPdo fp;
                fp.p.config_id = parse_u32(a);
                fp.p.sync_config_id = parse_u32(b);
                fp.p.pdo_index = parse_u16(c);
                pdos.push_back(fp);
            }
            else if (tok == "entry") {
                std::string a, b, c, d, e, f;
                if (!(iss >> a >> b >> c >> d >> e >> f)) {
                    return -EINVAL;
                }
                FlatEntry fe;
                fe.e.config_id = parse_u32(a);
                fe.e.pdo_config_id = parse_u32(b);
                fe.e.entry_id = parse_u32(c);
                fe.e.index = parse_u16(d);
                fe.e.subindex = parse_u8(e);
                fe.e.bit_length = parse_u8(f);
                entries.push_back(fe);
            }
            else if (tok == "domain") {
                std::string a;
                if (!(iss >> a)) {
                    return -EINVAL;
                }
                ParsedDomain d;
                d.config_id = parse_u32(a);
                domains.push_back(d);
            }
            else if (tok == "domain_slave") {
                std::string a, b, c;
                if (!(iss >> a >> b >> c)) {
                    return -EINVAL;
                }
                ParsedDomainAssign da;
                da.config_id = parse_u32(a);
                da.slave_config_id = parse_u32(b);
                da.domain_config_id = parse_u32(c);
                assigns.push_back(da);
            }
        }
        catch (...) {
            std::cerr << "elc config parse error at " << path << ":" << lineno << "\n";
            return -EINVAL;
        }
    }

    // Index slaves
    std::map<uint32_t, size_t> slave_ix;
    for (size_t i = 0; i < slaves.size(); ++i) {
        slave_ix[slaves[i].config_id] = i;
    }
    // Attach syncs
    std::map<uint32_t, std::pair<size_t, size_t>> sync_loc; // id -> (slave_i, sync_i)
    for (const auto &fs : syncs) {
        auto it = slave_ix.find(fs.s.slave_config_id);
        if (it == slave_ix.end()) {
            std::cerr << "sync " << fs.s.config_id << " references unknown slave "
                      << fs.s.slave_config_id << "\n";
            return -EINVAL;
        }
        size_t si = it->second;
        slaves[si].syncs.push_back(fs.s);
        sync_loc[fs.s.config_id] = {si, slaves[si].syncs.size() - 1};
    }
    // Attach pdos
    std::map<uint32_t, std::tuple<size_t, size_t, size_t>> pdo_loc;
    for (const auto &fp : pdos) {
        auto it = sync_loc.find(fp.p.sync_config_id);
        if (it == sync_loc.end()) {
            std::cerr << "pdo " << fp.p.config_id << " references unknown sync "
                      << fp.p.sync_config_id << "\n";
            return -EINVAL;
        }
        size_t si = it->second.first;
        size_t syi = it->second.second;
        slaves[si].syncs[syi].pdos.push_back(fp.p);
        pdo_loc[fp.p.config_id] = {si, syi, slaves[si].syncs[syi].pdos.size() - 1};
    }
    // Attach entries
    for (const auto &fe : entries) {
        auto it = pdo_loc.find(fe.e.pdo_config_id);
        if (it == pdo_loc.end()) {
            std::cerr << "entry " << fe.e.config_id << " references unknown pdo "
                      << fe.e.pdo_config_id << "\n";
            return -EINVAL;
        }
        size_t si = std::get<0>(it->second);
        size_t syi = std::get<1>(it->second);
        size_t pi = std::get<2>(it->second);
        slaves[si].syncs[syi].pdos[pi].entries.push_back(fe.e);
    }

    out.slaves = std::move(slaves);
    out.domains = std::move(domains);
    out.domain_assigns = std::move(assigns);
    out.slave_ix = std::move(slave_ix);
    return 0;
}

} // namespace

const char *elcDefaultTopologyConfigPath() {
    const char *env = getenv("ELC_TOPOLOGY_CONFIG");
    if (env && env[0]) {
        return env;
    }
    // Prefer plant topology under iod/configs. ELC_TOPOLOGY_CONFIG overrides.
    // all34_captured_topology.conf is a legacy name (symlink on this plant).
    static const char *candidates[] = {
        "/opt/latproc/iod/configs/elc_topology.conf",
        "/opt/latproc/iod/configs/all34_captured_topology.conf",
        "/opt/etherlab-cyclic-kmod/tools/configs/elc_topology.conf",
        "/opt/etherlab-cyclic-kmod/tools/configs/all34_captured_topology.conf",
        nullptr};
    for (int i = 0; candidates[i]; ++i) {
        if (access(candidates[i], R_OK) == 0) {
            return candidates[i];
        }
    }
    return candidates[0];
}

int elcApplyConfigFile(KernelEthercatBus *bus, const char *path,
                       std::vector<uint32_t> *domain_ids_out) {
    if (!bus || !bus->isOpen() || !path) {
        return -EINVAL;
    }
    ParsedFile file;
    int pret = parse_file(path, file);
    if (pret != 0) {
        std::cerr << "Failed to parse ELC config " << path << " (" << pret << ")\n";
        return pret;
    }
    if (domain_ids_out) {
        domain_ids_out->clear();
        for (const auto &d : file.domains) {
            domain_ids_out->push_back(d.config_id);
        }
    }

    int ret = bus->configBegin();
    if (ret != 0) {
        std::cerr << "elc_config_begin failed: " << ret << "\n";
        return ret;
    }

    for (const auto &d : file.domains) {
        struct elc_config_domain dom = {};
        elc_init_api_header(&dom, sizeof(dom));
        dom.config_id = d.config_id;
        ret = bus->configAddDomain(&dom);
        if (ret != 0) {
            std::cerr << "configAddDomain " << d.config_id << " failed: " << ret << "\n";
            return ret;
        }
    }

    for (const auto &s : file.slaves) {
        struct elc_config_slave slave = {};
        elc_fill_config_slave(&slave, s.config_id, s.position, s.alias, s.vendor_id,
                              s.product_code, s.revision, 0);
        ret = bus->configAddSlave(&slave);
        if (ret != 0) {
            std::cerr << "configAddSlave id " << s.config_id << " pos " << s.position
                      << " failed: " << ret << "\n";
            return ret;
        }
        for (const auto &sy : s.syncs) {
            struct elc_config_sync sync = {};
            elc_init_api_header(&sync, sizeof(sync));
            sync.config_id = sy.config_id;
            sync.slave_config_id = sy.slave_config_id;
            sync.sync_index = sy.sync_index;
            sync.direction = sy.direction;
            sync.watchdog_mode = sy.watchdog;
            ret = bus->configAddSync(&sync);
            if (ret != 0) {
                std::cerr << "configAddSync " << sy.config_id << " failed: " << ret << "\n";
                return ret;
            }
            for (const auto &p : sy.pdos) {
                struct elc_config_pdo pdo = {};
                elc_init_api_header(&pdo, sizeof(pdo));
                pdo.config_id = p.config_id;
                pdo.sync_config_id = p.sync_config_id;
                pdo.pdo_index = p.pdo_index;
                ret = bus->configAddPdo(&pdo);
                if (ret != 0) {
                    std::cerr << "configAddPdo " << p.config_id << " failed: " << ret << "\n";
                    return ret;
                }
                for (const auto &e : p.entries) {
                    struct elc_config_entry ent = {};
                    elc_init_api_header(&ent, sizeof(ent));
                    ent.config_id = e.config_id;
                    ent.pdo_config_id = e.pdo_config_id;
                    ent.entry_id = e.entry_id;
                    ent.index = e.index;
                    ent.subindex = e.subindex;
                    ent.bit_length = e.bit_length;
                    ret = bus->configAddEntry(&ent);
                    if (ret != 0) {
                        std::cerr << "configAddEntry " << e.config_id << " failed: " << ret
                                  << "\n";
                        return ret;
                    }
                }
            }
        }
    }

    for (const auto &da : file.domain_assigns) {
        struct elc_config_domain_assignment asgn = {};
        elc_init_api_header(&asgn, sizeof(asgn));
        asgn.config_id = da.config_id;
        asgn.slave_config_id = da.slave_config_id;
        asgn.domain_config_id = da.domain_config_id;
        ret = bus->configAddDomainAssignment(&asgn);
        if (ret != 0) {
            std::cerr << "configAddDomainAssignment " << da.config_id << " failed: " << ret
                      << "\n";
            return ret;
        }
    }

    struct elc_config_validate val = {};
    elc_init_api_header(&val, sizeof(val));
    ret = bus->configValidate(&val);
    if (ret != 0 || val.result != 0) {
        std::cerr << "elc_config_validate failed ret=" << ret << " result=" << val.result << "\n";
        return ret ? ret : -EINVAL;
    }
    struct elc_config_apply apply = {};
    elc_init_api_header(&apply, sizeof(apply));
    ret = bus->configApply(&apply);
    if (ret != 0 || apply.result != 0) {
        std::cerr << "elc_config_apply failed ret=" << ret << " result=" << apply.result
                  << " id=" << apply.failed_config_id << "\n";
        return ret ? ret : -EINVAL;
    }
    struct elc_domain_create domc = {};
    elc_init_api_header(&domc, sizeof(domc));
    ret = bus->domainCreate(&domc);
    if (ret != 0 || domc.result != 0) {
        std::cerr << "elc_domain_create failed ret=" << ret << " result=" << domc.result << "\n";
        return ret ? ret : -EINVAL;
    }
    std::cout << "ELC topology loaded from " << path << ": " << file.slaves.size()
              << " slaves, domain_size=" << bus->domainSize();
    if (file.domains.empty()) {
        std::cout << " (implicit single domain — no domain records)\n";
    }
    else {
        std::cout << ", explicit_domains=" << file.domains.size()
                  << " assignments=" << file.domain_assigns.size() << "\n";
        for (const auto &d : file.domains) {
            unsigned n_asgn = 0;
            for (const auto &da : file.domain_assigns) {
                if (da.domain_config_id == d.config_id) {
                    ++n_asgn;
                }
            }
            std::cout << "  domain " << d.config_id << ": " << n_asgn << " slave(s)\n";
        }
        // Roles are plant-defined. First declared domain is primary for
        // ETHERCAT.slave_states / ETHERCAT_WC / all_ok. Others are isolatable
        // groups (ECDomain_<id> on L_ECDomains).
        if (!file.domains.empty()) {
            std::cout << "  primary domain_config_id=" << file.domains.front().config_id
                      << " (first declared); others isolatable via L_ECDomains\n";
        }
    }
    return 0;
}

int elcPopulateModulesFromConfigFile(KernelEthercatBus *bus, const char *path) {
    if (!bus || !bus->isOpen() || !path) {
        return -EINVAL;
    }
    ParsedFile file;
    int pret = parse_file(path, file);
    if (pret != 0) {
        return pret;
    }

    for (const auto &s : file.slaves) {
        ECModule *m = ECInterface::findModule(s.position);
        if (!m) {
            // ensure slot exists
            m = new ECModule();
            m->name = "pos" + std::to_string(s.position);
            m->alias = s.alias;
            m->position = s.position;
            m->vendor_id = s.vendor_id;
            m->product_code = s.product_code;
            m->revision_no = s.revision;
            auto res = ECInterface::instance()->addModule(m, true);
            if (!res) {
                delete m;
                m = ECInterface::findModule(s.position);
            }
        }
        if (!m) {
            continue;
        }
        m->vendor_id = s.vendor_id;
        m->product_code = s.product_code;
        m->alias = s.alias;
        m->revision_no = s.revision;
        m->elc_config_id = s.config_id;
        // Map slave_config_id → domain_config_id from domain_slave lines.
        m->elc_domain_id = 0;
        for (const auto &da : file.domain_assigns) {
            if (da.slave_config_id == s.config_id) {
                m->elc_domain_id = da.domain_config_id;
                break;
            }
        }

        // Count entries
        unsigned int total_entries = 0;
        unsigned int sync_count = static_cast<unsigned int>(s.syncs.size());
        for (const auto &sy : s.syncs) {
            for (const auto &p : sy.pdos) {
                total_entries += static_cast<unsigned int>(p.entries.size());
            }
        }

        // Free previous dynamic layout if we own it (seed modules have nulls)
        // Allocate flat entry array + syncs with embedded pdo pointers into heap blocks.
        auto *c_entries = new ec_pdo_entry_info_t[total_entries ? total_entries : 1];
        auto *c_entry_details = new EntryDetails[total_entries ? total_entries : 1];
        auto *c_syncs = new ec_sync_info_t[sync_count + 1];
        memset(c_syncs, 0, sizeof(ec_sync_info_t) * (sync_count + 1));

        // PDOs allocated per-sync as contiguous blocks
        unsigned int entry_pos = 0;
        for (unsigned int si = 0; si < sync_count; ++si) {
            const auto &sy = s.syncs[si];
            c_syncs[si].index = sy.sync_index;
            c_syncs[si].dir = (sy.direction == ELC_DIR_INPUT) ? EC_DIR_INPUT : EC_DIR_OUTPUT;
            c_syncs[si].n_pdos = static_cast<unsigned int>(sy.pdos.size());
            c_syncs[si].watchdog_mode =
                (sy.watchdog == ELC_WD_ENABLE)
                    ? EC_WD_ENABLE
                    : (sy.watchdog == ELC_WD_DISABLE ? EC_WD_DISABLE : EC_WD_DEFAULT);
            if (sy.pdos.empty()) {
                c_syncs[si].pdos = nullptr;
                continue;
            }
            auto *c_pdos = new ec_pdo_info_t[sy.pdos.size()];
            memset(c_pdos, 0, sizeof(ec_pdo_info_t) * sy.pdos.size());
            c_syncs[si].pdos = c_pdos;
            for (size_t pi = 0; pi < sy.pdos.size(); ++pi) {
                const auto &p = sy.pdos[pi];
                c_pdos[pi].index = p.pdo_index;
                c_pdos[pi].n_entries = static_cast<unsigned int>(p.entries.size());
                c_pdos[pi].entries =
                    p.entries.empty() ? nullptr : &c_entries[entry_pos];
                for (const auto &e : p.entries) {
                    c_entries[entry_pos].index = e.index;
                    c_entries[entry_pos].subindex = e.subindex;
                    c_entries[entry_pos].bit_length = e.bit_length;
                    c_entry_details[entry_pos].name =
                        "0x" + std::to_string(e.index) + ":" + std::to_string(e.subindex);
                    c_entry_details[entry_pos].entry_index = entry_pos;
                    c_entry_details[entry_pos].sm_index = si;
                    c_entry_details[entry_pos].pdo_index = static_cast<unsigned int>(pi);

                    struct elc_entry_offset io = {};
                    elc_init_api_header(&io, sizeof(io));
                    io.entry_id = e.entry_id;
                    int r = bus->getEntryOffset(&io);
                    if (r == 0) {
                        m->offsets[entry_pos < 64 ? entry_pos : 63] = io.global_offset;
                        m->bit_positions[entry_pos < 64 ? entry_pos : 63] = io.bit_position;
                    }
                    else {
                        m->offsets[entry_pos < 64 ? entry_pos : 63] = 0;
                        m->bit_positions[entry_pos < 64 ? entry_pos : 63] = 0;
                    }
                    ++entry_pos;
                }
            }
        }
        c_syncs[sync_count].index = 0xff;

        // Replace module layout pointers (leak previous if any — only seed nulls)
        m->pdo_entries = c_entries;
        m->entry_details = c_entry_details;
        m->num_entries = total_entries;
        m->syncs = c_syncs;
        m->sync_count = sync_count;
        m->pdos = nullptr; // owned via syncs[i].pdos
        // Start in PREOP until cycle is active; STARTUP waits for PREOP then
        // sends activate, then requires OP + slave_states==8.
        m->slave_config_state.online = 1;
        m->slave_config_state.operational = 0;
        m->slave_config_state.al_state = 2; // PREOP
    }
    return 0;
}
