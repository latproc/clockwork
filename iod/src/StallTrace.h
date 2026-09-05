/*
    Opt-in processing stall diagnostic (STALLSNAP).

    Processing path: relaxed-atomic breadcrumbs only when enabled.
    Observer thread: detects heartbeat gap, emits one rate-limited STALLSNAP
    after recovery. Never stops outputs or changes CW state.

    Enable:  DEBUG DEBUG_STALLSNAP on
    Disable: DEBUG DEBUG_STALLSNAP off

    Not built/installed until operator-approved plant deploy.
*/

#ifndef __cw_stall_trace_h__
#define __cw_stall_trace_h__

#include <cstdint>
#include <cstddef>

namespace StallTrace {

enum Stage : uint8_t {
    StageIdle = 0,
    StageOuterHousekeeping = 1,
    StageZmqPoll = 2,
    StageEcatHandle = 3,
    StagePlugins = 4,
    StageChannelsCommands = 5,
    StageScheduler = 6,
    StagePollMachines = 7,
    StageStableStates = 8,
    StageOutputs = 9,
    StageCount
};

const char *stageName(Stage s);

/** Call once from processing thread startup (safe if repeated). */
void init();

/** Shut down observer (program exit). */
void shutdown();

/** Sync enable flag from DEBUG_STALLSNAP (call once per outer loop). */
void syncEnabledFromDebug();

bool enabled();

/**
 * Hot path: set stage + heartbeat. No-op when disabled (single relaxed load).
 * No alloc, log, lock, or I/O.
 */
void markStage(Stage s);

/**
 * Optional machine name breadcrumb (truncated). No-op when disabled.
 * name may be null.
 */
void markMachine(const char *name);

/**
 * Publish queue depth scalars (processing thread only). No-op when disabled.
 */
void publishQueueCounts(uint32_t runnable, uint32_t stable, uint32_t exec, uint32_t mail,
                        uint32_t events, uint32_t pend_ev);

/** Heartbeat-only (same as markStage without changing stage). */
void heartbeat();

/**
 * Always update heartbeat (even when DEBUG_STALLSNAP is off).
 * Command-port timeout, SIGTERM _exit, and output-lease stale checks use this.
 */
void tickHeartbeat();

uint64_t heartbeatUs();
uint64_t heartbeatSeq();
uint8_t stage();
uint64_t heartbeatAgeUs();

} // namespace StallTrace

#endif
