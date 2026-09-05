/*
 * Pure reapply gating for ElcSetupRecipe (unit-testable, no iod types).
 *
 * Do not setup-hold / remap a slave that has been *continuously* OP
 * (iod restart onto live drives, spurious reapply). That raced the master
 * and applied 0x1C12 in OP (abort 0x08000022) on 1G2C-122.
 *
 * After a real power-down (not visible / INIT) volatile CoE is factory
 * (ED3L mode 1, 0x6083=0). Then needs_commission is set: wait PREOP and
 * hold-from-OP is allowed so the recipe can run. Apply only in PREOP/SAFEOP.
 */
#ifndef ELC_SETUP_RECIPE_POLICY_H
#define ELC_SETUP_RECIPE_POLICY_H

#include <cstdint>

namespace ElcSetupRecipe {

constexpr uint8_t kAlInit = 0x01;
constexpr uint8_t kAlPreop = 0x02;
constexpr uint8_t kAlSafeop = 0x04;
constexpr uint8_t kAlOp = 0x08;

// Hold ioctl may take a moment; if AL never becomes PREOP/SAFEOP, stop fighting.
constexpr uint64_t kHoldStuckLimitUs = 2000000ULL; // 2 s
// Warn if a mailbox apply batch exceeds this (kernel SDO timeout is ~1 s).
constexpr uint64_t kApplyExclusiveWarnUs = 300000ULL; // 300 ms

enum class ReapplyGate {
    WaitVisible,      // not on the bus (mains off)
    SkipAlreadyOp,    // AL OP and not mid-hold — do not remap
    WaitPreop,        // visible, wait for PREOP/SAFEOP (optionally after hold)
    Apply,            // PREOP or SAFEOP
    ReleaseHoldStuck  // hold never produced PREOP; release and retry/skip
};

inline ReapplyGate decideReapply(bool visible, uint8_t al, bool hold_active,
                                 uint64_t hold_wait_us,
                                 uint64_t hold_stuck_limit_us = kHoldStuckLimitUs,
                                 bool needs_commission = false) {
    if (!visible) {
        return ReapplyGate::WaitVisible;
    }
    const bool al_setup = (al == kAlPreop || al == kAlSafeop);
    if (al_setup) {
        return ReapplyGate::Apply;
    }
    // Continuously OP: leave CoE alone. After power-down, do not skip.
    if (al == kAlOp && !hold_active && !needs_commission) {
        return ReapplyGate::SkipAlreadyOp;
    }
    if (hold_active && hold_stuck_limit_us > 0 && hold_wait_us >= hold_stuck_limit_us) {
        // Hold failed to reach PREOP. After power-down keep trying (release);
        // if CoE is still believed valid, skip.
        if (al == kAlOp && !needs_commission) {
            return ReapplyGate::SkipAlreadyOp;
        }
        return ReapplyGate::ReleaseHoldStuck;
    }
    return ReapplyGate::WaitPreop;
}

/** Hold ioctl is for PREOP/SAFEOP/OP (hold-from-OP). Not INIT or SII 0:0. */
inline bool canBeginSetupHold(uint8_t al, uint32_t vendor_id, uint32_t product_code) {
    if (al == 0 || al == kAlInit) {
        return false;
    }
    if (vendor_id == 0 && product_code == 0) {
        return false;
    }
    return true;
}

} // namespace ElcSetupRecipe

#endif
