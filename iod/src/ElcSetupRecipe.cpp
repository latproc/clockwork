/*
 * Ordered setup-recipe apply for iod-elc (generic control system).
 * Recipe format matches elc_sdo: sequence position type index subindex value
 * Template may use POS (expanded per target position) and/or fixed positions.
 */

#include "ElcSetupRecipe.h"
#include "ECInterface.h"
#include "ElcConfigFile.h"
#include "EtherCATSetup.h"
#include "KernelEthercatBus.h"
#include "MachineInstance.h"
#include "MessageLog.h"
#include "SDOEntry.h"
#include "elc_ethercat.h"
#include "options.h"
#include "value.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <unistd.h>

namespace ElcSetupRecipe {
namespace {

// Pending re-apply after power/link return. Not a fire-and-forget set: drives
// often appear online in INIT (identity 0 / invalid mailbox) before PREOP, and
// a single early SDO batch fails then never retries — long "not ready" window.
struct PendingReapply {
    uint16_t position = 0;
    uint64_t first_queued_us = 0;
    uint64_t next_try_us = 0;
    uint64_t ready_since_us = 0; // 0 = not PREOP/SAFEOP ready yet
    uint64_t last_wait_status_us = 0; // throttle waiting_preop HMI updates
    unsigned attempts = 0;
    int last_ret = 0;
    bool hold_active = false; // elc setup-hold begun for this position
};

/** Begin setup-hold once per pending job when CAP available (API 0.19). */
bool ensureSetupHold(KernelEthercatBus *bus, PendingReapply &p) {
    if (!bus || !bus->hasSetupHold()) {
        return false;
    }
    if (p.hold_active) {
        return true;
    }
    // Brief exclusive only around the hold ioctl (not the whole wait loop).
    ECInterface::setSetupMailboxExclusive(true);
    const int hr = bus->setupHoldBeginPosition(p.position, /*PREOP*/ 2, /*default timeout*/ 0);
    ECInterface::setSetupMailboxExclusive(false);
    if (hr == 0) {
        p.hold_active = true;
        p.ready_since_us = 0; // wait for PREOP after hold
        return true;
    }
    return false;
}

void releaseSetupHold(KernelEthercatBus *bus, PendingReapply &p) {
    if (!bus || !p.hold_active) {
        return;
    }
    ECInterface::setSetupMailboxExclusive(true);
    bus->setupHoldReleasePosition(p.position);
    ECInterface::setSetupMailboxExclusive(false);
    p.hold_active = false;
}

std::mutex g_pending_mu;
std::map<uint16_t, PendingReapply> g_pending;
uint64_t g_last_apply_us = 0;

// Min gap between any re-apply attempts (avoid SDO storm / bus thrash).
constexpr uint64_t kMinApplyGapUs = 500000; // 500 ms (PDO window is short)
// PDO map / mode CoE must run in PREOP or SAFEOP — not OP. Master still
// promotes slaves to OP while cyclic is active (no elc "hold PREOP" ioctl),
// so the PREOP window after power return is short. Hold only long enough
// for identity/mailbox, then apply immediately.
constexpr uint64_t kReadyHoldUs = 400000; // 400 ms in PREOP/SAFEOP
// Backoff after a real apply failure (not "still in OP").
constexpr uint64_t kInitialBackoffUs = 1000000; // 1 s
constexpr uint64_t kMaxBackoffUs = 8000000;     // 8 s
constexpr unsigned kMaxAttempts = 40;
// Give up and leave machine status failed after this age.
constexpr uint64_t kMaxPendingAgeUs = 600000000ULL; // 10 min

// AL states (IgH / elc)
constexpr uint8_t kAlInit = 0x01;
constexpr uint8_t kAlPreop = 0x02;
constexpr uint8_t kAlSafeop = 0x04;
constexpr uint8_t kAlOp = 0x08;

struct RecipeSpec {
    std::string path;
    std::string positions_text; // "29-33" / "29,30"
    uint32_t domain_id = 0;
    uint32_t product_code = 0;
    uint32_t vendor_id = 0;
    bool reapply = true;
    bool enabled = true;
    MachineInstance *machine = nullptr; // optional status updates
};

bool parseU64(const std::string &text, uint64_t maximum, uint64_t *value) {
    if (text.empty() || text[0] == '-') {
        return false;
    }
    char *end = nullptr;
    errno = 0;
    unsigned long long parsed = strtoull(text.c_str(), &end, 0);
    if (errno || !end || *end || parsed > maximum) {
        return false;
    }
    *value = static_cast<uint64_t>(parsed);
    return true;
}

bool encodeValue(const std::string &type, const std::string &text, struct elc_setup_sdo *req) {
    uint64_t u = 0;
    if (type == "u8") {
        if (!parseU64(text, UINT8_MAX, &u)) {
            return false;
        }
        req->type = ELC_SDO_U8;
        req->data_len = 1;
        req->data[0] = static_cast<uint8_t>(u);
        return true;
    }
    if (type == "u16") {
        if (!parseU64(text, UINT16_MAX, &u)) {
            return false;
        }
        req->type = ELC_SDO_U16;
        req->data_len = 2;
        req->data[0] = static_cast<uint8_t>(u);
        req->data[1] = static_cast<uint8_t>(u >> 8);
        return true;
    }
    if (type == "u32") {
        if (!parseU64(text, UINT32_MAX, &u)) {
            return false;
        }
        req->type = ELC_SDO_U32;
        req->data_len = 4;
        req->data[0] = static_cast<uint8_t>(u);
        req->data[1] = static_cast<uint8_t>(u >> 8);
        req->data[2] = static_cast<uint8_t>(u >> 16);
        req->data[3] = static_cast<uint8_t>(u >> 24);
        return true;
    }
    return false;
}

struct TemplateLine {
    uint32_t seq_offset = 0;
    bool is_pos = true;
    uint16_t fixed_position = 0;
    std::string type;
    uint16_t index = 0;
    uint8_t subindex = 0;
    std::string value;
};

bool loadTemplate(const char *path, std::vector<TemplateLine> *out, bool *has_pos,
                  std::set<uint16_t> *fixed_positions) {
    std::ifstream in(path);
    if (!in) {
        return false;
    }
    if (has_pos) {
        *has_pos = false;
    }
    std::string line;
    unsigned lineno = 0;
    while (std::getline(in, line)) {
        ++lineno;
        auto hash = line.find('#');
        if (hash != std::string::npos) {
            line.resize(hash);
        }
        std::istringstream iss(line);
        std::string seq_s, pos_s, type, index_s, sub_s, value;
        if (!(iss >> seq_s >> pos_s >> type >> index_s >> sub_s >> value)) {
            continue;
        }
        TemplateLine tl;
        uint64_t v = 0;
        if (!parseU64(seq_s, UINT32_MAX, &v) || v == 0) {
            std::cerr << "ElcSetupRecipe: bad sequence line " << lineno << " in " << path << "\n";
            return false;
        }
        tl.seq_offset = static_cast<uint32_t>(v);
        if (pos_s == "POS" || pos_s == "pos") {
            tl.is_pos = true;
            if (has_pos) {
                *has_pos = true;
            }
        }
        else {
            if (!parseU64(pos_s, UINT16_MAX, &v)) {
                std::cerr << "ElcSetupRecipe: bad position line " << lineno << " in " << path
                          << "\n";
                return false;
            }
            tl.is_pos = false;
            tl.fixed_position = static_cast<uint16_t>(v);
            if (fixed_positions) {
                fixed_positions->insert(tl.fixed_position);
            }
        }
        tl.type = type;
        if (!parseU64(index_s, UINT16_MAX, &v) || v == 0) {
            return false;
        }
        tl.index = static_cast<uint16_t>(v);
        if (!parseU64(sub_s, UINT8_MAX, &v)) {
            return false;
        }
        tl.subindex = static_cast<uint8_t>(v);
        tl.value = value;
        out->push_back(tl);
    }
    return !out->empty();
}

bool parsePositionsList(const std::string &text, std::vector<uint16_t> *out) {
    if (text.empty() || !out) {
        return false;
    }
    out->clear();
    std::string tok;
    for (size_t i = 0; i <= text.size(); ++i) {
        char c = (i < text.size()) ? text[i] : ',';
        if (c == ',' || c == ' ' || c == ';' || c == '\t') {
            if (tok.empty()) {
                continue;
            }
            // range a-b
            auto dash = tok.find('-');
            if (dash != std::string::npos && dash > 0 && dash + 1 < tok.size()) {
                uint64_t a = 0, b = 0;
                if (!parseU64(tok.substr(0, dash), UINT16_MAX, &a) ||
                    !parseU64(tok.substr(dash + 1), UINT16_MAX, &b) || a > b) {
                    return false;
                }
                for (uint64_t p = a; p <= b; ++p) {
                    out->push_back(static_cast<uint16_t>(p));
                }
            }
            else {
                uint64_t p = 0;
                if (!parseU64(tok, UINT16_MAX, &p)) {
                    return false;
                }
                out->push_back(static_cast<uint16_t>(p));
            }
            tok.clear();
            continue;
        }
        tok.push_back(c);
    }
    // unique sorted
    std::sort(out->begin(), out->end());
    out->erase(std::unique(out->begin(), out->end()), out->end());
    return !out->empty();
}

bool optionTruthy(const Value &v, bool default_when_null = true) {
    if (v.isNull() || v.kind == Value::t_empty) {
        return default_when_null;
    }
    if (v.kind == Value::t_bool) {
        return v.bValue;
    }
    int64_t n = 0;
    if (v.asInteger(n)) {
        return n != 0;
    }
    std::string s = v.asString();
    if (s.empty() || s == "0" || s == "false" || s == "FALSE" || s == "no" || s == "off") {
        return false;
    }
    return true;
}

uint32_t optionU32(MachineInstance *mi, const char *name) {
    if (!mi) {
        return 0;
    }
    const Value &v = mi->getValue(name);
    if (v.isNull() || v.kind == Value::t_empty) {
        return 0;
    }
    int64_t n = 0;
    if (v.asInteger(n) && n >= 0) {
        return static_cast<uint32_t>(n);
    }
    uint64_t u = 0;
    if (parseU64(v.asString(), UINT32_MAX, &u)) {
        return static_cast<uint32_t>(u);
    }
    return 0;
}

std::string optionString(MachineInstance *mi, const char *name) {
    if (!mi) {
        return {};
    }
    const Value &v = mi->getValue(name);
    if (v.isNull() || v.kind == Value::t_empty) {
        return {};
    }
    return v.asString();
}

void setMachineStatus(MachineInstance *mi, const char *status, const char *err) {
    if (!mi) {
        return;
    }
    mi->setValue("status", Value(status));
    if (err) {
        mi->setValue("last_error", Value(err));
    }
    else {
        mi->setValue("last_error", Value(""));
    }
}

void collectFromMachines(std::vector<RecipeSpec> *out) {
    for (auto it = MachineInstance::begin(); it != MachineInstance::end(); ++it) {
        MachineInstance *mi = *it;
        if (!mi || mi->_type != "ECSETUPRECIPE") {
            continue;
        }
        RecipeSpec s;
        s.machine = mi;
        s.path = optionString(mi, "recipe");
        if (s.path.empty()) {
            s.path = optionString(mi, "file"); // alias
        }
        s.positions_text = optionString(mi, "positions");
        s.domain_id = optionU32(mi, "domain_id");
        s.product_code = optionU32(mi, "product_code");
        s.vendor_id = optionU32(mi, "vendor_id");
        s.reapply = optionTruthy(mi->getValue("reapply"), true);
        s.enabled = optionTruthy(mi->getValue("enabled"), true);
        if (!s.enabled) {
            setMachineStatus(mi, "disabled", nullptr);
            continue;
        }
        if (s.path.empty()) {
            setMachineStatus(mi, "failed", "recipe path empty");
            std::cerr << "ElcSetupRecipe: " << mi->getName() << " has empty recipe path\n";
            continue;
        }
        out->push_back(s);
    }
}

void collectFromCli(std::vector<RecipeSpec> *out) {
    unsigned n = elc_setup_recipe_count();
    for (unsigned i = 0; i < n; ++i) {
        const char *path = elc_setup_recipe_path_at(i);
        if (!path || !path[0]) {
            continue;
        }
        RecipeSpec s;
        s.path = path;
        const char *pos = elc_setup_recipe_positions_at(i);
        if (pos) {
            s.positions_text = pos;
        }
        s.domain_id = static_cast<uint32_t>(elc_setup_recipe_domain_at(i));
        s.product_code = static_cast<uint32_t>(elc_setup_recipe_product_at(i));
        s.vendor_id = static_cast<uint32_t>(elc_setup_recipe_vendor_at(i));
        s.reapply = true;
        s.enabled = true;
        out->push_back(s);
    }
}

bool identityMatches(uint32_t vendor_id, uint32_t product_code, const RecipeSpec &s) {
    if (s.vendor_id != 0 && vendor_id != s.vendor_id) {
        return false;
    }
    if (s.product_code != 0 && product_code != s.product_code) {
        return false;
    }
    return true;
}

bool resolvePositions(KernelEthercatBus *bus, const RecipeSpec &spec,
                      const std::vector<TemplateLine> &tmpl, bool has_pos,
                      const std::set<uint16_t> &fixed_in_file,
                      std::vector<uint16_t> *out, std::string *err) {
    out->clear();
    std::vector<uint16_t> candidates;

    if (!spec.positions_text.empty()) {
        if (!parsePositionsList(spec.positions_text, &candidates)) {
            if (err) {
                *err = "bad positions list: " + spec.positions_text;
            }
            return false;
        }
    }
    else if (spec.domain_id != 0) {
        const char *topo = elcDefaultTopologyConfigPath();
        if (!topo) {
            if (err) {
                *err = "ELC_TOPOLOGY_CONFIG is not set";
            }
            return false;
        }
        int r = elcPositionsForDomain(topo, spec.domain_id, &candidates);
        if (r != 0) {
            if (err) {
                *err = "domain_id positions failed from topology";
            }
            return false;
        }
        if (candidates.empty()) {
            if (err) {
                *err = "no slaves assigned to domain_id in topology";
            }
            return false;
        }
    }
    else if (spec.product_code != 0 || spec.vendor_id != 0) {
        // Fill from bus listSlaves then modules fallback.
        if (bus) {
            for (const auto &sl : bus->listSlaves()) {
                if (identityMatches(sl.vendor_id, sl.product_code, spec)) {
                    candidates.push_back(sl.position);
                }
            }
        }
        if (candidates.empty()) {
            for (unsigned p = 0; p < 128; ++p) {
                ECModule *m = ECInterface::findModule(p);
                if (m && identityMatches(m->vendor_id, m->product_code, spec)) {
                    candidates.push_back(static_cast<uint16_t>(p));
                }
            }
        }
        if (candidates.empty()) {
            if (err) {
                *err = "no slaves match vendor_id/product_code";
            }
            return false;
        }
    }
    else if (!fixed_in_file.empty() && !has_pos) {
        candidates.assign(fixed_in_file.begin(), fixed_in_file.end());
    }
    else if (has_pos) {
        if (err) {
            *err = "POS recipe needs positions, domain_id, or product/vendor filter";
        }
        return false;
    }
    else {
        if (err) {
            *err = "no targets resolved";
        }
        return false;
    }

    // Optional identity filter on top of positions/domain.
    if (spec.product_code != 0 || spec.vendor_id != 0) {
        std::vector<uint16_t> filtered;
        for (uint16_t p : candidates) {
            ECModule *m = ECInterface::findModule(p);
            uint32_t vid = m ? m->vendor_id : 0;
            uint32_t pid = m ? m->product_code : 0;
            if (!m && bus) {
                for (const auto &sl : bus->listSlaves()) {
                    if (sl.position == p) {
                        vid = sl.vendor_id;
                        pid = sl.product_code;
                        break;
                    }
                }
            }
            if (identityMatches(vid, pid, spec)) {
                filtered.push_back(p);
            }
        }
        candidates.swap(filtered);
        if (candidates.empty()) {
            if (err) {
                *err = "identity filter removed all positions";
            }
            return false;
        }
    }

    std::sort(candidates.begin(), candidates.end());
    candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
    *out = std::move(candidates);
    (void)tmpl;
    return true;
}

bool positionInResolved(const RecipeSpec &spec, uint16_t position, KernelEthercatBus *bus) {
    if (!spec.enabled || !spec.reapply || spec.path.empty()) {
        return false;
    }
    std::vector<TemplateLine> tmpl;
    bool has_pos = false;
    std::set<uint16_t> fixed;
    if (!loadTemplate(spec.path.c_str(), &tmpl, &has_pos, &fixed)) {
        return false;
    }
    std::vector<uint16_t> pos;
    std::string err;
    if (!resolvePositions(bus, spec, tmpl, has_pos, fixed, &pos, &err)) {
        return false;
    }
    return std::find(pos.begin(), pos.end(), position) != pos.end();
}

std::vector<RecipeSpec> allSpecs() {
    std::vector<RecipeSpec> specs;
    collectFromMachines(&specs);
    collectFromCli(&specs);
    return specs;
}

} // namespace

int applyForPositions(KernelEthercatBus *bus, const char *recipe_path,
                      const std::vector<uint16_t> &positions) {
    if (!bus || !bus->isOpen() || !recipe_path || !recipe_path[0]) {
        return -EINVAL;
    }
    std::vector<TemplateLine> tmpl;
    bool has_pos = false;
    std::set<uint16_t> fixed_in_file;
    if (!loadTemplate(recipe_path, &tmpl, &has_pos, &fixed_in_file)) {
        std::cerr << "ElcSetupRecipe: failed to load template " << recipe_path << "\n";
        return -ENOENT;
    }

    // Build work list: for each target position, emit POS lines at that pos;
    // fixed lines once per matching position (if positions empty, all fixed).
    struct Work {
        uint32_t sequence;
        uint16_t position;
        TemplateLine tl;
    };
    std::vector<Work> work;
    uint32_t seq = 0;

    if (has_pos) {
        if (positions.empty()) {
            std::cerr << "ElcSetupRecipe: POS lines but no positions for " << recipe_path << "\n";
            return -EINVAL;
        }
        uint32_t seq_base = 0;
        for (uint16_t pos : positions) {
            for (const TemplateLine &tl : tmpl) {
                if (!tl.is_pos) {
                    continue;
                }
                Work w;
                w.sequence = seq_base + tl.seq_offset;
                w.position = pos;
                w.tl = tl;
                work.push_back(w);
            }
            seq_base += 100;
        }
        // Fixed lines once each (not per POS slave)
        for (const TemplateLine &tl : tmpl) {
            if (tl.is_pos) {
                continue;
            }
            Work w;
            w.sequence = seq_base + tl.seq_offset;
            w.position = tl.fixed_position;
            w.tl = tl;
            work.push_back(w);
        }
    }
    else {
        // Fixed-only: apply each line; if positions filter given, only those positions.
        for (const TemplateLine &tl : tmpl) {
            if (!positions.empty() &&
                std::find(positions.begin(), positions.end(), tl.fixed_position) ==
                    positions.end()) {
                continue;
            }
            Work w;
            w.sequence = tl.seq_offset;
            w.position = tl.fixed_position;
            w.tl = tl;
            work.push_back(w);
        }
    }

    if (work.empty()) {
        std::cerr << "ElcSetupRecipe: nothing to apply for " << recipe_path << "\n";
        return -EINVAL;
    }

    std::sort(work.begin(), work.end(),
              [](const Work &a, const Work &b) { return a.sequence < b.sequence; });

    int ret = bus->setupBegin();
    if (ret) {
        std::cerr << "ElcSetupRecipe: setup_begin failed " << ret << "\n";
        return ret;
    }

    for (const Work &w : work) {
        struct elc_setup_sdo sdo = {};
        elc_init_api_header(&sdo, sizeof(sdo));
        sdo.sequence = w.sequence ? w.sequence : ++seq;
        sdo.position = w.position;
        sdo.index = w.tl.index;
        sdo.subindex = w.tl.subindex;
        if (!encodeValue(w.tl.type, w.tl.value, &sdo)) {
            std::cerr << "ElcSetupRecipe: encode failed pos=" << w.position << " 0x" << std::hex
                      << w.tl.index << std::dec << "\n";
            bus->setupReset();
            return -EINVAL;
        }
        ret = bus->setupAddSDO(&sdo);
        if (ret) {
            std::cerr << "ElcSetupRecipe: add_sdo failed ret=" << ret << " pos=" << w.position
                      << " seq=" << sdo.sequence << "\n";
            bus->setupReset();
            return ret;
        }
    }

    struct elc_setup_apply apply = {};
    elc_init_api_header(&apply, sizeof(apply));
    ret = bus->setupApply(&apply);
    if (ret) {
        std::cerr << "ElcSetupRecipe: apply failed ret=" << ret
                  << " failed_seq=" << apply.failed_sequence << " pos=" << apply.failed_position
                  << " idx=0x" << std::hex << apply.failed_index << std::dec << ":"
                  << (int)apply.failed_subindex << " abort=0x" << std::hex << apply.abort_code
                  << std::dec << "\n";
        bus->setupReset();
        return ret;
    }

    std::ostringstream msg;
    msg << "ElcSetupRecipe: applied " << recipe_path;
    if (!positions.empty()) {
        msg << " for " << positions.size() << " slave(s):";
        for (uint16_t p : positions) {
            msg << " " << p;
        }
    }
    MessageLog::instance()->add(msg.str());
    std::cerr << msg.str() << "\n";
    return 0;
}

int applyAllConfigured(KernelEthercatBus *bus) {
    if (!bus || !bus->isOpen()) {
        return -EINVAL;
    }
    std::vector<RecipeSpec> specs = allSpecs();
    if (specs.empty()) {
        std::cerr << "ElcSetupRecipe: no ECSETUPRECIPE machines and no --setup-recipe; skip\n";
        return 0;
    }

    int worst = 0;
    for (RecipeSpec &spec : specs) {
        std::vector<TemplateLine> tmpl;
        bool has_pos = false;
        std::set<uint16_t> fixed;
        if (!loadTemplate(spec.path.c_str(), &tmpl, &has_pos, &fixed)) {
            std::cerr << "ElcSetupRecipe: cannot load " << spec.path << "\n";
            setMachineStatus(spec.machine, "failed", "load failed");
            worst = -ENOENT;
            continue;
        }
        std::vector<uint16_t> positions;
        std::string err;
        if (!resolvePositions(bus, spec, tmpl, has_pos, fixed, &positions, &err)) {
            std::cerr << "ElcSetupRecipe: resolve failed for " << spec.path << ": " << err << "\n";
            setMachineStatus(spec.machine, "failed", err.c_str());
            worst = -EINVAL;
            continue;
        }
        int r = applyForPositions(bus, spec.path.c_str(), positions);
        if (r != 0) {
            setMachineStatus(spec.machine, "failed", "apply failed");
            worst = r;
        }
        else {
            setMachineStatus(spec.machine, "applied", nullptr);
        }
    }
    return worst;
}

bool positionWantsReapply(uint16_t position) {
    // bus may be null for identity-only path; resolve still works for positions/domain text
    KernelEthercatBus *bus = nullptr;
    // Prefer open bus from ECInterface if available via processPending path only;
    // here we re-check specs without bus list when possible.
    std::vector<RecipeSpec> specs = allSpecs();
    for (const RecipeSpec &s : specs) {
        if (!s.reapply || !s.enabled) {
            continue;
        }
        // Cheap: positions text / fixed file; domain needs topology
        if (!s.positions_text.empty()) {
            std::vector<uint16_t> pos;
            if (parsePositionsList(s.positions_text, &pos) &&
                std::find(pos.begin(), pos.end(), position) != pos.end()) {
                return true;
            }
            continue;
        }
        if (s.domain_id != 0) {
            std::vector<uint16_t> pos;
            const char *topo = elcDefaultTopologyConfigPath();
            if (topo && elcPositionsForDomain(topo, s.domain_id, &pos) == 0 &&
                std::find(pos.begin(), pos.end(), position) != pos.end()) {
                // still honor product/vendor if set
                if (s.product_code == 0 && s.vendor_id == 0) {
                    return true;
                }
                ECModule *m = ECInterface::findModule(position);
                if (m && identityMatches(m->vendor_id, m->product_code, s)) {
                    return true;
                }
            }
            continue;
        }
        if (s.product_code != 0 || s.vendor_id != 0) {
            ECModule *m = ECInterface::findModule(position);
            if (m && identityMatches(m->vendor_id, m->product_code, s)) {
                return true;
            }
        }
    }
    (void)bus;
    return false;
}

/** Online + non-zero identity (SII). Mailbox may still be INIT. */
bool slaveIdentityReady(KernelEthercatBus *bus, uint16_t position, std::string *why_not) {
    ECModule *m = ECInterface::findModule(position);
    if (!m) {
        if (why_not) {
            *why_not = "no module";
        }
        return false;
    }
    if (!m->slave_config_state.online) {
        if (why_not) {
            *why_not = "offline";
        }
        return false;
    }
    uint32_t vid = m->vendor_id;
    uint32_t pid = m->product_code;
    if (vid == 0 && pid == 0 && bus) {
        struct elc_slave_info info = {};
        if (bus->getSlaveInfo(position, &info) == 0) {
            vid = info.vendor_id;
            pid = info.product_code;
        }
    }
    if (vid == 0 && pid == 0) {
        if (why_not) {
            *why_not = "identity 0:0 (SII not ready)";
        }
        return false;
    }
    return true;
}

/**
 * PDO map / SM assignment CoE (0x1C12/0x1600/…) requires PREOP or SAFEOP.
 * Applying in OP fails or is rejected; do not treat OP as "ready".
 * (Kernel has no hold-in-PREOP while cyclic is active — catch this window.)
 */
bool slaveSetupStateReady(KernelEthercatBus *bus, uint16_t position, std::string *why_not) {
    if (!slaveIdentityReady(bus, position, why_not)) {
        return false;
    }
    ECModule *m = ECInterface::findModule(position);
    const uint8_t al = m->slave_config_state.al_state;
    if (al == kAlPreop || al == kAlSafeop) {
        return true;
    }
    if (why_not) {
        if (al == kAlOp) {
            *why_not = "AL OP — hold for PREOP/SAFEOP (PDO map not applied in OP)";
        }
        else if (al == kAlInit) {
            *why_not = "AL INIT (mailbox not ready)";
        }
        else {
            *why_not = "AL not PREOP/SAFEOP (0x" + std::to_string(al) + ")";
        }
    }
    return false;
}

uint64_t backoffUs(unsigned attempts) {
    // attempts is 1-based after a failure: 1→1s, 2→2s, 3→4s, … cap 8s
    uint64_t b = kInitialBackoffUs;
    for (unsigned i = 1; i < attempts && b < kMaxBackoffUs; ++i) {
        b *= 2;
        if (b > kMaxBackoffUs) {
            b = kMaxBackoffUs;
        }
    }
    return b;
}

void requestReapply(uint16_t position) {
    const uint64_t now = microsecs();
    std::lock_guard<std::mutex> lock(g_pending_mu);
    auto it = g_pending.find(position);
    if (it == g_pending.end()) {
        PendingReapply p;
        p.position = position;
        p.first_queued_us = now;
        p.next_try_us = now; // readiness hold still applies
        p.ready_since_us = 0;
        p.attempts = 0;
        g_pending[position] = p;
        std::cerr << "ElcSetupRecipe: queued reapply pos=" << position << "\n";
        return;
    }
    // Already pending: online flapped — reset readiness hold so we do not
    // apply on a single PREOP blip, but keep attempt history for backoff.
    it->second.ready_since_us = 0;
    if (it->second.next_try_us > now + kReadyHoldUs) {
        // keep longer backoff
    }
    else {
        it->second.next_try_us = now;
    }
}

void processPending(KernelEthercatBus *bus) {
    if (!bus || !bus->isOpen()) {
        return;
    }
    // Do NOT hold exclusive for the whole pass — AL/status must keep updating
    // so we observe PREOP after setup-hold. Exclusive only around mailbox ops.
    const uint64_t now = microsecs();
    if (g_last_apply_us && now - g_last_apply_us < kMinApplyGapUs) {
        return;
    }

    PendingReapply chosen;
    bool have = false;
    {
        std::lock_guard<std::mutex> lock(g_pending_mu);
        if (g_pending.empty()) {
            return;
        }

        // Drop or update readiness for each entry; pick one due attempt.
        for (auto it = g_pending.begin(); it != g_pending.end();) {
            PendingReapply &p = it->second;
            if (now - p.first_queued_us > kMaxPendingAgeUs) {
                std::cerr << "ElcSetupRecipe: reapply give up pos=" << p.position
                          << " after " << (now - p.first_queued_us) / 1000000ULL
                          << "s attempts=" << p.attempts << " last_ret=" << p.last_ret
                          << "\n";
                releaseSetupHold(bus, p);
                it = g_pending.erase(it);
                continue;
            }

            std::string why;
            // Prefer API 0.19 setup-hold so OP is inhibited while we wait/apply.
            ensureSetupHold(bus, p);
            // Apply when AL is PREOP/SAFEOP, or when setup-hold is active (module
            // may still report operational/OP briefly after hold begin).
            const bool al_ok = slaveSetupStateReady(bus, p.position, &why);
            if (!al_ok && !p.hold_active) {
                p.ready_since_us = 0;
                if (now - p.last_wait_status_us >= 2000000ULL) {
                    p.last_wait_status_us = now;
                    for (RecipeSpec &spec : allSpecs()) {
                        if (!spec.reapply || !spec.enabled || !spec.machine) {
                            continue;
                        }
                        if (positionInResolved(spec, p.position, bus)) {
                            setMachineStatus(spec.machine, "waiting_preop", why.c_str());
                        }
                    }
                }
                ++it;
                continue;
            }
            if (p.ready_since_us == 0) {
                p.ready_since_us = now;
                std::cerr << "ElcSetupRecipe: pos=" << p.position
                          << (al_ok ? " PREOP/SAFEOP ready" : " setup-hold active")
                          << "; holding " << (kReadyHoldUs / 1000) << "ms then apply\n";
            }
            if (now - p.ready_since_us < kReadyHoldUs) {
                if (!al_ok && p.hold_active &&
                    now - p.last_wait_status_us >= 2000000ULL) {
                    p.last_wait_status_us = now;
                    for (RecipeSpec &spec : allSpecs()) {
                        if (!spec.reapply || !spec.enabled || !spec.machine) {
                            continue;
                        }
                        if (positionInResolved(spec, p.position, bus)) {
                            setMachineStatus(spec.machine, "holding_preop",
                                             "setup-hold active; applying shortly");
                        }
                    }
                }
                ++it;
                continue;
            }
            if (now < p.next_try_us) {
                ++it;
                continue;
            }
            if (p.attempts >= kMaxAttempts) {
                std::cerr << "ElcSetupRecipe: reapply max attempts pos=" << p.position
                          << " attempts=" << p.attempts << " last_ret=" << p.last_ret << "\n";
                releaseSetupHold(bus, p);
                it = g_pending.erase(it);
                continue;
            }
            // Prefer oldest first_queued among due entries.
            if (!have || p.first_queued_us < chosen.first_queued_us) {
                chosen = p;
                have = true;
            }
            ++it;
        }
        if (!have) {
            return;
        }
        // Leave entry in map until success or final give-up; mark next_try far
        // so we do not double-pick before this call finishes.
        g_pending[chosen.position].next_try_us = now + kMinApplyGapUs;
        // Sync hold flag from map (ensureSetupHold may have set it under lock).
        chosen.hold_active = g_pending[chosen.position].hold_active;
    }

    g_last_apply_us = now;
    const uint16_t pos = chosen.position;

    // Hold across apply so master does not race us back to OP mid-batch.
    ensureSetupHold(bus, chosen);
    {
        std::lock_guard<std::mutex> lock(g_pending_mu);
        auto it = g_pending.find(pos);
        if (it != g_pending.end()) {
            it->second.hold_active = chosen.hold_active;
        }
    }

    std::vector<RecipeSpec> specs = allSpecs();
    bool any_recipe = false;
    bool all_ok = true;
    int last_r = 0;
    MachineInstance *status_mi = nullptr;

    for (RecipeSpec &spec : specs) {
        if (!spec.reapply || !spec.enabled) {
            continue;
        }
        if (!positionInResolved(spec, pos, bus)) {
            continue;
        }
        any_recipe = true;
        if (spec.machine) {
            status_mi = spec.machine;
            setMachineStatus(spec.machine, "pending",
                             chosen.hold_active ? "reapply under setup-hold"
                                                : "reapply in progress");
        }
        std::vector<uint16_t> one = {pos};
        ECInterface::setSetupMailboxExclusive(true);
        int r = applyForPositions(bus, spec.path.c_str(), one);
        ECInterface::setSetupMailboxExclusive(false);
        last_r = r;
        if (r != 0) {
            all_ok = false;
            setMachineStatus(spec.machine, "retrying", "reapply failed; will retry");
        }
        else {
            setMachineStatus(spec.machine, "applied", nullptr);
        }
    }

    if (!any_recipe) {
        // No recipe owns this position — L_SDO recommission only.
        {
            std::lock_guard<std::mutex> lock(g_pending_mu);
            auto it = g_pending.find(pos);
            if (it != g_pending.end()) {
                releaseSetupHold(bus, it->second);
                g_pending.erase(it);
            }
        }
#ifdef USE_SDO
        ECModule *m = ECInterface::findModule(pos);
        if (m) {
            SDOEntry::recommissionModule(m, now);
        }
#endif
        return;
    }

    if (all_ok) {
        std::cerr << "ElcSetupRecipe: reapply ok pos=" << pos << " attempts="
                  << (chosen.attempts + 1)
                  << (chosen.hold_active ? " (setup-hold)" : "") << "\n";
        {
            std::lock_guard<std::mutex> lock(g_pending_mu);
            auto it = g_pending.find(pos);
            if (it != g_pending.end()) {
                releaseSetupHold(bus, it->second);
                g_pending.erase(it);
            }
            else {
                releaseSetupHold(bus, chosen);
            }
        }
#ifdef USE_SDO
        // Defaults after successful PDO/mode recipe only (avoid fighting INIT).
        ECModule *m = ECInterface::findModule(pos);
        if (m) {
            SDOEntry::recommissionModule(m, now);
        }
#endif
        // After *all* pending reapply positions finish, re-seed process-image
        // defaults once (accel/decel/torque etc. must not stay 0 on RxPDO-mapped
        // 0x6083/0x6084 — A.76). Do not call per-position: that thrashes control
        // words while ClearFault is in flight across multiple servos.
        bool pending_empty = false;
        {
            std::lock_guard<std::mutex> lock(g_pending_mu);
            pending_empty = g_pending.empty();
        }
        if (pending_empty) {
            std::cerr << "ElcSetupRecipe: all reapply done — reapplyOutputDefaults once\n";
            reapplyOutputDefaults();
        }
        return;
    }

    // Failed: schedule retry with backoff; keep setup-hold for next PREOP apply.
    const unsigned next_attempt = chosen.attempts + 1;
    const uint64_t delay = backoffUs(next_attempt);
    {
        std::lock_guard<std::mutex> lock(g_pending_mu);
        auto it = g_pending.find(pos);
        if (it != g_pending.end()) {
            it->second.attempts = next_attempt;
            it->second.last_ret = last_r;
            it->second.next_try_us = now + delay;
            it->second.hold_active = chosen.hold_active;
            // If left PREOP/SAFEOP, require a fresh ready hold when it returns.
            std::string why;
            if (!slaveSetupStateReady(bus, pos, &why)) {
                it->second.ready_since_us = 0;
            }
            if (next_attempt >= kMaxAttempts) {
                releaseSetupHold(bus, it->second);
                g_pending.erase(it);
            }
        }
    }
    std::cerr << "ElcSetupRecipe: reapply failed pos=" << pos << " ret=" << last_r
              << " attempt=" << next_attempt << " retry_in_ms=" << (delay / 1000) << "\n";
    if (status_mi && next_attempt >= kMaxAttempts) {
        setMachineStatus(status_mi, "failed",
                         "reapply exhausted retries (need PREOP window / mains cycle)");
    }
}

// ---- Background worker: never block sendUpdates / ecat with mailbox SDO ----
std::mutex g_worker_mu;
std::condition_variable g_worker_cv;
std::atomic<KernelEthercatBus *> g_worker_bus{nullptr};
std::atomic<bool> g_worker_kick{false};
std::atomic<bool> g_worker_stop{false};
std::once_flag g_worker_once;
std::thread g_worker_thread;

void reapplyWorkerMain() {
    pthread_setname_np(pthread_self(), "iod setup reapply");
    std::cerr << "ElcSetupRecipe: reapply worker started (off hot path)\n";
    while (!g_worker_stop.load()) {
        {
            std::unique_lock<std::mutex> lk(g_worker_mu);
            g_worker_cv.wait_for(lk, std::chrono::milliseconds(200), [] {
                return g_worker_kick.load() || g_worker_stop.load();
            });
            g_worker_kick.store(false);
        }
        if (g_worker_stop.load()) {
            break;
        }
        KernelEthercatBus *bus = g_worker_bus.load();
        if (!bus || !bus->isOpen()) {
            continue;
        }
        // One processPending pass (at most one position apply + holds).
        // Pause ecat userspace path while we hold the master for mailbox SDO.
        try {
            // Signal ECInterface via processPending-side lock: use same busy
            // flag by calling through a thin pause if needed. Hold is short.
            processPending(bus);
        }
        catch (const std::exception &ex) {
            std::cerr << "ElcSetupRecipe: worker exception: " << ex.what() << "\n";
        }
        catch (...) {
            std::cerr << "ElcSetupRecipe: worker unknown exception\n";
        }
    }
    std::cerr << "ElcSetupRecipe: reapply worker stopped\n";
}

void ensureReapplyWorker() {
    std::call_once(g_worker_once, [] {
        g_worker_stop.store(false);
        g_worker_thread = std::thread(reapplyWorkerMain);
        g_worker_thread.detach();
    });
}

void scheduleProcessPending(KernelEthercatBus *bus) {
    if (!bus) {
        return;
    }
    // Only after cycle activate — pre-activate reapply+hold left domains
    // status_known=0 and ETHERCAT DISCONNECTED while holds blocked recovery.
    if (!ECInterface::active) {
        return;
    }
    g_worker_bus.store(bus);
    // Only wake when there is work (cheap check under pending lock).
    {
        std::lock_guard<std::mutex> lock(g_pending_mu);
        if (g_pending.empty()) {
            return;
        }
    }
    ensureReapplyWorker();
    g_worker_kick.store(true);
    g_worker_cv.notify_one();
}

} // namespace ElcSetupRecipe
