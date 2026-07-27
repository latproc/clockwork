# Legacy output pending / SetState review (look at later)

**Date:** 2026-07-28  
**Branch:** `prod-experimental-mqtt-fix`  
**Binary:** legacy ecrt path `iod_sdo` (not kernel EtherCAT)  
**Plant:** Grab / Core dual motor start — softstart SetState hung “starting”  
**Status:** Deployed and working on plant. This note is for later cleanup, not a fire drill.

**Key commit:** `7e062d0c` — *Fix legacy output turnOn/turnOff and pending-out completion under quiet EC loop.*  
**Companion earlier:** `97c399a8` / `106da577` — *io: clear pending outputs when domain has no input changes* (`processAll` + `pending_value` on turnOn/turnOff).

Related docs: `IDLE_CPU_FIXES.md`, `OPEN_WORK_PLAN.md`.

---

## 1. Symptom that drove the fix

- Clockwork softstart / digital and analog outs stayed in **SetState Running**
  (POINT stuck, motors never finished starting).
- Idle-CPU work had made the processing path quieter (5 ms quiet out pace,
  absorb when no urgency). Pending outs no longer got built/sent/cleared in
  time, and one cleanup path **dropped** real pending work.

Plant outcome after `7e062d0c`: dual start completes (`P_StartBothMotors`
→ Running, both motors on).

---

## 2. What changed (three layers)

| Layer | File | Change |
|-------|------|--------|
| **Root write path** | `IOComponent.cpp` `Output::turnOn` / `turnOff` | Set `last_event` **before** `markChange()` |
| **Pending queue** | `ProcessingThread.cpp` | Do **not** `clearPendingOutputUpdates()` when `getUpdates()` is null |
| **Service cadence** | `ProcessingThread.cpp` | Service pending outs at **bus period** (`get_cycle_time()`, min 1 ms), not ≥5 ms quiet pace |
| **Action completion** | `SetStateAction.cpp` | Treat `turning_on` / `turning_off` as match for commanded on/off so CW does not wait for domain echo |
| **Prior companion** | `IOComponent.cpp` `processAll` | No early return when no input changes; clear out-pending when `updates_sent && pending_value == address.value`; set `pending_value` in turnOn/turnOff |

Kernel path (`USE_KERNEL_ETHERCAT`) already applies bits immediately and
clears pending differently — these notes are mainly about **legacy ecrt**.

---

## 3. Root cause detail (`last_event` vs `markChange`)

`markChange()` for 1-bit outs only writes the update-image bit when
`last_event` is already `e_on` or `e_off`:

```text
if (!value && last_event == e_on)  → set bit 1
if (value  && last_event == e_off) → set bit 0
then last_event = e_none   // always for bitlen == 1
```

**Bug:** `last_event = e_on` *after* `markChange()`  
→ mask could mark dirty, **data bit stayed 0**  
→ bus never got the commanded edge  
→ `getStateString()` stayed `"turning_on"`  
→ SetState Running forever.

**Fix:** set `last_event` first, then `markChange()`. That is the real root
fix. Keep it; do not reorder again.

After a correct turnOn, `markChange` clears `last_event`, and
`getStateString()` returns `"on"` via `address.value == 1`. So
`"turning_on"` is usually **not** observed after turnOn returns.

---

## 4. Optimality assessment (as of 2026-07-28)

### Ship / leave as-is

- **Correct for production** under quiet EC + idle-CPU processing.
- Small diffs, legacy-path scoped, validated on Grab/Core motors.
- Three layers complement each other; removing only one can re-hang softstart.

### Not perfectly layered (tidy later)

1. **Three partial fixes for one pipeline**  
   Write update image → send → clear pending → complete action. Today each
   stage has a band-aid; ideal is one clear ownership story.

2. **`SetStateAction` matching `turning_on` / `turning_off`**  
   Mostly **redundant** if turnOn order is always correct (markChange clears
   last_event; state is already on/off via `address.value`).  
   Harmless defense-in-depth. Completes CW on **command accepted**, not on
   “bit seen on wire.”

3. **Never clear pending on null `getUpdates()`**  
   Right vs the false-clear bug (dropped real work).  
   Risk if mask/update build stays null for other reasons:
   `updatesWaiting()` sticky → processing stays “urgent.”  
   Better long-term: clear only when safe, or timeout + log, not drop
   silently and not spin forever.

4. **Out service at bus period**  
   Good tradeoff for plant. If CPU ever regresses, keep gate on
   `updatesWaiting()` and consider a 1–2 ms floor rather than returning to
   5 ms quiet while outs are pending.

---

## 5. Optional cleanup backlog (when not under plant pressure)

Priority is low while machines are healthy.

### A. Hard invariant on digital turnOn/turnOff (legacy)

- Keep `last_event` before `markChange` as the documented contract.
- Optional debug assert: after `markChange()`, update-image bit matches
  commanded level when `last_event` was e_on/e_off.

### B. Pending-out ownership

Replace “never clear on null getUpdates” with explicit rules:

1. Clear when `updates_sent && pending_value == address.value` (`processAll` — already).
2. If `getUpdates()` is null:
   - if queue empty / no real outs → idle;
   - if pending remains → retry next bus period;
   - if pending age &gt; N ms → **log once** (MessageLog), then decide:
     clear + fail soft, or keep retrying.
3. Never silently discard pending that still needs a wire update.

### C. Simplify SetStateAction IO completion

Prefer completing after turnOn/turnOff when **commanded value already
matches** (`address.value` / known on-off), without string-matching
`turning_*`. Keep intermediate-string match only if a path still leaves
`last_event` set without calling `markChange`.

### D. Align with idle-CPU design

- Digital ASAP when `updatesWaiting()` (already intended).
- Analog quiet pace only when **no** pending outs and hardware operational.
- Re-measure with `SHOW PROCSNAP` / `SHOW CYCLING` after any cadence change
  (`IDLE_CPU_FIXES.md`).

### E. Port notes for elc / kernel transport

- Kernel path should not need last_event/markChange ordering for apply.
- Still ensure pending-out / `updatesWaiting()` cannot sticky-spin.
- When porting mqtt-fix idle work to elc, re-check this file vs
  `OPEN_WORK_PLAN.md` Track B so quiet absorb does not reintroduce
  SetState hangs.

---

## 6. Quick verification checklist (regression)

On Grab (or any legacy `iod_sdo` plant):

```text
# After start motors
GET P_StartBothMotors;          # expect Running (or plant-specific)
GET M_GrabMotor; GET M_CoreMotor;  # on
# During a forced digital out SetState, action should leave Running quickly
SHOW PROCSNAP;
SHOW CYCLING;
```

Failure signatures that mean this area regressed:

- Softstart / POINT stuck in SetState Running while hardware already on/off.
- `updatesWaiting()` true forever with no outs actually changing.
- High `brk_out` with empty runnable queue after a single turnOn (sticky pending).

---

## 7. Verdict snapshot

| Question | Answer |
|----------|--------|
| Safe to leave on plant? | **Yes** |
| Optimal design? | **Good enough; not final architecture** |
| Must-fix later? | No — optional hardening only |
| Must never regress? | `last_event` before `markChange`; do not drop real pending outs under quiet EC |

---

## 8. File / symbol map

| Symbol / area | Location |
|---------------|----------|
| `Output::turnOn` / `turnOff` | `iod/src/IOComponent.cpp` |
| `IOComponent::markChange` | `iod/src/IOComponent.cpp` |
| `IOComponent::getStateString` | `iod/src/IOComponent.cpp` |
| `IOComponent::getUpdates` / `clearPendingOutputUpdates` | `iod/src/IOComponent.cpp` |
| `IOComponent::processAll` pending-out cleanup | `iod/src/IOComponent.cpp` |
| Out service + null-getUpdates path | `iod/src/ProcessingThread.cpp` |
| SetState on/off + `checkComplete` | `iod/src/SetStateAction.cpp` |
| `get_cycle_time` / `get_polling_time` | `iod/src/options.cpp` / `options.h` |
