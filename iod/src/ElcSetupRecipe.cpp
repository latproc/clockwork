/*
 * ED3L velocity PDO setup recipe apply for iod-elc.
 * Recipe format matches elc_sdo: sequence position type index subindex value
 * Template uses POS for position; expanded per slave.
 */

#include "ElcSetupRecipe.h"
#include "ECInterface.h"
#include "KernelEthercatBus.h"
#include "MessageLog.h"
#include "SDOEntry.h"
#include "elc_ethercat.h"
#include "value.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <mutex>
#include <set>
#include <sstream>
#include <string>

namespace ElcSetupRecipe {
namespace {

// ESTUN/Summa ED3L (ESI ProductCode #xED310001)
constexpr uint32_t kEd3lProductCode = 0xED310001u;

std::mutex g_pending_mu;
std::set<uint16_t> g_pending_positions;
uint64_t g_last_apply_us = 0;
// Min gap between re-apply batches while cycling (avoid storm).
constexpr uint64_t kMinApplyGapUs = 500000; // 500 ms

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
    // Plant recipe only uses u8/u16/u32.
    return false;
}

struct TemplateLine {
    uint32_t seq_offset = 0; // original sequence from file
    std::string type;
    uint16_t index = 0;
    uint8_t subindex = 0;
    std::string value;
};

bool loadTemplate(const char *path, std::vector<TemplateLine> *out) {
    std::ifstream in(path);
    if (!in) {
        return false;
    }
    std::string line;
    unsigned lineno = 0;
    while (std::getline(in, line)) {
        ++lineno;
        // strip comments
        auto hash = line.find('#');
        if (hash != std::string::npos) {
            line.resize(hash);
        }
        std::istringstream iss(line);
        std::string seq_s, pos_s, type, index_s, sub_s, value;
        if (!(iss >> seq_s >> pos_s >> type >> index_s >> sub_s >> value)) {
            continue; // blank
        }
        if (pos_s != "POS" && pos_s != "pos") {
            // Allow fixed position lines; plant template uses POS only.
            continue;
        }
        TemplateLine tl;
        uint64_t v = 0;
        if (!parseU64(seq_s, UINT32_MAX, &v) || v == 0) {
            std::cerr << "ElcSetupRecipe: bad sequence line " << lineno << "\n";
            return false;
        }
        tl.seq_offset = static_cast<uint32_t>(v);
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

} // namespace

const char *defaultEd3lRecipePath() {
    static const char *paths[] = {
        "/opt/latproc/code/config/recipes/ed3l_velocity_pdo.recipe.in",
        "/opt/latproc/iod/recipes/ed3l_velocity_pdo.recipe.in",
        nullptr,
    };
    for (int i = 0; paths[i]; ++i) {
        if (FILE *f = fopen(paths[i], "r")) {
            fclose(f);
            return paths[i];
        }
    }
    return paths[0];
}

bool isEd3lProduct(uint32_t product_code) {
    return product_code == kEd3lProductCode;
}

bool isEd3lModule(const ECModule *m) {
    if (!m) {
        return false;
    }
    if (isEd3lProduct(m->product_code)) {
        return true;
    }
    const std::string &n = m->getName();
    return n.find("ED3L") != std::string::npos || n.find("Summa") != std::string::npos;
}

int applyForPositions(KernelEthercatBus *bus, const char *recipe_path,
                      const std::vector<uint16_t> &positions) {
    if (!bus || !bus->isOpen() || positions.empty()) {
        return -EINVAL;
    }
    const char *path = recipe_path && recipe_path[0] ? recipe_path : defaultEd3lRecipePath();
    std::vector<TemplateLine> tmpl;
    if (!loadTemplate(path, &tmpl)) {
        std::cerr << "ElcSetupRecipe: failed to load template " << path << "\n";
        return -ENOENT;
    }

    int ret = bus->setupBegin();
    if (ret) {
        std::cerr << "ElcSetupRecipe: setup_begin failed " << ret << "\n";
        return ret;
    }

    uint32_t seq = 0;
    uint32_t seq_base = 0;
    for (uint16_t pos : positions) {
        for (const TemplateLine &tl : tmpl) {
            struct elc_setup_sdo sdo = {};
            elc_init_api_header(&sdo, sizeof(sdo));
            // Monotonic sequences across slaves (elc requires increasing sequence).
            seq = seq_base + tl.seq_offset;
            sdo.sequence = seq;
            sdo.position = pos;
            sdo.index = tl.index;
            sdo.subindex = tl.subindex;
            if (!encodeValue(tl.type, tl.value, &sdo)) {
                std::cerr << "ElcSetupRecipe: encode failed pos=" << pos << " 0x" << std::hex
                          << tl.index << std::dec << "\n";
                bus->setupReset();
                return -EINVAL;
            }
            ret = bus->setupAddSDO(&sdo);
            if (ret) {
                std::cerr << "ElcSetupRecipe: add_sdo failed ret=" << ret << " pos=" << pos
                          << " seq=" << seq << "\n";
                bus->setupReset();
                return ret;
            }
        }
        seq_base += 100; // match iod-elc.sh spacing
    }

    struct elc_setup_apply apply = {};
    elc_init_api_header(&apply, sizeof(apply));
    ret = bus->setupApply(&apply);
    if (ret) {
        std::cerr << "ElcSetupRecipe: apply failed ret=" << ret
                  << " failed_seq=" << apply.failed_sequence
                  << " pos=" << apply.failed_position << " idx=0x" << std::hex
                  << apply.failed_index << std::dec << ":" << (int)apply.failed_subindex
                  << " abort=0x" << std::hex << apply.abort_code << std::dec << "\n";
        bus->setupReset();
        return ret;
    }

    std::ostringstream msg;
    msg << "ElcSetupRecipe: applied " << path << " for " << positions.size()
        << " slave(s):";
    for (uint16_t p : positions) {
        msg << " " << p;
    }
    MessageLog::instance()->add(msg.str());
    std::cerr << msg.str() << "\n";
    return 0;
}

void requestReapply(uint16_t position) {
    std::lock_guard<std::mutex> lock(g_pending_mu);
    g_pending_positions.insert(position);
}

void processPending(KernelEthercatBus *bus) {
    if (!bus || !bus->isOpen()) {
        return;
    }
    const uint64_t now = microsecs();
    if (g_last_apply_us && now - g_last_apply_us < kMinApplyGapUs) {
        return;
    }
    std::vector<uint16_t> batch;
    {
        std::lock_guard<std::mutex> lock(g_pending_mu);
        if (g_pending_positions.empty()) {
            return;
        }
        batch.assign(g_pending_positions.begin(), g_pending_positions.end());
        g_pending_positions.clear();
    }
    g_last_apply_us = now;
    (void)applyForPositions(bus, defaultEd3lRecipePath(), batch);
    // Plant L_SDO defaults: mark recommission so ramps/mode follow map.
#ifdef USE_SDO
    for (uint16_t pos : batch) {
        ECModule *m = ECInterface::findModule(pos);
        if (m) {
            SDOEntry::recommissionModule(m, now);
        }
    }
#endif
}

int applyForAllEd3lOnBus(KernelEthercatBus *bus, const char *recipe_path) {
    if (!bus || !bus->isOpen()) {
        return -EINVAL;
    }
    std::vector<uint16_t> positions;
    // Prefer configured modules with product/name match.
    // ECInterface::modules is private; use listSlaves discovery.
    auto slaves = bus->listSlaves();
    for (const auto &s : slaves) {
        if (isEd3lProduct(s.product_code) ||
            (s.name[0] && (strstr(s.name, "ED3L") || strstr(s.name, "Summa")))) {
            positions.push_back(s.position);
        }
    }
    if (positions.empty()) {
        // Fallback: plant MODULE names via findModule scan by position 0..63
        for (unsigned p = 0; p < 64; ++p) {
            ECModule *m = ECInterface::findModule(p);
            if (m && isEd3lModule(m)) {
                positions.push_back(static_cast<uint16_t>(p));
            }
        }
    }
    if (positions.empty()) {
        std::cerr << "ElcSetupRecipe: no ED3L slaves found; skip recipe\n";
        return 0;
    }
    return applyForPositions(bus, recipe_path ? recipe_path : defaultEd3lRecipePath(),
                             positions);
}

} // namespace ElcSetupRecipe
