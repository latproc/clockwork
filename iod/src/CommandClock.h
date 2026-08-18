#ifndef COMMAND_CLOCK_H
#define COMMAND_CLOCK_H

#include <cstdint>

/*
 * Monotonic, phase-aligned cadence for a COMMANDCLOCK instance.
 * A disabled group records the current slot and waits for the next boundary
 * after it is enabled; it never catches up missed commands in a burst.
 */
class CommandClock {
  public:
    bool due(uint64_t now_us, uint64_t period_ms, bool enabled) {
        if (period_ms == 0) {
            period_ms = 1000;
        }
        const uint64_t period_us = period_ms * 1000ULL;
        const uint64_t slot = now_us / period_us;

        if (period_us != period_us_ || !seen_) {
            period_us_ = period_us;
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
    bool wouldBeDue(uint64_t now_us, uint64_t period_ms) const {
        if (!seen_ || period_us_ == 0) {
            return false;
        }
        if (period_ms == 0) {
            period_ms = 1000;
        }
        const uint64_t period_us = period_ms * 1000ULL;
        if (period_us != period_us_) {
            return false;
        }
        return (now_us / period_us) != slot_;
    }

  private:
    uint64_t period_us_ = 0;
    uint64_t slot_ = 0;
    bool seen_ = false;
};

#endif
