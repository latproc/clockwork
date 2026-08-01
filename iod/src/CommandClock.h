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

  private:
    uint64_t period_us_ = 0;
    uint64_t slot_ = 0;
    bool seen_ = false;
};

#endif
