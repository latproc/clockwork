# iod idle / processing CPU fixes

**Branch:** `prod-experimental-mqtt-fix`  
**Binary:** legacy EtherCAT path `iod_sdo` (main thread renames to `iod_main`)  
**Date:** 2026-07-28  
**Status:** Deployed and measured on plant; later commits refine out-service and
SetState under the quiet path — see §4.4–4.5 and commits below.

This document records why processing CPU was high with empty runnable queues,
what was wrong, what each fix does, latency trade-offs, how to measure, and what
remains.

---

## 1. Problem summary

With the plant largely idle (empty machine work, no digital storm), `iod_sdo`
still showed:

| Symptom | Typical observation |
|---------|---------------------|
| Process CPU | ~50–70% total |
| `iod processing` thread | ~40%+ |
| Outer processing loops | ~330–480 loops/s |
| PROCSNAP | `brk_out` ~160/s; scheduler or ECAT_OUT wakes dominant |
| Runnable / stableQ | Often empty or a single waiting SetState |

Expectation: when quiet, processing should sleep on interruptible `zmq_poll`
and only wake for real EtherCAT digital edges, commands, or paced TIMER work.

---

## 2. Design constraints

1. **Digital (POINT) edges must not be delayed by idle rate-limits.**  
   No outer-loop `usleep` after poll; rate-limit only via interruptible poll and
   EC pull stretch that restores busy rate when IO is urgent.

2. **Analog effectiveness** may accept a lower sample rate into Clockwork when
   idle (latest domain snapshot wins; no backlog of stale samples).

3. **Legacy ecrt path** (`iod_sdo`, not kernel EtherCAT transport): outputs use
   process-image / pending-out queues, not kernel shadow apply.

4. **Diagnostics** are opt-in (`DEBUG DEBUG_PROCSNAP on`) so production logs
   stay quiet by default.

5. **Pending outputs must complete under quiet pacing.** Softstart / SetState
   on digitals and multi-bit outs must not hang because absorb windows or
   over-aggressive pending clear discarded real work.

---

## 3. Root causes (ordered by impact)

### 3.1 Pending outputs never cleared (`updatesWaiting` stuck)

**Files:** `IOComponent.cpp`, interaction with `ProcessingThread.cpp`

`IOComponent::processAll()` returned early when `updatedComponentsIn` was empty
(no *input* domain changes this frame). Cleanup of `updatedComponentsOut` lived
*after* that return, so:

- After `Output::turnOn` / `turnOff`, entries stayed in `updatedComponentsOut`.
- TX-only output bits often never appear as input-domain changes.
- `Output::turnOn`/`turnOff` did not set `pending_value`, so the
  `pending_value == address.value` clear path never fired for digitals.
- `updatesWaiting()` stayed true forever.

Effect on processing:

- Outer path treated dirty outs as work every quiet pace → `brk_out` thrash.
- Each cycle did an ECAT_OUT REQ/REP handshake → many outer loops with empty
  machine work.

### 3.2 Idle work tiers too coarse

**File:** `ProcessingThread.cpp`

Treating the following as full “urgent” forced busy EC pull and 1 ms polls:

| Signal | Problem if treated as urgent forever |
|--------|--------------------------------------|
| `updatesWaiting()` | Permanent thrash (see 3.1) |
| Waiting `SetStateAction` (exec only) | e.g. machine stuck “turning_on”; never quiet |
| Stable-state TIMER re-queues only | Hundreds of Hz full outer loops |

### 3.3 EtherCAT always fed Clockwork every collect

**File:** `ecat_thread.cpp`

When `POLLING_DELAY == CYCLE_DELAY` (~2 ms), every bus collect with any domain
change (analogs almost always change) pushed process data to CW. Keep-alive
default was 4 ms, which with a ~2 ms period could also force ~500 Hz pings.

### 3.4 Scheduler TIMER storms

**File:** `Scheduler.cpp`

Many TIMER items ready close together woke processing at hundreds of Hz via ZMQ,
even when each fire only re-queued stable checks.

### 3.5 `usleep` after poll

**File:** `wait_for_work.cpp` (and principle applied in processing loop)

Post-loop `usleep` delayed the next poll window and could hold off EtherCAT /
timer / command servicing. Idle rate-limiting must use interruptible `zmq_poll`
only.

### 3.6 Legacy turnOn / turnOff + quiet out-service (follow-up)

**Files:** `IOComponent.cpp`, `ProcessingThread.cpp`, `SetStateAction.cpp`  
**Commit:** `7e062d0c` (2026-07-28)

After idle pacing landed, a second class of hang appeared under quiet EC:

1. **`last_event` after `markChange()`** — `markChange()` only writes the
   process-image bit when `last_event` is `e_on`/`e_off`. Setting the event
   *after* left mask dirty but data still 0 → SetState stayed Running as
   `turning_on` while the bus never got the bit.
2. **`clearPendingOutputUpdates()` when `getUpdates()` returned null** — discarded
   real pending digitals/analogs still waiting for a successful mask build /
   domain echo (softstart stuck `starting`).
3. **Out-service pace tied to quiet pull (≥5 ms)** — pending outs waited behind
   analog quiet absorb; multi-bit/softstart outs completed too slowly.
4. **SetState waited for domain echo only** — on TX-only digitals, intermediate
   `turning_on`/`turning_off` must count as commanded match so CW POINT state
   advances immediately.

---

## 4. Changes by area

Commits are intentionally small; this section maps theme → **current** behaviour.

### 4.1 EtherCAT → Clockwork feed (`ecat_thread.cpp`)

- Default **keep-alive 50 ms** (was 4 ms); floor keep-alive interval at 50 ms.
- Bus period follows `SYSTEM.CYCLE_DELAY` (**1000 µs when POINTSSTARTUP is on**).
- **Digital ASAP:** each cycle peeks the domain vs a dig shadow
  (`domainHasDigitalChange`). POINT / 1-bit (non-`regular_poll`) edges
  **collect+push immediately** (~1 bus period while powered).
- **Analog paced:** ANALOG/COUNTER on `regular_polls` do **not** force a push.
  They use `pull_due` from `get_polling_time()` (quiet **5 ms**) so LIST/PID/plugins
  are not free-run at 1 kHz on dither. dig_shadow advances only on push so the
  next collect still sees the full analog delta (latest wins).
- Keep-alive still forces a rare full push.

**Digital:** no longer waits for the quiet pull window.  
**Analog:** still paced; continuous analog noise does **not** exit “analog quiet.”

### 4.2 Scheduler wake floor (`Scheduler.cpp`)

- Before signalling CW that scheduled work is ready, enforce **≥ 2×
  `SYSTEM.CYCLE_DELAY`** since the last signal (`get_cycle_time()`, live —
  not a hard-coded 2 ms; earlier experiments used 10 ms then 2 ms fixed).
  POINTSSTARTUP sets bus to 1000 µs (on) / 2000 µs (off) so the floor is
  2 ms / 4 ms respectively.
- Wait to an absolute deadline in short sleep chunks so `Scheduler::add()`
  interrupts cannot defeat the floor.
- Stable/exec recheck in `ProcessingThread` uses the same **2× CYCLE_DELAY**.
- When the batch runs, all ready TIMER items still drain in `e_running`.
- Digital IO does **not** use this path.

### 4.2b Startup ENABLE storms

`POINTSSTARTUP` ENABLE of large LISTs (inputs/guards/panel/outputs) causes a
**machine** storm (runnable/mail/stable), not an analog free-run. That path is
still rate-limited by stable/exec pacing (2× CYCLE_DELAY) and absorb of empty EC frames.
Digital floods still event immediately (correct for end-stops). ENABLE storms
should **not** reintroduce free-running idle thrash from analog dither.

Plant checks (2GRAB, dig-ASAP binary): boot OP+INIT peaked ~1400 runnable /
~1400 mail+events but **drained in ~1–2 s** (not free-run). Staged ENABLE of
outputs/panel/auto was smaller (~400–500). Sustained thrash after Auto ENABLE
was `M_GrabBaleClamp` HomingSafe Start↔Wait (plant LPC, separate fix), not list
size.

### 4.3 No poll-loop `usleep` (`wait_for_work.cpp`)

- Remove `usleep(1)` under KEEPSTATS after empty poll.
- Comment documents: rate-limit only via interruptible `zmq_poll`.

### 4.4 Pending output clear + legacy turnOn/turnOff (`IOComponent.cpp`)

- Do **not** early-return from `processAll` when there are no input updates;
  always run pending-out cleanup when `updates_sent`.
- Set `pending_value` in `Output::turnOn` / `turnOff` so
  `pending_value == address.value` can clear digitals after send.
- Keep `outputs_waiting` in sync under the same lock.
- **Legacy ecrt:** set `last_event = e_on` / `e_off` **before** `markChange()` so
  the update image actually receives the bit (`7e062d0c`).
- **Kernel path (`USE_KERNEL_ETHERCAT`):** apply shadow immediately; do not leave
  digitals in `updatedComponentsOut` (nothing would clear them without domain
  echo).

### 4.5 Processing wait loop (`ProcessingThread.cpp`)

**Urgency tiers**

| Tier | Meaning | Poll / EC behaviour |
|------|---------|---------------------|
| `io_urgent` | Global pending events or `io_work_queue` | Busy EC pull; short wait |
| `machine_urgent` | Mail or per-machine pending events | Short wait; full outer path |
| `exec_only_waiting` | `executingCommand` only (e.g. waiting SetState) | Paced 2× CYCLE_DELAY like stable |
| `stable_pending` | Stable-state re-queue only | Paced 2× CYCLE_DELAY |
| `updatesWaiting` | Output shadow dirty | Service every **bus period** (min 1 ms); not “urgent” for EC pull |

**In-wait EC absorb**

- On EC frame: always `HandleIncomingEtherCatData` first (digital first).
- Break out immediately for digital work / mail / machine events.
- Absorb empty domain frames (`snap_absorb`) without a full outer loop.
- Service scheduler handshake **in-wait** so TIMER can fire without a full loop
  when no machine work remains.
- Drain client time-sync (`CMD_ITEM`) without forcing outer loops.
- ECAT_OUT mid-update still forces outer path; idle clears revents.
- Pending outs break out of absorb at **bus cadence** (not quiet-pull cadence)
  so softstart is not held behind 5 ms analog quiet.

**Quiet EC pull (analog / process-data delivery)**

- When **not** `io_urgent`: `set_polling_time` to **max(busy_pull, 5000 µs)** →
  typically **5 ms** quiet.
- When `io_urgent`: restore busy pull (`cycle_delay`, min 100 µs).
- Max added digital observation lag while quiet ≈ one quiet pull window
  (**~5 ms**) until the edge is observed and busy pull is restored.
  Digital ASAP push in `ecat_thread` still pushes edges every bus cycle once
  the domain is collected.

**Outputs (legacy path)**

- Pace outer out-service at **`get_cycle_time()`** (min 1 ms), **not** quiet
  pull (5 ms).
- **Do not** call `clearPendingOutputUpdates()` when `getUpdates()` returns null
  on the legacy path — that discarded real pending turnOn/setValue and left
  SetState Running forever.
- Kernel builds: after a successful update send, `clearPendingOutputUpdates()`
  is still valid (shadow applied immediately).
- Pending outs clear when `processAll` sees `updates_sent && pending_value ==
  address.value`, or when a later `getUpdates()` succeeds.

**SetStateAction (digital POINT)**

- After `turnOn`/`turnOff`, treat intermediate `turning_on` / `turning_off` as
  commanded match so CW POINT completes immediately (softstart not blocked on
  domain echo for TX-only bits).

**Plugins while quiet**

- Service plugins **in-wait** at ≥10 ms when fully idle so plugins do not force
  ~100 full outer loops/s.

**PROCSNAP (opt-in)**

Extended line fields:

- `outN`, `hw` (pre/init/op)
- `absorb`, `brk_dig`, `brk_out`, `brk_exec`, `brk_oth`
- `oth[1..7]` ZMQ index wake counts (1=CMD, 2=SCHEDULER, 3=ECAT_OUT, …)

Enable:

```bash
touch /tmp/iod-verbose   # or: echo 3600 > /tmp/iod-verbose
# after iod up:
printf 'DEBUG DEBUG_PROCSNAP on;\n' | /opt/latproc/iod/iosh
tail -f /tmp/iod.log | grep PROCSNAP
```

Disable:

```bash
printf 'DEBUG DEBUG_PROCSNAP off;\n' | /opt/latproc/iod/iosh
rm -f /tmp/iod-verbose
```

Also useful:

```bash
printf 'SHOW HEALTH;\nSHOW PROCSNAP;\nSHOW CYCLING;\n' | /opt/latproc/iod/iosh
```

---

## 5. Analogs vs digitals (latency and effectiveness)

| | Digital POINT | Analog / COUNTER |
|--|---------------|------------------|
| Wake path | Bit change → `io_work_queue` / events | Domain noise often sets `num_updates` |
| CW machine storm | Event path | `regular_polls` / `sampleRegularPolls` (IOTIME/filter) |
| Quiet pull | Restores busy immediately when urgent | Sample into CW at pull rate; **latest value wins** |

**What quiet pull does to analogs**

- Does **not** buffer a history of old values.
- Reduces how often properties / `IOTIME` advance in Clockwork when idle.
- At **5 ms**, typical plant analogs (oil temp, pressure, bus voltage, motor
  current) remain effective for monitoring, CLOCKING, and soft thresholds.
- **Do not** stretch further without checking any analog used as a fast trip.

**What it does not damage**

- Correctness of the last sample applied.
- Digital edge handling (separate, event-driven, dig-ASAP push + busy pull restore).

Further analog-only options (not done unless requested): longer quiet pull
(e.g. 10–20 ms); push only on digital change + keep-alive (larger analog lag).

---

## 6. Measured results (plant, settled idle)

Approximate before → after on the original idle work (pre–out-service refine):

| Metric | Before | After |
|--------|--------|-------|
| loops/s | ~330–480 | ~10–15 |
| `brk_out` | ~160/s | ~0 when no pending outs |
| `outN` / `hw` | stuck / spinning | `0` / `op` when quiet |
| `iod processing` (short sample) | ~40%+ | ~9% |
| Process total (short sample) | ~50–60%+ | ~25–30% |
| Scheduler thread | hot | ~0.3% |

Remaining process CPU is largely **real bus work**: ethercat thread, domain
`processAll` absorb (~200/s at 5 ms pull), ecat timer — not empty-queue thrash.

Under HMI/sampler activity, `SHOW HEALTH` may still show elevated **LOAD BUSY**.
On 2C-120 / 2GRAB PID `127846` (~27 h uptime, 2026-07-29), live samples were
**~110–125 loops/s** with THRASH none while core panels were active. Earlier
quieter windows on this plant have shown **~30–60 loops/s**; re-measure quiet
vs auto after channel clients settle.

---

## 7. Finding the process

The main thread calls `pthread_setname_np(..., "iod_main")`, so `/proc/<pid>/comm`
is **`iod_main`**, not `iod_sdo`.

| Goal | Command |
|------|---------|
| Service PID | `svstat /etc/service/iod` |
| By binary path | `pgrep -af iod_sdo` |
| By comm name | `pgrep -x iod_main` / `ps -C iod_main` |
| Prefer not | `pgrep -x iod_sdo` (often misses) |

Deploy note: stop service before replacing the binary to avoid `ETXTBSY`.

---

## 8. Files touched

| File | Role |
|------|------|
| `iod/src/ecat_thread.cpp` | Keep-alive; pull_due; dig ASAP; push only on change/keepalive |
| `iod/src/Scheduler.cpp` | 2× SYSTEM.CYCLE_DELAY inter-signal floor |
| `iod/src/wait_for_work.cpp` | Remove KEEPSTATS `usleep` |
| `iod/src/IOComponent.cpp` | Pending-out clear; `pending_value`; last_event before markChange |
| `iod/src/ProcessingThread.cpp` | Urgency tiers; in-wait absorb; quiet 5 ms pull; bus-rate out service |
| `iod/src/SetStateAction.cpp` | Accept turning_on/off as commanded match |

Key commits (mqtt-fix lineage, oldest → newest on this theme):

| Commit | Theme |
|--------|--------|
| `42a46460` | ecat: throttle CW domain push when idle |
| `16965333` | scheduler: floor CW wakes at 10 ms (superseded) |
| `15886ac5` | no usleep after empty poll |
| `97c399a8` | clear pending outs without input changes |
| `a9017630` | urgency tiers + in-wait absorb |
| `53fd85dd` / dig-ASAP series | digital edges every cycle; pace analog-only |
| `fd2a1f97` / `be7aa2cf` | sched/stable floor → 2× CYCLE_DELAY |
| `989240af` | 5 ms analog quiet pull; stable pace alignment |
| `7e062d0c` | last_event order; no null-getUpdates clear; bus-rate outs; SetState |

Related plant/config work from the same effort (may already be committed
elsewhere): LIST `propagate_member_checks`, TIMER AND short-circuit, iod.sh
verbose switch — not all are in the patch set above.

---

## 9. Verification checklist

1. Build: `make -C iod/build -j$(nproc) iod_sdo`
2. Deploy: `svc -d /etc/service/iod`; copy binary; `svc -u /etc/service/iod`
3. Enable PROCSNAP briefly; confirm `outN=0`, `hw=op`, low `brk_out`,
   `loops/s` low, `absorb` dominating over breaks when idle.
4. Exercise digital IO; confirm edges still processed promptly.
5. Exercise softstart / multi-bit outputs; confirm SetState completes (not stuck
   `turning_on` / `starting`).
6. Spot-check analogs (oil temp, pressure) still update on HMI / CLOCKING.
7. Disable PROCSNAP / verbose when done.

---

## 10. Residual risks / follow-ups

1. Quiet pull **5 ms** adds up to ~5 ms digital observation lag while idle
   until the first edge restores busy pull — acceptable for this plant if
   verified on critical sensors. Dig ASAP push still delivers edges every bus
   cycle once collected.
2. Waiting SetState that never completes for non-IO reasons still costs paced
   rechecks; plant logic may need separate investigation.
3. Further CPU reduction: analog-only domain push suppression or longer quiet
   pull — document trade-offs before enabling.
4. Under HMI/sampler load, re-measure quiet vs auto after channel clients are
   stable (see channel handshake fixes on this branch).

---

## 11. Glossary (PROCSNAP)

| Field | Meaning |
|-------|---------|
| `loops/s` | Full outer processing iterations per second |
| `absorb` | EC frames handled in-wait without outer loop |
| `brk_dig` | Break for digital / pending events / io work |
| `brk_out` | Break for paced output / hardware-init path |
| `brk_exec` | Break for machine exec/mail/paced recheck |
| `brk_oth` | Scheduler handshake completed in-wait (counter name historical) |
| `oth[2]` | Scheduler ZMQ wakes |
| `oth[3]` | ECAT_OUT ZMQ wakes |
| `outN` | `updatesWaiting()` size |
| `hw` | `pre` / `init` / `op` hardware state |

---

## 12. Related: iod-elc HW-init thrash (2026-07-30) — **not the legacy bug**

On **`feature/iod-elc-kernel-transport`** / `iod-elc` (1G2C dual-domain, five
servos offline forever), plants saw `loops/s≈315`, `brk_out≈loops`, `absorb=0`
with **empty** `updatedComponentsOut`. gdb: `hardware_state=s_hardware_init`,
`machine_is_ready=false` because `ECInterface::operational()` requires **all**
configured modules OP.

That is a **different** thrash from §3.1 on this branch (pending outs stuck).
Typical ecrt plants do not run with optional domain slaves offline as a steady
state, so this elc failure mode is **usually not seen** on `iod_sdo`.

| | This branch (`iod_sdo` / ecrt) | Elc dual-domain thrash |
|--|-------------------------------|-------------------------|
| Primary cause | `updatesWaiting()` stuck true | HW never leaves init; `!operational` forces `brk_out` |
| Quiet target after fix | ~10–15 loops/s | ~20 loops/s with offline servos |
| Code port from elc? | **Do not** port kernel `active+link` ready / `kernelPromoteIoOperational` | Elc-only |
| Optional general port | Pure exec-wait 50 ms pace (if ERROR modules burn loops) | Already on elc |

Full write-up and dual-branch agent port rules:
`feature/iod-elc-kernel-transport` → `iod/IDLE_CPU_FIXES.md` (kernel elc section)
and `iod/docs/LEGACY_ECRT_REMOVAL_PLAN.md`.

**Agent rule:** when moving patches between `prod-experimental-mqtt-fix` and
elc, classify **general** vs **bus-specific**; do not reintroduce elc-only ready
logic into this tree “for symmetry.”
