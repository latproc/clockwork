#ifndef COMMAND_CLOCK_H
#define COMMAND_CLOCK_H

#include <cstdint>

/*
 * Monotonic cadence for a COMMANDCLOCK instance.
 * Slot = (now - phase) / period. phase_ms=0 is the old wall-aligned tick;
 * a non-zero phase staggers that instance off the shared 0/period boundary.
 * A disabled group records the current slot and waits for the next boundary
 * after it is enabled; it never catches up missed commands in a burst.
 */
class CommandClock {
  public:
    bool due(uint64_t now_us, uint64_t period_ms, bool enabled, uint64_t phase_ms = 0) {
        uint64_t period_us = 0;
        uint64_t phase_us = 0;
        const uint64_t slot = slotAt(now_us, period_ms, phase_ms, period_us, phase_us);

        if (period_us != period_us_ || phase_us != phase_us_ || !seen_) {
            period_us_ = period_us;
            phase_us_ = phase_us;
            slot_ = slot;
            seen_ = true;
            return false;
        }
        if (!enabled) {
            slot_ = slot;
            return false;
        }
        if (slot == slot_) {
            return false;
        }
        slot_ = slot;
        return true;
    }

    // True if due() would fire now. Does not arm or advance the slot.
    bool wouldBeDue(uint64_t now_us, uint64_t period_ms, uint64_t phase_ms = 0) const {
        if (!seen_ || period_us_ == 0) {
            return false;
        }
        uint64_t period_us = 0;
        uint64_t phase_us = 0;
        const uint64_t slot = slotAt(now_us, period_ms, phase_ms, period_us, phase_us);
        if (period_us != period_us_ || phase_us != phase_us_) {
            return false;
        }
        return slot != slot_;
    }

  private:
    static uint64_t slotAt(uint64_t now_us, uint64_t period_ms, uint64_t phase_ms,
                           uint64_t &period_us, uint64_t &phase_us) {
        if (period_ms == 0) {
            period_ms = 1000;
        }
        if (phase_ms >= period_ms) {
            phase_ms %= period_ms;
        }
        period_us = period_ms * 1000ULL;
        phase_us = phase_ms * 1000ULL;
        const uint64_t shifted = (now_us >= phase_us) ? (now_us - phase_us) : 0;
        return shifted / period_us;
    }

    uint64_t period_us_ = 0;
    uint64_t phase_us_ = 0;
    uint64_t slot_ = 0;
    bool seen_ = false;
};

#endif
