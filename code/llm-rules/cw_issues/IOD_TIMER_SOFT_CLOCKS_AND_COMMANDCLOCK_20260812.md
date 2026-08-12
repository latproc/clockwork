# IOD TIMER, soft clocks, and COMMANDCLOCK (2026-08-12)

Portable design notes from 2G4C-120 Grab conveyor late-stop investigation and
follow-on architecture discussion. Use when soft clocks freeze, `calcAdjust`
gaps cause motion overshoot, or someone proposes TIMER vs COMMANDCLOCK vs
STALLSNAP fixes.

This file is **not** authorization to edit, build, deploy, or restart a plant.

## Related docs (read with this)

| Doc | Role |
|-----|------|
| `IOD_PROCESSING_LOAD_AND_IDLE_STORMS_20260725.md` | Idle/load storms; live checklist; STALLSNAP proposal; 2G4C-120 overshoot evidence |
| `../2G4C/PIDLISTCLOCK_TIMER_STALL_20260805.md` | Plant soft-clock stall; LPC re-arm; overdue TIMER core history |
| `../IO_NOTIFY_COMMANDCLOCK_DESIGN.md` | elc-path COMMANDCLOCK + silent IO notify design |
| `../CW_RULES.md` | LIST `propagate_member_checks`; elc ANALOGINPUT/COUNTER notify scope |
| `/opt/latproc/iod/IDLE_CPU_FIXES.md` | Processing idle pacing (legacy iod_sdo) |

## Symptom classes (do not collapse them)

| Class | What fails | Typical look |
|-------|------------|--------------|
| **A. Soft-clock freeze** | TIMER / PIDLISTCLOCK stop generating edges | No `calcAdjust`; last valve demand holds; encoder still moves in process image; often resumes on an unrelated digital edge |
| **B. Whole-loop gap** | Processing thread busy or blocked | No CW work of any kind for hundreds of ms–seconds; COMMANDCLOCK would pause too |
| **C. Idle / load storm** | Loop thrashing with empty or residual queues | High CPU / `loops/s`; LIST cascade, plugins, bad DEBUG, etc. |

Class A and B can both look like “control lost position updates.” Class C is the
opposite (too busy). Fixes for C do not fix A/B by themselves.

## Thread model (legacy iod / this plant class)

There is **no** dedicated “RT Clockwork” thread that runs PID / `calcAdjust`.

| Thread | Role | Runs plant LPC / calcAdjust? |
|--------|------|------------------------------|
| **Processing** | Outer loop: EC pull, stable states, actions, mail, plugins, outputs | **Yes — this is CW** |
| **EtherCAT** | Bus cycle, process data | No CW rules |
| **Scheduler** | TIMER queue; handshake with processing; fire triggers | Wakes processing only |

Soft-clock path:

```text
scheduleTimerEvents → Scheduler sleep/due → ZMQ handshake with processing
  → Trigger::fire → setNeedsCheck → setStableState → ENTER → SEND calcAdjust
```

COMMANDCLOCK (elc design) path:

```text
handle_io_sampling → dispatchCommandClocks(now)
  → CommandClock::due(mono, notify_period, Guard)
  → notifyCommandConsumers("calcAdjust") → mail/actions on processing
```

Both **execute** control on the processing thread. COMMANDCLOCK only changes how
the **tick is generated**.

## Why Grab conveyor control is sensitive

Plant pattern (legacy soft clocks):

```text
C_ClockFreq / C_ClockPosition  (PIDLISTCLOCK, TIMER on/off)
  → SEND calcAdjust → velocity / dual-position PID → valve demand

IA_GrabConveyorPos (COUNTER on regular_polls)
  → silent VALUE/Position refresh (no CW wake by design — idle CPU)
```

Stop / at-position logic updates `Process` / PID mainly inside **`calcAdjust`**,
not on every encoder sample. If soft clocks die, demand freezes while physical
position advances → late stop / overshoot / `LoadBaleError`.

Evidence pattern (2G4C-120, 2026-08-12): multi-hundred-ms to ~1 s gap in
control-processed position with EtherCAT still observing motion. Same-loop CW
TIMERs / COMMANDCLOCK cannot diagnose a true whole-loop stall (class B).

## TIMER design problems (when used as a control clock)

CW `TIMER` is **state dwell**. It is a good fit for debounce, startup dwell, and
timeouts. Soft clocks (`PIDLISTCLOCK`) stretch it into a **periodic control
source**. That creates structural fragility:

1. **Cadence coupled to the full processing loop**  
   Busy stableQ, channels, plugins, or a blocked handshake delay all soft clocks.

2. **Next wake is a side-effect of predicate evaluation**  
   Lost or dropped arm → silence until an unrelated event re-enters
   `setStableState` (e.g. bale-leaving digital).

3. **False rules vs matched rules need opposite overdue policy**  
   `setStableState` calls `scheduleTimerEvents` on **false** TIMER rules
   (speculative “when could this become true?”) and on the **matched** hold.
   - False `TIMER < N` when TIMER is already past: `t ≪ 0`.  
   - If that path always `setNeedsCheck()`, the machine re-queues every
     evaluation → **load storm** (especially after mass enable).  
   - If overdue is dropped when `t < -2000` µs (legacy), late checks on the
     **matched** hold can fail to re-arm → **stuck clock** (class A).

4. **Scheduler ↔ processing handshake**  
   Scheduler blocks waiting for processing to complete the handshake. TIMER
   delivery latency tracks processing busy time.

5. **No first-class deadline**  
   No built-in “no calcAdjust for N ms while seeking → hold/fault.” Failures
   show up as plant overshoot, not a TIMER fault channel.

**Not** the same problem as LIST cascade, plugin `setIntValue` thrash, or
accidental `DEBUG_PROCSNAP` (class C) — those are separate; see idle-storms
handoff.

## LPC soft-clock shape (PIDLISTCLOCK)

Old OR-clause hold could leave rising-edge arming on a **false** rule path; a
lost wake left `on`/`off` with free-running TIMER and no further ENTER edges.

Plant fix (2G4C, 2026-08-05): explicit due vs hold rules so **both** dwells arm
TIMER from the **matching** stable-state path; `ENTER on` / `ENTER off` still
send `calcAdjust`. See `lib/generic_pid.lpc` and
`../2G4C/PIDLISTCLOCK_TIMER_STALL_20260805.md`.

LPC alone is **not** enough if iod drops overdue re-arm or processing never
re-checks the machine.

## Option 1: load-safe overdue recovery (deployed 2026-08-12)

### Intent

Recover **matched-hold** overdue TIMER checks without re-activating machines
from **false-rule** overdue clauses.

### API

```text
enum class TimerOverduePolicy {
  ArmFutureOnly,   // false rules: t > 0 schedule only; no setNeedsCheck
  RecoverOverdue   // matched hold: t <= 0 → setNeedsCheck (coalesced if queued)
};
```

`Predicate::scheduleTimerEvents(..., policy)` default **ArmFutureOnly**.

### Call sites

| Site | Policy |
|------|--------|
| `setStableState` matched hold + subconditions | `RecoverOverdue` |
| `setStableState` false TIMER rules while scanning | `ArmFutureOnly` (default) |
| `SetStateAction` arming timers for new stable state | `RecoverOverdue` |

### Code

- `iod/src/Expression.h` / `Expression.cpp`
- `iod/src/MachineInstance.cpp` (`setStableState`, `setNeedsCheck` comments)
- `iod/src/SetStateAction.cpp`

### History

| Commit / change | Effect |
|-----------------|--------|
| Always recheck every overdue (`6c8f6545` lineage) | Helped soft clocks; risk of enable/load storm |
| Revert (`7ada2a6d`) | Restored `t >= -2000` only drop |
| Option 1 (this note) | Recover only on matched hold; false rules future-only |

### What Option 1 fixes / does not fix

| Fixes | Does not fix |
|-------|----------------|
| Class A when a check runs on a matched hold already past due | Pure lost wake with **zero** later evaluation |
| False-rule overdue storms from blanket setNeedsCheck | Class B whole-loop processing stalls |
| | Need for COMMANDCLOCK as structural control tick |
| | Idle/load storms (class C) |

**Plant status (2G4C-120):** Option 1 built and running as of 2026-08-12
operator report. Watch LoadBale late-stops and enable-time HEALTH for regression.

## COMMANDCLOCK (elc) — intended reliable calcAdjust tick

### Design intent

Prove **`calcAdjust` on a fixed period** without CW TIMER soft clocks:

- iod owns cadence: monotonic `CommandClock::due` + `notify_period`
- Guard on → always send `command` (default `calcAdjust`) when a new period slot
  is due
- No List bag; consumers list the clock and declare `COMMAND calcAdjust`
- Phase-aligned; **no burst catch-up** after disable or long stall

Documented in `../IO_NOTIFY_COMMANDCLOCK_DESIGN.md`. Runtime scope:
**iod-elc / `feature/iod-elc-kernel-transport`** (not legacy `iod_sdo` unless
ported).

### elc implementation sketch (branch)

```text
handle_io_sampling()
  → regular_polls filter / silent VALUE
  → MachineInstance::dispatchCommandClocks(now)
       → for each COMMANDCLOCK: due(now, notify_period, enabled)?
       → notifyCommandConsumers(command)
            → sendMessageToReceiver (skip if hasPending)
```

### TIMER delay issue vs processing delay

| Concern | elc COMMANDCLOCK |
|---------|------------------|
| TIMER arm / overdue drop / soft-clock stuck (class A) | **Not affected** — tick not TIMER-armed |
| calcAdjust **execution** delayed under processing load | **Still possible** — same processing path |
| Silent during class B whole-loop stall | **Still possible** — sampling/dispatch not running |
| Separate RT CW thread | **No** |

Correct summary:

> COMMANDCLOCK is not affected by the TIMER soft-clock delay issue; processing
> of the issued commands can still be delayed.

### This plant (2G4C-120 legacy path)

- Live motion still uses **PIDLISTCLOCK** + Option 1 (as of this note).
- No COMMANDCLOCK instances in plant LPC on the legacy path; migration is a
  separate elc + plant project under approval.

## STALLSNAP (proposed diagnostic)

From `IOD_PROCESSING_LOAD_AND_IDLE_STORMS_20260725.md`:

- Opt-in independent observer of processing **heartbeat / stage breadcrumbs**
- **Not** a safety watchdog; must not stop outputs or change CW state
- Needed for **class B** location (where the loop spent time)
- **Not** a substitute for COMMANDCLOCK; COMMANDCLOCK is not a substitute for
  STALLSNAP

At a few events per day, external processing-TID stack sampling is a valid
first capture before shipping STALLSNAP.

## Design ladder (preferred order of thinking)

```text
1. TIMER used only for dwell/timeouts (good original role)
2. PIDLISTCLOCK soft period (fragile) ← legacy plant motion today
3. Option 1 load-safe overdue recovery ← mitigation on (2), running 2G4C-120
4. COMMANDCLOCK + silent IO notify (elc) ← prove calcAdjust without TIMER
5. Independent motion deadline / fail-safe
     (seeking && no tick for N ms → hold/zero demand)
     — bounds damage under class B; not full PID isolation
6. Optional light control path off heavy stableQ
     (bounded sample→tick→output; full CW can lag)
7. STALLSNAP / external capture when class B remains after (4)–(5)
```

Do **not**:

- Slow motion clocks to make `SHOW HEALTH` look quieter
- Put control logic inside STALLSNAP
- Treat idle-storm cherry-picks as the fix for soft-clock freezes
- Assume COMMANDCLOCK alone prevents whole-loop overshoot

## Open / re-validate

- [ ] 2G4C-120: post–Option 1 late-stop rate vs prior “few per day”
- [ ] 2G4C-120: enable / idle HEALTH not regressed by RecoverOverdue
- [ ] Whether residual stops are class A (clocks) vs class B (whole loop)
- [ ] elc COMMANDCLOCK migration plan for Grab motion clocks (separate project)
- [ ] Motion deadline fail-safe design if class B remains after COMMANDCLOCK

## Change log

| Date | Note |
|------|------|
| 2026-08-12 | Initial handoff from investigation + architecture discussion; Option 1 running on 2G4C-120 |
