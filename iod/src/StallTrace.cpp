/*
    Opt-in processing stall diagnostic (STALLSNAP). See StallTrace.h.
*/

#include "StallTrace.h"
#include "DebugExtra.h"
#include "Logger.h"
#include "value.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <pthread.h>
#include <string>
#include <thread>

namespace StallTrace {
namespace {

constexpr size_t kRingSize = 32;
constexpr size_t kNameCap = 48;
constexpr uint64_t kStallThresholdUs = 100000; // 100 ms
constexpr uint64_t kMinEmitIntervalUs = 1000000; // rate-limit 1 s
constexpr int kObserverSleepMs = 20;

struct RingEntry {
    uint64_t at_us;
    uint8_t stage;
};

struct Shared {
    std::atomic<bool> enabled{false};
    std::atomic<bool> observer_started{false};
    std::atomic<bool> observer_stop{false};

    std::atomic<uint64_t> heartbeat_us{0};
    std::atomic<uint64_t> heartbeat_seq{0};
    std::atomic<uint8_t> stage{StageIdle};
    std::atomic<uint64_t> stage_enter_us{0};

    std::atomic<uint32_t> q_runnable{0};
    std::atomic<uint32_t> q_stable{0};
    std::atomic<uint32_t> q_exec{0};
    std::atomic<uint32_t> q_mail{0};
    std::atomic<uint32_t> q_events{0};
    std::atomic<uint32_t> q_pend_ev{0};

    // machine name: length + fixed buffer; writer is processing only
    std::atomic<uint8_t> name_len{0};
    char name_buf[kNameCap];

    std::atomic<uint32_t> ring_pos{0};
    RingEntry ring[kRingSize];

    std::atomic<uint32_t> suppressed{0};
    std::atomic<uint64_t> last_emit_us{0};
};

Shared g;

std::thread g_observer;

const char *stageNameLocal(uint8_t s) {
    static const char *names[] = {
        "idle",          // 0
        "outer",         // 1
        "zmq_poll",      // 2
        "ecat",          // 3
        "plugins",       // 4
        "channels_cmd",  // 5
        "scheduler",     // 6
        "poll_machines", // 7
        "stable",        // 8
        "outputs",       // 9
    };
    if (s < StageCount) {
        return names[s];
    }
    return "?";
}

void writeHeartbeat(uint64_t now) {
    g.heartbeat_us.store(now, std::memory_order_relaxed);
    g.heartbeat_seq.fetch_add(1, std::memory_order_relaxed);
}

void pushRing(uint8_t stage, uint64_t now) {
    const uint32_t i = g.ring_pos.fetch_add(1, std::memory_order_relaxed) % kRingSize;
    g.ring[i].at_us = now;
    g.ring[i].stage = stage;
}

void copyName(char *out, size_t out_sz) {
    const uint8_t n = g.name_len.load(std::memory_order_relaxed);
    size_t len = n;
    if (len >= out_sz) {
        len = out_sz ? out_sz - 1 : 0;
    }
    if (len) {
        std::memcpy(out, g.name_buf, len);
    }
    if (out_sz) {
        out[len] = '\0';
    }
}

void emitStallSnap(uint64_t stall_start_us, uint64_t recover_us, uint64_t last_hb_us,
                   uint8_t last_stage, uint64_t stage_enter_us) {
    const uint64_t now = microsecs();
    const uint64_t last_emit = g.last_emit_us.load(std::memory_order_relaxed);
    if (last_emit && now - last_emit < kMinEmitIntervalUs) {
        g.suppressed.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    g.last_emit_us.store(now, std::memory_order_relaxed);

    char name[kNameCap];
    copyName(name, sizeof(name));

    const uint64_t dur = recover_us > stall_start_us ? recover_us - stall_start_us : 0;
    const uint32_t suppressed = g.suppressed.exchange(0, std::memory_order_relaxed);

    // Snapshot ring (best-effort; may race with writer)
    const uint32_t pos = g.ring_pos.load(std::memory_order_relaxed);
    std::string ring_s;
    ring_s.reserve(256);
    for (size_t k = 0; k < kRingSize; ++k) {
        const size_t idx = (pos + k) % kRingSize;
        const RingEntry &e = g.ring[idx];
        if (e.at_us == 0) {
            continue;
        }
        if (!ring_s.empty()) {
            ring_s += ',';
        }
        const char *sn = stageNameLocal(e.stage);
        ring_s += sn;
        ring_s += '@';
        ring_s += std::to_string(e.at_us);
    }

    std::cerr << "STALLSNAP"
              << " start_us=" << stall_start_us
              << " recover_us=" << recover_us
              << " duration_us=" << dur
              << " last_hb_us=" << last_hb_us
              << " stage=" << stageNameLocal(last_stage)
              << " stage_enter_us=" << stage_enter_us
              << " machine=" << (name[0] ? name : "-")
              << " runnable=" << g.q_runnable.load(std::memory_order_relaxed)
              << " stable=" << g.q_stable.load(std::memory_order_relaxed)
              << " exec=" << g.q_exec.load(std::memory_order_relaxed)
              << " mail=" << g.q_mail.load(std::memory_order_relaxed)
              << " events=" << g.q_events.load(std::memory_order_relaxed)
              << " pend_ev=" << g.q_pend_ev.load(std::memory_order_relaxed)
              << " suppressed=" << suppressed
              << " ring=[" << ring_s << "]"
              << "\n";
}

void observerMain() {
#ifdef __APPLE__
    pthread_setname_np("iod stallobs");
#else
    pthread_setname_np(pthread_self(), "iod stallobs");
#endif

    bool in_stall = false;
    uint64_t stall_start_us = 0;
    uint64_t stall_hb_us = 0;
    uint8_t stall_stage = StageIdle;
    uint64_t stall_stage_enter = 0;
    uint64_t last_seen_seq = 0;

    while (!g.observer_stop.load(std::memory_order_relaxed)) {
        // Enable follows DEBUG flag; start emitting only when on
        const bool want = LogState::instance()->includes(DebugExtra::instance()->DEBUG_STALLSNAP);
        g.enabled.store(want, std::memory_order_relaxed);

        if (!want) {
            in_stall = false;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        const uint64_t now = microsecs();
        const uint64_t hb = g.heartbeat_us.load(std::memory_order_relaxed);
        const uint64_t seq = g.heartbeat_seq.load(std::memory_order_relaxed);
        const uint8_t stage = g.stage.load(std::memory_order_relaxed);
        const uint64_t stage_enter = g.stage_enter_us.load(std::memory_order_relaxed);

        if (hb == 0) {
            // processing has not marked yet
            std::this_thread::sleep_for(std::chrono::milliseconds(kObserverSleepMs));
            continue;
        }

        const uint64_t age = now > hb ? now - hb : 0;

        if (!in_stall) {
            if (age >= kStallThresholdUs) {
                in_stall = true;
                stall_start_us = hb; // approx start = last good heartbeat
                stall_hb_us = hb;
                stall_stage = stage;
                stall_stage_enter = stage_enter;
                last_seen_seq = seq;
            }
        }
        else {
            // recovered when heartbeat advances
            if (seq != last_seen_seq || age < kStallThresholdUs / 2) {
                emitStallSnap(stall_start_us, now, stall_hb_us, stall_stage, stall_stage_enter);
                in_stall = false;
            }
            else {
                // still stalled; keep last stage snapshot frozen at begin
                last_seen_seq = seq;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(kObserverSleepMs));
    }
}

void ensureObserver() {
    bool expected = false;
    if (!g.observer_started.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return;
    }
    g.observer_stop.store(false, std::memory_order_relaxed);
    g_observer = std::thread(observerMain);
    g_observer.detach();
}

} // namespace

const char *stageName(Stage s) {
    return stageNameLocal(static_cast<uint8_t>(s));
}

void init() {
    ensureObserver();
    const uint64_t now = microsecs();
    writeHeartbeat(now);
}

void shutdown() {
    g.observer_stop.store(true, std::memory_order_relaxed);
    // detached; no join required for process exit
}

void syncEnabledFromDebug() {
    ensureObserver();
    const bool want = LogState::instance()->includes(DebugExtra::instance()->DEBUG_STALLSNAP);
    g.enabled.store(want, std::memory_order_relaxed);
}

bool enabled() {
    return g.enabled.load(std::memory_order_relaxed);
}

void markStage(Stage s) {
    if (!g.enabled.load(std::memory_order_relaxed)) {
        return;
    }
    const uint64_t now = microsecs();
    const uint8_t prev = g.stage.load(std::memory_order_relaxed);
    if (prev != static_cast<uint8_t>(s)) {
        g.stage.store(static_cast<uint8_t>(s), std::memory_order_relaxed);
        g.stage_enter_us.store(now, std::memory_order_relaxed);
        pushRing(static_cast<uint8_t>(s), now);
    }
    writeHeartbeat(now);
}

void markMachine(const char *name) {
    if (!g.enabled.load(std::memory_order_relaxed)) {
        return;
    }
    if (!name || !*name) {
        g.name_len.store(0, std::memory_order_relaxed);
        return;
    }
    size_t n = 0;
    while (name[n] && n + 1 < kNameCap) {
        g.name_buf[n] = name[n];
        ++n;
    }
    g.name_buf[n] = '\0';
    g.name_len.store(static_cast<uint8_t>(n), std::memory_order_relaxed);
    writeHeartbeat(microsecs());
}

void publishQueueCounts(uint32_t runnable, uint32_t stable, uint32_t exec, uint32_t mail,
                        uint32_t events, uint32_t pend_ev) {
    if (!g.enabled.load(std::memory_order_relaxed)) {
        return;
    }
    g.q_runnable.store(runnable, std::memory_order_relaxed);
    g.q_stable.store(stable, std::memory_order_relaxed);
    g.q_exec.store(exec, std::memory_order_relaxed);
    g.q_mail.store(mail, std::memory_order_relaxed);
    g.q_events.store(events, std::memory_order_relaxed);
    g.q_pend_ev.store(pend_ev, std::memory_order_relaxed);
}

void heartbeat() {
    if (!g.enabled.load(std::memory_order_relaxed)) {
        return;
    }
    writeHeartbeat(microsecs());
}

} // namespace StallTrace
