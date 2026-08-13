/*
 * Pure reapply gating for ElcSetupRecipe (unit-testable, no iod types).
 *
 * Power-return must not start setup-hold / CoE when the slave is already OP:
 * that races the master, drops domain WC, and applied 0x1C12 in OP (abort
 * 0x08000022) on 1G2C-122. Apply only in PREOP/SAFEOP.
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
                                 uint64_t hold_stuck_limit_us = kHoldStuckLimitUs) {
    if (!visible) {
        return ReapplyGate::WaitVisible;
    }
    const bool al_setup = (al == kAlPreop || al == kAlSafeop);
    if (al_setup) {
        return ReapplyGate::Apply;
    }
    if (al == kAlOp && !hold_active) {
        return ReapplyGate::SkipAlreadyOp;
    }
    if (hold_active && hold_stuck_limit_us > 0 && hold_wait_us >= hold_stuck_limit_us) {
        return (al == kAlOp) ? ReapplyGate::SkipAlreadyOp : ReapplyGate::ReleaseHoldStuck;
    }
    return ReapplyGate::WaitPreop;
}

} // namespace ElcSetupRecipe

#endif
