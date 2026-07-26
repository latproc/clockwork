# Open work plan (iod / Clockwork)

**Updated:** 2026-07-26  
**Branches:** `feature/iod-elc-kernel-transport` (elc), `prod-experimental-mqtt-fix` (legacy iod/iod_sdo)

This file keeps multi-session context so thrash/PROCSNAP, idle CPU, and analog
emit work does not get lost.

---

## Done recently

| Item | Where |
|------|--------|
| Multi-domain isolation + Martin safety fixes | elc `7fe6f81f` |
| `SHOW CYCLING` / `SHOW HEALTH` / peak `PROCSNAP`, iosh status line | elc `50900dc8` |
| `IDLE_CPU_FIXES.md` (measurement guide from mqtt-fix plant) | elc (also on mqtt-fix `f3c01bca`) |

---

## Track A — Port thrash + PROCSNAP + HEALTH → mqtt-fix

**Goal:** Same operator tools on legacy `iod` / `iod_sdo`.

1. Update local `prod-experimental-mqtt-fix` to `origin` tip.
2. Port from elc `50900dc8` (diagnostics only):
   - `MachineInstance` thrash analysis (`analyseStateThrash`)
   - `ProcessingThread` peak once-per-second snap
   - `IODCommands` + `ClientInterface` (`SHOW CYCLING` / `HEALTH` / `PROCSNAP`)
   - `iosh` startup status line under messages
3. Build legacy targets; run unit tests.
4. Smoke: `SHOW HEALTH;`, `SHOW PROCSNAP;`, `SHOW CYCLING;`
5. Push mqtt-fix.

**Risk:** Low (CW diagnostics only).

**Status:** Done on mqtt-fix (this session).

---

## Track B — Idle CPU fixes: mqtt-fix ↔ elc

**Source commits on mqtt-fix (after Martin tests):**

| Commit | Theme |
|--------|--------|
| `d8899dbe` | ecat: throttle CW domain push when idle |
| `6e273d52` | scheduler: ≥10 ms CW wake floor |
| `7ef79c42` | no usleep after empty poll |
| `41974251` | clear pending outs without input changes |
| `d6312cc2` | urgency tiers + in-wait absorb + PROCSNAP fields |
| `f3c01bca` | docs (`IDLE_CPU_FIXES.md`) |

**On elc:**

1. Diff carefully vs existing quiet pull / absorb (avoid double rate-limits).
2. Port scheduler 10 ms floor (shared).
3. Port pending-out clear if still stuck on kernel/shadow path.
4. Port urgency tiers only where elc still over-treats `updatesWaiting` / exec-only.
5. Align optional PROCSNAP `absorb`/`brk_*` fields; keep peak model from `50900dc8`.
6. Plant measure + digital edge check.

**Risk:** Medium on elc. Details: `iod/IDLE_CPU_FIXES.md`.

**Status:** Planned.

---

## Track C — Analog / COUNTER change emit (like POINT / DIGITALVALUE)

**Today:**

- POINT / DIGITALVALUE: bit change → event / `io_work` path.
- ANALOG / COUNTER: `regular_polls` + `sampleRegularPolls` / filter; plant often uses
  **CLOCKEDANALOGINPUT** LPC machines to push scaled values on IOTIME.

**Target:**

1. Internal CW/IO: when filtered value **changes** (existing tolerance), emit work
   like digital (activate owner / notify), not only property rewrite.
2. ANALOGINPUT / COUNTER publish VALUE on change like DIGITALVALUE.
3. IOTIME may still advance on sample schedule without full machine storms.
4. Idle CPU: no CW wake for unchanging analogs (keep domain-push throttle).

**Steps:** map paths → spec on-change + tolerance → implement → unit/pilot → plant.

**Risk:** Medium–high if LSB noise storms CW — tolerance mandatory.

**Status:** Planned (after A; ideally after B feed policy on elc).

---

## Track D — Plant LPC cleanup

After C is proven: inventory `CLOCKEDANALOGINPUT` in plant config; migrate where
hardware analogs emit on change; keep wrappers only where extra clocking needed.

**Status:** Blocked on C.

---

## Suggested sequence

```
A  Port thrash/PROCSNAP/HEALTH → mqtt-fix
B  Selective idle-CPU port → elc (measure)
C  Spec + implement analog emit
D  LPC cleanup
```

## Non-goals (for now)

- Continuous thrash sampling in the CW loop (on-demand only).
- iocmd thrash-aware protocol changes.
- Blind full cherry-pick of all mqtt idle commits onto elc without review.
