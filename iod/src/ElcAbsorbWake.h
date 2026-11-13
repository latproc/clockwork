/*
 * Wait-loop policy for iod-elc analog absorb. Header only, no bus.
 *
 * Analog-only frames stay in the processing-thread wait so COUNTER and
 * ANALOGINPUT do not walk runnable[] at the bus rate. Two things still have
 * to punch through that wait:
 *   - COMMANDCLOCK fires on its notify_period, including while absorb continues
 *   - a POINT / STATUS_FLAG / FLAG edge marks listeners change-driven so their
 *     WHEN runs now. A TIMER-only requeue stays on stable_check_us.
 *
 * cw already capped and dispatched in this wait. iod-elc compiled that out
 * (USE_ETHERCAT), so a 25 ms clock was measured at 395 ms on 1G2C-122.
 */
#ifndef ELC_ABSORB_WAKE_H
#define ELC_ABSORB_WAKE_H

#include <cstdint>
#include <cstring>

namespace ElcAbsorbWake {

// Shorten an idle/paced poll so it returns at the next command-clock boundary.
// next_wake_us == 0 means the clock wants no wake (disabled, or none registered).
// The result is at least 1 ms: zmq_poll(0) busy-spins.
inline int capWaitMs(int wait_ms, uint64_t now_us, uint64_t next_wake_us) {
    if (wait_ms < 1) {
        wait_ms = 1;
    }
    if (next_wake_us != 0) {
        const int clock_wait_ms =
            (next_wake_us > now_us) ? static_cast<int>((next_wake_us - now_us) / 1000) : 0;
        if (clock_wait_ms < wait_ms) {
            wait_ms = clock_wait_ms;
        }
    }
    if (wait_ms < 1) {
        wait_ms = 1;
    }
    return wait_ms;
}

// POINT, STATUS_FLAG and FLAG are on/off. ANALOGINPUT and COUNTER are not:
// marking those immediate is the every-sample fan-out absorb exists to avoid.
inline bool isDigitalEdgeType(const char *type) {
    if (!type) {
        return false;
    }
    return std::strcmp(type, "POINT") == 0 || std::strcmp(type, "STATUS_FLAG") == 0 ||
           std::strcmp(type, "FLAG") == 0;
}

// A late due() while K=1 has not reached every consumer must keep
// command_fanout_skip. Restarting at the first dependant drops the rest.
inline bool dueKeepsFanoutSkip(bool fanout_pending) { return fanout_pending; }

// When a point/flag edge is marked, the listener's WHEN runs at the edge.
// A TIMER-only requeue runs at the paced stable recheck, not at the edge.
inline uint64_t whenRunsUs(bool point_or_flag_edge, uint64_t edge_us,
                           uint64_t stable_check_us) {
    if (point_or_flag_edge) {
        return edge_us;
    }
    return edge_us + stable_check_us;
}

} // namespace ElcAbsorbWake

#endif
