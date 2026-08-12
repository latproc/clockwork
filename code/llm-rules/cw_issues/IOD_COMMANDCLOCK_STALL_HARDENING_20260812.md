# COMMANDCLOCK stall hardening (2026-08-12)

Production residual risk after COMMANDCLOCK migration on
`feature/iod-elc-kernel-transport` / 1G2C-122. This is **not** authorization to
edit, build, deploy, or restart a plant.

## Related docs

| Doc | Role |
|-----|------|
| `IOD_TIMER_SOFT_CLOCKS_AND_COMMANDCLOCK_20260812.md` | TIMER vs COMMANDCLOCK architecture |
| `IOD_PROCESSING_LOAD_AND_IDLE_STORMS_20260725.md` | Class C load; STALLSNAP proposal |
| `../IO_NOTIFY_COMMANDCLOCK_DESIGN.md` | elc COMMANDCLOCK + silent IO notify |
| `../2G4C/PIDLISTCLOCK_TIMER_STALL_20260805.md` | Legacy soft-clock plant path |

## Branching

| Item | Choice |
|------|--------|
| Base | `feature/iod-elc-kernel-transport` |
| Work branch | `feature/commandclock-stall-hardening` |
| Merge | Back into elc transport after review/offline test |

Do not pile this work onto the transport tip in place. Do not mix with unrelated
setup-hold / topology work.

## Context

Grab motion on 1G2C already uses **COMMANDCLOCK**
(`M_GrabVelocityUpdate` / `M_GrabPositionUpdate`). That removes TIMER soft-clock
freeze (class A) for those ticks.

COMMANDCLOCK only **generates** ticks when the **processing thread** reaches
`sampleRegularPolls` → `handle_io_sampling` → `dispatchCommandClocks`. Tick
**execution** is the same processing loop (`calcAdjust` actions).

| Class | Failure | COMMANDCLOCK alone? |
|-------|---------|---------------------|
| A Soft-clock freeze | TIMER/PIDLISTCLOCK stops edges | Fixed for Grab by migration |
| B Whole-loop gap | Processing stuck/busy for hundreds of ms–s | **Still open** |
| C Idle/load storm | Empty high loops/s | Mostly fixed on elc; hygiene remains |

## Goals (items 1–3 + hot path)

```text
1 hasPending recovery     → control mail cannot soft-mute forever
2a on-thread deadline     → if ticks stop but loop still turns, demand holds
2c off-thread safe-hold   → if processing soft-locks, listed outs go safe
3 STALLSNAP               → next gap is named; shares heartbeat with 2c
S  SYSTEMEXEC harden      → image scripts stay; Result/concurrency/OS impact fixed
```

After 1–3, deeper hot-path surgery is **evidence-led**.

---

## Item 1 — hasPending coalesce / stale recovery (approved; implement first)

### Problem

`notifyCommandConsumers`:

```text
if (dep->hasPending(command_msg)) continue;  // permanent mute until mail drains
```

Each COMMANDCLOCK tick still walks **every** dependant that declares the command.
Per dependant, at most one matching command should be in flight — not infinite
mute if mail never drains.

### Behaviour (per consumer, not global)

| Case | Action |
|------|--------|
| No matching pending | Enqueue as today |
| Pending fresh (< `notify_period × K`, K=2, min 50 ms) | Skip that dep only |
| Pending stale | Drop matching pending on that dep; enqueue one replacement |
| Disabled / no COMMAND | Skip |

Conveyor velocity and head velocity stay independent queues.

### Code touchpoints

- `Package` enqueue timestamp
- `Receiver::hasPending` / drop matching
- `MachineInstance::notifyCommandConsumers` (+ period argument from COMMANDCLOCK / AI notify)
- Offline tests: one stuck dep recovers; healthy dep no multi-mail flood

### Acceptance

- Stale pending recovers within one period after recovery path runs
- Healthy 50 ms clock does not grow multi-pending mail
- No TIMER semantic change; no plant LPC required for item 1

### Out of scope

- Burst catch-up of missed COMMANDCLOCK slots after long stall (by design)
- True whole-loop soft-lock (items 2c / 3)

---

## Item 2 — Motion deadline fail-safe

### Critical constraint

**Plant LPC deadlines still need the processing loop** to evaluate and zero demand.
They do **not** fire during a full processing soft-lock.

| Layer | When it helps |
|-------|----------------|
| **2a LPC / on-thread** | hasPending mute, slow but alive loop, after recovery |
| **2c Off processing path** | True multi-second soft-lock: elc output lease and/or STALLSNAP observer safe-hold on plant-listed outs |

Do not claim 2a alone fixes multi-second free-run overshoot.

### 2c options

- Re-validate elc **output lease / hang failsafe** for Grab valve AOs
- Observer: heartbeat stale → force allow-listed outs to safe (opt-in; high review)

---

## Item 3 — STALLSNAP

Per `IOD_PROCESSING_LOAD_AND_IDLE_STORMS_20260725.md` (2026-08-12):

- Opt-in independent observer; processing only relaxed-atomic breadcrumbs
- Heartbeat gap ≥ ~100 ms → one rate-limited recovery `STALLSNAP`
- Stages: outer, ZMQ wait, EC, plugins, channels/cmds, scheduler, runnable, stable (machine id), outputs
- Never stops outputs or changes CW state as a diagnostic

Shares heartbeat infrastructure with optional 2c safe-hold.

---

## Hot path inventory

COMMANDCLOCK dispatch is skipped whenever processing does not complete outer
passes that call sampling.

### Outer loop (time sinks)

```text
zmq_poll / in-wait → EC + sampleRegularPolls + dispatchCommandClocks
  → POINT handleChange → plugins → channels → client drain
  → scheduler → poll_machines → checkStableStates → ecat_out
```

### WEBREQUEST

HTTP already on **worker pool** (`1bee0d8d`). Residual: apply/Result on processing,
concurrency, completion storms. Not always the multi-second root cause.

### SYSTEMEXEC (in scope — plant uses heavily)

**Image pipeline is intentional SYSTEMEXEC + scripts** (do not remove):

| Script | Role |
|--------|------|
| `machine/scripts/camera_capture.sh` | wget + ImageMagick crop |
| `machine/scripts/image_weight.sh` | ImageMagick annotate weight overprint (`IMAGEADDWEIGHT`) |

Also: disk temp, panel helpers, command queues.

Plugin already **forks async** (`waitpid` WNOHANG). Risks:

1. ImageMagick CPU contention with RT/processing
2. Concurrent fork/`convert` storms
3. Finish path `read_file_to` → `Result` on processing if stdout large
4. fork hitch under memory pressure

| Fix | Notes |
|-----|-------|
| **S1** Result/Errors size cap | Early on work branch |
| **S4** nice/ionice on capture/overprint scripts | Keep scripts; lower OS impact |
| **S3** Bounded concurrent children | If bursts correlate with gaps |
| **S2** Deferred Result apply | If STALLSNAP blames finish-apply |

### Other candidates

ECSETUPRECIPE worker (already off hot path), channels, plugins setIntValue,
stableQ/LIST hygiene, ecat_out REQ, scheduler handshake, `global_lists_mutex`
scope, OS/off-CPU.

### Principles

1. Never block processing on network, HTTP, mailbox SDO, or unbounded disk.
2. Workers own long I/O; processing applies small status.
3. Budget channel/plugin/command work per pass.
4. Measure (STALLSNAP) before a second control thread.

Optional later: early-loop COMMANDCLOCK sample before channels; light control path.

---

## Implementation sequence

| Step | Content |
|------|---------|
| Branch | `feature/commandclock-stall-hardening` from elc tip |
| 1 | hasPending stale recovery + tests |
| 1b | SYSTEMEXEC S1 + script S4 (nice/ionice) |
| 2 | STALLSNAP breadcrumbs (default off); audit output lease |
| 3 | Optional 2c observer safe-hold (high risk; allow-list) |
| 4 | Optional 2a Grab seeking LPC deadline |
| 5 | S2/S3 if evidence blames SYSTEMEXEC apply/concurrency |

Deploy only with operator-approved build/install/restart and rollback binary.

## Change log

| Date | Note |
|------|------|
| 2026-08-12 | Initial handoff from architecture discussion; item 1 approved first |
