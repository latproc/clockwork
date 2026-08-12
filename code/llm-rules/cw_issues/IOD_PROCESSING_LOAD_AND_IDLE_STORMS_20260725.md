# IOD processing load and idle storms (2026-07-25)

Learnings from `feature/iod-elc-kernel-transport` / 1G2C-122 kernel-elc work.
Use this when an operational machine shows high CPU, sticky timers, startup
overload, or empty-but-busy `PROCSNAP`.

## Symptom pattern (abandoned “overload” branch class)

- Startup never settles: CPU pegged, long enable storms.
- Runtime “gets busy”: real work is fine, but CW feels delayed.
- `PROCSNAP` shows high `loops/s` with **empty** exec/mail/stable — or only
  residual noise that should not dominate the loop.

Often **several** causes stack; fixing one alone can hide others.

## Portable Clockwork commits (cherry-pick order)

Do **not** confuse with kernel-elc-only commits (`4835fee3` and similar).
These commits are **possible follow-up candidates**, not a standing instruction
to cherry-pick them onto a plant release. First apply and measure the narrower
plant-side fixes (especially the generated `L_Globals` option), then review the
target IOD history and test the commits offline before any approved build or
deployment.

```text
41b7b994  cw: Fix disable/action deadlock, scheduler storm, and idle processing load
afc04fbd  cw: Quiet LIST cascade and TIMER AND short-circuit for idle load
56d1836e  cw: Quiet idle processing; opt-in PROCSNAP and MEMSNAPSHOT
```

Plant-side (often gitignored under `code/` / SVN):

- `L_Globals LIST (propagate_member_checks:false)` + `update_LIST` must emit it.
- Pump plugin: no per-sample `setIntValue` of noisy diags; use `Pressure.VALUE`
  for bind if desired; control path write-on-change only.

### 2G46 observation (2026-08-02)

On Grab installation 2G46 / serial 116, loading generated
`L_Globals LIST (propagate_member_checks:false) ...` worked: `L_Globals`
stable-state evaluations fell from thousands to one after restart. `SHOW
HEALTH` nevertheless remained near `LOAD BUSY loops/s=50` because the legacy
soft clocks still schedule work at 30 ms, 50 ms, 100 ms, and 200 ms cadences.
`THRASH none` and modest processing-thread CPU showed this was scheduled legacy
work, not proof of CPU saturation or failure of the LIST option.

No portable Clockwork load commits were tried on this installation. The three
commits above may be needed if reducing the remaining BUSY indication is an
operational requirement, but they require a separate source review, offline
test, approved build, rollback binary, and controlled restart. Do not slow the
motion/control clocks merely to make `SHOW HEALTH` report a lower number.

## Root-cause map

### 1. LIST member cascade

- Default: any member `setNeedsCheck` → LIST → all LIST dependents.
- Required for `ALL`/`ANY` **state** lists (`L_Modules` → `M_Startup`).
- Fatal for bag lists (`L_Globals`) with hundreds of members and enable-only
  consumers.
- Fix: `propagate_member_checks:false` on the LIST constructor.
- **Never** put that option on ALL/ANY state lists.
- `update_LIST.sh` / awk must regenerate the option or it is wiped.

### 2. TIMER + AND without short-circuit

- `SELF IS startup AND TIMER >= N` kept scheduling timer events after leaving
  `startup`/`preop`.
- Fix: `scheduleTimerEvents` skips TIMER sides when a non-timer AND conjunct is
  already false.

### 3. setNeedsCheck / scheduler storms

- Debounce when already queued; avoid busy-spin on tiny TIMER waits; purge
  inert runnable entries.
- DISABLE must clear unfinished actions/mail or SetStateAction stays New forever.

### 4. Plugin `setIntValue` every idle pass

- `setIntValue` → `setValue` → `setNeedsCheck` → stableQ.
- Mirroring `Pressure.IOTIME` / raw every plugin period requeues the plugin
  machine forever.
- Pattern: control path write-on-change; diagnostics rare or demand-only.
- Other plant plugins (range/over/position) mostly `changeState` — lower risk.

### 5. ANALOG/COUNTER in the CW work queue

- `regular_polls` devices (ANALOGINPUT, COUNTER) were enqueued on every domain
  bit change → full outer loops at domain/poll rate with empty stableQ.
- Fix: refresh address for filters; **do not** enqueue for `handleChange` CW work.
- **POINT / DIGITALVALUE must stay on the event path**
  (`on_enter`/`off_enter`, `DigitalValue::filter` → `setValue` when VALUE changes).
- Hardening: skip regular-poll enqueue only if `regular_polls && bitlen > 1`.

### 6. Wait loop vs usleep (latency vs CPU)

- End-of-loop `usleep` cuts `loops/s` but **delays** EC / scheduler / commands
  that already arrived on ZMQ.
- Correct: rate-limit with **interruptible `zmq_poll`**, not silent usleep after work.
- Empty EC frames: service in-wait (process + `go`); break only for real IO /
  timers / cmd / channels.
- Plant-quiet: stretch ecat→CW pull toward ~5 ms; restore `POLLING_DELAY` when
  work appears.

### 7. Bus vs poll rate (do not run 1:1)

- `CYCLE_DELAY` = EtherCAT bus period (kernel RT).
- `POLLING_DELAY` = CW process-data pull / plugin cadence.
- **1:1 periods** → free-running phase; worst-case lag ≈ **~2 periods** + jitter.
- Prefer bus faster than pull (e.g. 250 µs bus / 2 ms poll = 4 kHz / 500 Hz).
- Faster bus always costs more RT + ecat userspace CPU even if CW is quiet.

### 8. Diagnostics accidentally always on

- `PROCSNAP` and `MEMSNAPSHOT` are gated (default **off**):

  ```text
  DEBUG DEBUG_PROCSNAP on|off
  DEBUG DEBUG_MEMSNAPSHOT on|off
  ```

- `iod.conf` (`-c`) **only enables** tokens: any bare `DEBUG_PROCSNAP` line
  **turns it on**. There is no `off` in that file format.
- Wrong:

  ```text
  DEBUG DEBUG_PROCSNAP off    # enables DEBUG_PROCSNAP!
  ```

- Right: comment out, or omit:

  ```text
  #DEBUG_PROCSNAP
  #DEBUG_MEMSNAPSHOT
  ```

- `iosh QUIT` / `SHUTDOWN` stop the **whole** iod process, not just the shell.
  Use Ctrl-D to leave iosh on a live plant.

## Live-machine checklist

1. Confirm generation, operational state, and restart approval (`LLM_CONTEXT.md`).
2. Run `./machine/scripts/check.sh` once; inspect dependency graph if behaviour
   involves shared lists/outputs.
3. CPU split: `ps -eLo pid,tid,pcpu,comm | awk '$1==<iod_pid>'`  
   - `iod processing` vs `iod ethercat` vs `iod ecat timer`.
4. Enable snapshots only when needed:

   ```text
   DEBUG DEBUG_PROCSNAP on
   ```

5. Idle healthy: low `loops/s`, mostly empty exec/mail/stable; occasional real
   machines (panel, scales) OK.
6. Bad idle: high `loops/s` + empty queues → recheck §4–§6 (plugins, analogs,
   empty-EC outer loop).
7. Startup stuck / M_Startup thrash → §1–§2 (`L_Globals` option, TIMER AND).
8. Sticky after DISABLE → §3 (action/mail clear).
9. `CYCLE_DELAY` vs `POLLING_DELAY` in `generic_startup.lpc` / `DESCRIBE SYSTEM`.
10. Confirm `iod.conf` is not enabling DEBUG groups by accident.
11. DIGITALVALUE / POINT: change must still event (`setValue` / on_enter). Do not
    put them on `regular_polls`.

## Related docs

- `CW_RULES.md` — LIST `propagate_member_checks` rule.
- `TRANSPORT.md` (repo root) — kernel elc transport notes.
- `generic_servo_pump_pressure_control.md` — pump demand handoff / bypass timing.
- `IOD_TIMER_SOFT_CLOCKS_AND_COMMANDCLOCK_20260812.md` — TIMER soft-clock
  design limits, Option 1 overdue recovery, COMMANDCLOCK vs whole-loop stalls.
- `../2G4C/PIDLISTCLOCK_TIMER_STALL_20260805.md` — plant soft-clock stall notes.

## Open / re-validate on powered plant

- Quiet EC pull (~5 ms) when idle: measure request→target and stop paths.
- 4 kHz bus CPU budget vs 2 kHz.
- Residual plugins / channels under production HMI load.
- Any remaining “abandoned branch” delays under simultaneous motion + EC noise.

## Proposed diagnostic: processing stall trace (2026-08-12)

### Evidence from 2G4C-120

The Grab conveyor produced two late stops with the same whole-processing-gap
signature.  During the later event at `2026-08-12 14:56:10 AEST`:

- `C_GrabConveyor.StopPosition` was 5,855,128 pulses
- the last processed position before the gap was 5,752,820 pulses
- no control/PID updates were processed for approximately 1.39 seconds
- processing resumed at position 6,929,251 pulses
- the final overshoot was 1,074,123 pulses, approximately 553.6 mm

The EtherCAT/input side continued to observe the physical motion.  Once the
processing loop resumed, it consumed the new position, removed conveyor demand,
and raised `LoadBaleError`.  This is not normal stopping variation and cannot be
diagnosed reliably by another Clockwork TIMER running in the same processing
loop.

An attempted core change that immediately rechecked every overdue TIMER was
reverted.  It allowed false overdue predicates to requeue themselves repeatedly
after each evaluation, increasing processing load.  Revert commit on
`prod-experimental-mqtt-fix`: `7ada2a6d`.

### Recommendation

Add an opt-in **diagnostic processing stall trace** to iod.  It is not a control
or safety watchdog: it must never stop outputs, restart iod, or change CW state.
Its only purpose is to identify where the processing thread spent or lost the
missing time.

Use an independent, low-priority observer thread.  Instrument the processing
thread with only bounded, relaxed-atomic breadcrumb writes:

- monotonic heartbeat timestamp and sequence
- current processing stage
- current machine/action identifier where available
- entry timestamp for the current stage
- fixed-size ring of the most recent stage transitions

The processing hot path must perform no logging, allocation, formatting, file
access, socket access, mutex locking, sampler publication, or synchronous
notification for this diagnostic.

When the heartbeat is unchanged beyond an initial threshold (suggest 100 ms),
the observer should capture a bounded snapshot.  After processing recovers, it
may emit one rate-limited `STALLSNAP` record containing:

- detected start, recovery time, and duration
- last processing stage and machine/action breadcrumb
- whether the processing thread was on-CPU or sleeping/blocked, if obtainable
  without stopping or signalling that thread
- pre-captured runnable/stable/action/mail/event counts
- recent fixed-ring stage transitions
- count of suppressed/repeated stalls

Do not walk machine containers, inspect mutable queues, allocate strings, take
processing locks, or produce a stack trace asynchronously from the observer.
Those operations can race with the control thread or perturb it.  Any desired
queue counts must be published by the processing thread as atomic scalar
breadcrumbs during its normal passes.

### Suggested stage coverage

At minimum distinguish:

1. outer-loop housekeeping and incoming package drain
2. ZMQ poll/wait
3. EtherCAT/process-data receive and `handleChange`
4. plugin state checks
5. channel and command handling
6. scheduler handshake
7. runnable machine command/action polling
8. stable-state evaluation, including the current machine
9. output construction and EtherCAT output delivery

This should reveal whether a future gap is a long-running CW machine/action,
queue drain, plugin/channel call, scheduler/output block, or an OS/off-CPU
pause.

### Acceptance criteria before plant deployment

- disabled by default and explicitly enabled for the investigation window
- no functional change to scheduling, TIMER semantics, output handling, or CW
  publication
- hot-path overhead measured offline and limited to relaxed atomic stores
- fixed memory use; no unbounded event or log growth
- one bounded recovery record per stall, with rate limiting
- offline injected-delay tests identify every instrumented stage correctly
- build and deployment follow the plant approval and rollback procedure

`COMMANDCLOCK` is not a substitute for this diagnostic.  It executes through
the same iod processing path and would also pause during a whole-loop delay.
Consider clock migration only after the trace identifies and the code fixes the
underlying stall.

## Multi-domain (servo power isolation) — spike 2026-07-26

Kernel elc API ≥0.12: explicit domains isolate WC/validity. Plant layout:

- `domain 1` (io): Beckhoff positions 0–28  
- `domain 2` (drives): ED3L pumps positions 29–33  

Topology: `iod/configs/elc_topology.conf` already declares these.
On activate, look for:

```text
ELC topology loaded ... explicit_domains=2
ECDOMAIN io id=1 data_valid=... wc=...
ECDOMAIN drives id=2 data_valid=... wc=...
```

**Power-off test (Stage 4) — proven elc + CW 2026-07-27:** remove **servo
control power** only → domain 2 incomplete/invalid; domain 1 stays
`data_valid` / complete WC; restore recovers without transport restart.
elc: `/opt/etherlab-cyclic-kmod/docs/testing.md` (*Live domain bus firewall*).
CW: after `ECDomain_*` status fix, plant re-prove logged
`ECDomain_2 COMPLETE -> INCOMPLETE` (`faults=0x20`) and restore to COMPLETE.
Details: `IOD_ELC_OPEN_WORK_20260726.md` Stage 4.

Aggregate `bus_healthy` vs primary-only `all_ok` and per-domain arm policy
remain plant-policy follow-ups (Stage 2 full CW prove-out).
