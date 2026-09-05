/*
 * Coalesce kernel domain/AL snapshots for Clockwork mirrors.
 *
 * The ecat thread stores POD snapshots only (no MachineInstance). Processing
 * applies them change-only, rate-limited while any domain is incomplete so a
 * domain-2 WC storm cannot setValue from the RT path (1G2C-122 2026-09-04).
 */
#ifndef ELC_DOMAIN_CW_MIRROR_H
#define ELC_DOMAIN_CW_MIRROR_H

#include <cstdint>
#include <cstring>

namespace ElcDomainCwMirror {

constexpr size_t kMaxDomains = 8;
/** Max apply rate while any domain is incomplete (not on COMPLETE↔INCOMPLETE edges). */
constexpr uint64_t kIncompleteApplyMinUs = 50000ULL; // 20 Hz

struct Slot {
    uint32_t id = 0;
    bool status_known = false;
    bool active = false;
    bool valid = false;
    bool armed = false;
    bool rearm_required = false;
    uint32_t wc = 0;
    uint8_t wc_state = 0;
    uint32_t faults = 0;
    uint32_t slave_states = 0;
    uint32_t base_offset = 0;
    uint32_t domain_size = 0;
};

struct Snapshot {
    Slot slots[kMaxDomains]{};
    uint8_t nslots = 0;
    uint32_t primary_domain_id = 0;
    bool domain_status_ok = false;
    bool domain_status_pending = false;
    uint32_t ethercat_slave_states = 0;
    uint32_t ethercat_all_slave_states = 0;
    bool ethercat_al_valid = false;
};

inline bool slotsEqual(const Slot &a, const Slot &b) {
    return a.id == b.id && a.status_known == b.status_known && a.active == b.active &&
           a.valid == b.valid && a.armed == b.armed && a.rearm_required == b.rearm_required &&
           a.wc == b.wc && a.wc_state == b.wc_state && a.faults == b.faults &&
           a.slave_states == b.slave_states && a.base_offset == b.base_offset &&
           a.domain_size == b.domain_size;
}

inline bool snapshotsEqual(const Snapshot &a, const Snapshot &b) {
    if (a.nslots != b.nslots || a.primary_domain_id != b.primary_domain_id ||
        a.domain_status_ok != b.domain_status_ok ||
        a.domain_status_pending != b.domain_status_pending ||
        a.ethercat_slave_states != b.ethercat_slave_states ||
        a.ethercat_all_slave_states != b.ethercat_all_slave_states ||
        a.ethercat_al_valid != b.ethercat_al_valid) {
        return false;
    }
    for (uint8_t i = 0; i < a.nslots && i < kMaxDomains; ++i) {
        if (!slotsEqual(a.slots[i], b.slots[i])) {
            return false;
        }
    }
    return true;
}

/** nullptr = lifecycle hold (status_known false). Never INVALID here. */
inline const char *cwState(const Slot &slot) {
    if (!slot.status_known) {
        return nullptr;
    }
    if (!slot.active) {
        return "INCOMPLETE";
    }
    if (slot.wc_state == 2 && slot.valid) {
        return "COMPLETE";
    }
    return "INCOMPLETE";
}

inline bool slotIncomplete(const Slot &slot) {
    const char *st = cwState(slot);
    return st != nullptr && std::strcmp(st, "COMPLETE") != 0;
}

inline bool anyIncomplete(const Snapshot &s) {
    for (uint8_t i = 0; i < s.nslots && i < kMaxDomains; ++i) {
        if (slotIncomplete(s.slots[i])) {
            return true;
        }
    }
    return false;
}

/** First declared domain is primary (index 0), matching iod-elc topology. */
inline bool anyNonPrimaryIncomplete(const Snapshot &s) {
    for (uint8_t i = 1; i < s.nslots && i < kMaxDomains; ++i) {
        if (slotIncomplete(s.slots[i])) {
            return true;
        }
    }
    return false;
}

inline bool isCwStateEdge(const Snapshot &prev, const Snapshot &next) {
    if (prev.nslots != next.nslots) {
        return true;
    }
    for (uint8_t i = 0; i < next.nslots && i < kMaxDomains; ++i) {
        const char *a = i < prev.nslots ? cwState(prev.slots[i]) : nullptr;
        const char *b = cwState(next.slots[i]);
        if (a == b) {
            continue;
        }
        if (a == nullptr || b == nullptr || std::strcmp(a, b) != 0) {
            return true;
        }
    }
    return false;
}

struct Coalesce {
    Snapshot pending{};
    Snapshot last_applied{};
    bool dirty = false;
    bool have_applied = false;
    uint64_t last_apply_us = 0;

    void store(const Snapshot &s) {
        pending = s;
        dirty = true;
    }

    bool takeIfDue(uint64_t now_us, Snapshot *out,
                   uint64_t min_incomplete_us = kIncompleteApplyMinUs) {
        if (!out || !dirty) {
            return false;
        }
        if (have_applied && snapshotsEqual(pending, last_applied)) {
            dirty = false;
            return false;
        }
        const bool incomplete = anyIncomplete(pending);
        const bool edge = !have_applied || isCwStateEdge(last_applied, pending);
        if (incomplete && !edge && last_apply_us != 0 &&
            now_us >= last_apply_us && (now_us - last_apply_us) < min_incomplete_us) {
            return false;
        }
        *out = pending;
        last_applied = pending;
        have_applied = true;
        last_apply_us = now_us;
        dirty = false;
        return true;
    }
};

} // namespace ElcDomainCwMirror

#endif
