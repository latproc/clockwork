# iod idle / processing CPU fixes

**Branch:** `prod-experimental-mqtt-fix`  
**Binary:** legacy EtherCAT path `iod_sdo` (main thread renames to `iod_main`)  
**Date:** 2026-07-26  
**Status:** Deployed and measured on plant; see commits that introduce each change.

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

- Outer path treated dirty outs as work every quiet pace (~5 ms) → `brk_out` ~160/s.
- Each cycle did an ECAT_OUT REQ/REP handshake → ~2 outer loops per out service
  → ~330 loops/s with empty machine work.

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

---

## 4. Changes by area

Commits are intentionally small; this section maps theme → behaviour.

### 4.1 EtherCAT → Clockwork feed (`ecat_thread.cpp`)

- Default **keep-alive 50 ms** (was 4 ms); floor keep-alive interval at 50 ms.
- Bus period follows `SYSTEM.CYCLE_DELAY` (**1000 µs when POINTSSTARTUP is on**).
- **Digital ASAP:** each cycle peeks the domain vs a dig shadow
  (`domainHasDigitalChange`). POINT / 1-bit (non-`regular_poll`) edges
  **collect+push immediately** (~1 bus period while powered).
- **Analog paced:** ANALOG/COUNTER on `regular_polls` do **not** force a push.
  They use `pull_due` from `get_polling_time()` (quiet ~5 ms) so LIST/PID/plugins
  are not free-run at 1 kHz on dither. dig_shadow advances only on push so the
  next collect still sees the full analog delta (latest wins).
- Keep-alive still forces a rare full push.

**Digital:** no longer waits for the quiet pull window.  
**Analog:** still paced; continuous analog noise does **not** exit “analog quiet.”

### 4.2 Scheduler wake floor (`Scheduler.cpp`)

- Before signalling CW that scheduled work is ready, enforce a floor derived
  from **live SYSTEM settings**: `min_signal ≈ 2 × SYSTEM.POLLING_DELAY`
  (fallback `2 × CYCLE_DELAY`, then 2000 µs). Clamp [500, 20000] µs.
  POINTSSTARTUP (`POLLING_DELAY:=1000`) → ~2 ms; idle 2000 → ~4 ms.
- When the batch runs, all ready TIMER items still drain in `e_running`.
- Digital IO does **not** use this path.
- Processing `stable_check` / quiet pull use the same POLLING_DELAY base.

### 4.2b Startup ENABLE storms

`POINTSSTARTUP` ENABLE of large LISTs (inputs/guards/panel/outputs) causes a
**machine** storm (runnable/mail/stable), not an analog free-run. That path is
still rate-limited by stable/exec pacing (~2 ms) and absorb of empty EC frames.
Digital floods still event immediately (correct for end-stops). ENABLE storms
should **not** reintroduce free-running idle thrash from analog dither.

### 4.3 No poll-loop `usleep` (`wait_for_work.cpp`)

- Remove `usleep(1)` under KEEPSTATS after empty poll.
- Comment documents: rate-limit only via interruptible `zmq_poll`.

### 4.4 Pending output clear (`IOComponent.cpp`)

- Do **not** early-return from `processAll` when there are no input updates;
  always run pending-out cleanup when `updates_sent`.
- Set `pending_value` in `Output::turnOn` / `turnOff` so
  `pending_value == address.value` can clear digitals after send.
- Keep `outputs_waiting` in sync under the same lock.

### 4.5 Processing wait loop (`ProcessingThread.cpp`)

**Urgency tiers**

| Tier | Meaning | Poll / EC behaviour |
|------|---------|---------------------|
| `io_urgent` | Global pending events or `io_work_queue` | Busy EC pull; short wait |
| `machine_urgent` | Mail or per-machine pending events | Short wait; full outer path |
| `exec_only_waiting` | `executingCommand` only (e.g. waiting SetState) | Paced ~10 ms like stable |
| `stable_pending` | Stable-state re-queue only | Paced ~10 ms |
| `updatesWaiting` | Output shadow dirty | Paced ≥ 5 ms out service; not “urgent” |

**In-wait EC absorb**

- On EC frame: always `HandleIncomingEtherCatData` first (digital first).
- Break out immediately for digital work / mail / machine events.
- Absorb empty domain frames (`snap_absorb`) without a full outer loop.
- Service scheduler handshake **in-wait** so TIMER can fire without a full loop
  when no machine work remains.
- Drain client time-sync (`CMD_ITEM`) without forcing outer loops.
- ECAT_OUT mid-update still forces outer path; idle clears revents.

**Quiet EC pull**

- When **not** `io_urgent`: `set_polling_time` to **10 ms** (or max with cycle).
- When `io_urgent`: restore busy pull (`cycle_delay`, min 100 µs).
- Max added digital lag while quiet ≈ one quiet pull window (~10 ms) until the
  edge is observed and busy pull is restored.

**Outputs**

- Pace outer out-service; if operational and `getUpdates()` builds nothing,
  `clearPendingOutputUpdates()` so stale entries cannot spin forever.

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
- At **10 ms**, typical plant analogs (oil temp, pressure, bus voltage, motor
  current) remain effective for monitoring, CLOCKING, and soft thresholds.
- **Do not** stretch further without checking any analog used as a fast trip.

**What it does not damage**

- Correctness of the last sample applied.
- Digital edge handling (separate, event-driven, busy pull restore).

Further analog-only options (not done unless requested): longer quiet pull
(e.g. 20 ms); push only on digital change + keep-alive (larger analog lag).

---

## 6. Measured results (plant, settled idle)

Approximate before → after on this work:

| Metric | Before | After |
|--------|--------|-------|
| loops/s | ~330–480 | ~10–15 |
| `brk_out` | ~160/s | ~0 |
| `outN` / `hw` | stuck / spinning | `0` / `op` |
| `iod processing` (short sample) | ~40%+ | ~9% |
| Process total (short sample) | ~50–60%+ | ~25–30% |
| Scheduler thread | hot | ~0.3% |

Remaining process CPU is largely **real bus work**: ethercat thread, domain
`processAll` absorb (~90/s at 10 ms pull), ecat timer — not empty-queue thrash.

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
| `iod/src/ecat_thread.cpp` | Keep-alive; pull_due; push only on change/keepalive |
| `iod/src/Scheduler.cpp` | 10 ms inter-signal floor |
| `iod/src/wait_for_work.cpp` | Remove KEEPSTATS `usleep` |
| `iod/src/IOComponent.cpp` | Pending-out clear; `pending_value` on turnOn/Off |
| `iod/src/ProcessingThread.cpp` | Urgency tiers; in-wait absorb; quiet pull; PROCSNAP |

Related plant/config work from the same effort (may already be committed
elsewhere): LIST `propagate_member_checks`, TIMER AND short-circuit, iod.sh
verbose switch — not all are in the uncommitted patch set above.

---

## 9. Verification checklist

1. Build: `make -C iod/build -j$(nproc) iod_sdo`
2. Deploy: `svc -d /etc/service/iod`; copy binary; `svc -u /etc/service/iod`
3. Enable PROCSNAP briefly; confirm `outN=0`, `hw=op`, low `brk_out`,
   `loops/s` low, `absorb` dominating over breaks when idle.
4. Exercise digital IO; confirm edges still processed promptly.
5. Spot-check analogs (oil temp, pressure) still update on HMI / CLOCKING.
6. Disable PROCSNAP / verbose when done.

---

## 10. Residual risks / follow-ups

1. Quiet pull **10 ms** adds up to ~10 ms digital observation lag while idle
   until the first edge restores busy pull — acceptable for this plant if
   verified on critical sensors.
2. Waiting SetState that never completes (e.g. output echo `turning_on`) still
   costs paced rechecks; plant logic may need separate investigation.
3. Further CPU reduction: analog-only domain push suppression or longer quiet
   pull — document trade-offs before enabling.
4. Commit series should land on the production branch after plant sign-off.

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

## Kernel elc path: CiA402 inputs must not be reapply targets (2026-07-28)

**Symptom:** After multi-domain kernel transport, `ethercat domain -v` showed
live `0x6041`/`0x603F` (e.g. A.76 = 0x76) while iod `Error.VALUE` stayed 0 and
controlword never pulsed `0x80`.

**Cause:** `reapplyOutputDefaults()` walked `output_points`, which also lists
`DIGITALVALUE`/`COUNTER` (PDO registration). Those often have `VALUE=0`, so
`setValue(0)` → `applyKernelOutputValue` on **input** PDO bytes. Every
`mergeKernelOutputShadow` then forced zeros over the kernel input snapshot.

**Fixes (keep):**

1. `reapplyOutputDefaults` — only `ANALOGOUTPUT` / true `DirOutput` POINTs.
2. `DigitalValue` — `DirInput` + regular poll; `setValue` must not publish DirInput
   into the output shadow.
3. `updateDomain` — do not expand `g_kernel_output_mask` from full CW process
   masks (inputs).
4. Regular-poll `handleChange` — always run `filter()` so machine `VALUE` tracks
   the wire (LPC `Error.VALUE` / statusword).
5. Plant LPC `CIA402_Setup_ESTUN` — fault clear when Module OP (Guard not
   required); Guard only gates enable. `NotReady` only when OP + no error + Guard off.

**Verify (slave 29 example):**

```bash
ethercat domain -v | sed -n '/0:29, SM3/,/0:30/p'
printf 'GET IA_CoreVB1MotorAlarm;\nDESCRIBE M_CoreVB1PumpServo;\n' | /opt/latproc/iod/iosh
for i in 1 2 3 4 5 6 7 8; do
  ethercat upload -p 29 0x6040 0 --type uint16
  ethercat upload -p 29 0x603F 0 --type uint16
  sleep 0.25
done
```

Expect: GET matches domain; with A.76, setup enters ClearFault and SDO may show
`6040=0x80`; after clear, `603F=0` and setup can sit NotReady if E24/Guard is false.
