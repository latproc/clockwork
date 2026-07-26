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
| Urgency tiers + in-wait absorb (Track B remainder from d6312cc2) | elc (hand-merge) |

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

**Status:** Done on elc (hand-merge of d6312cc2 urgency tiers + in-wait absorb + PROCSNAP absorb/brk counters). Earlier: usleep, scheduler 10 ms, pending-out clear, keep-alive 50 ms. Plant smoke: dual domain op/COMPLETE, thrash 0, quiet loops/s single-digit.

---

## Track B2 — Digital ASAP vs analog pace (mqtt-fix refresh 2026-07-26)

**Source (mqtt-fix tip after `0ec8593c` / dig-ASAP series):**

| Commit | Theme |
|--------|--------|
| `4e2ce9dd` | `IOComponent::domainHasDigitalChange` |
| `b2cfaf36` | `ECInterface` dig peek + `copyDomainData` |
| `53fd85dd` | ecat: dig push every cycle; analog `pull_due` only |
| `fd2a1f97` | scheduler floor **2 ms** (was 10 ms) |
| `989240af` | stable pace **2 ms**; quiet pull **5 ms**; sched handshake cap **10 ms** |
| docs | `IDLE_CPU_FIXES.md` digital ASAP / ENABLE storm notes |

**Why:** POINTSSTARTUP sets `CYCLE_DELAY=1000`. Quiet-only pull made digitals lag
~quiet window. Dig edges must push every bus period; analog dither must **not**
force 1 kHz CW free-run.

**On elc (kernel multi-domain):**

1. Port `domainHasDigitalChange` (shared; uses `regular_polls` + bitlen).
2. Kernel-path peek via `domain1_pd` after `receiveState` snapshot (not ecrt
   `ecrt_domain_data`).
3. ecat_thread: always snapshot when collecting; `want_cw = dig_edge || pull_due
   || need_ping || first_run`; dig_shadow advances only on push.
4. Scheduler floor 2 ms; ProcessingThread stable 2 ms, quiet pull 5 ms,
   in-wait sched handshake ≤10 ms.
5. Plant: digital edge latency, thrash 0, dual domain COMPLETE, quiet loops/s.

**Risk:** Medium — more snapshots per bus period (CPU). Dig floods still event
ASAP (correct for end-stops); ENABLE storms stay machine-paced.

**Status:** Done on elc this session. Copied mqtt `IDLE_CPU_FIXES.md`. Ported dig
ASAP + analog pace (kernel `domain1_pd` peek), scheduler 2 ms, stable 2 ms,
quiet pull 5 ms, sched handshake ≤10 ms. Plant: dual domain op/COMPLETE,
thrash 0, quiet loops/s single-digit to ~30.

---

## Track C — Analog / COUNTER change emit (like POINT / DIGITALVALUE)

### Intent (end state)

We want analogs and counters to behave like digital IO at the architecture
level: the controller IO path is responsible for sampling, scaling, and
publishing value/IOTIME under rate and guard policy. Clockwork should not
maintain CLOCKING / CLOCKINGWITHENABLE lists that periodically SEND update to
eng wrappers only to re-derive what iod already knows. Eng factors may live on
the IO object or a thin consumer, but work is driven by iod emit + selective
RECEIVE, not by plant soft-clocks. CLOCKINGWITHENABLE and L_ClockedAnalogInputs
are transitional; the end state is no CW sampling clocks for AI/COUNTER.

In short: **IO publishes engineered analog/counter values at the right rate;
Clockwork stops running soft clocks that re-sample IO.**

**Today:**

- POINT / DIGITALVALUE: bit change → event / `io_work` path.
- ANALOG / COUNTER: `regular_polls` + `sampleRegularPolls` / filter; plant often uses
  **CLOCKEDANALOGINPUT** LPC machines + `CLOCKINGWITHENABLE` lists to push scaled
  values (transitional soft-clock).

**Target:**

1. iod owns sample / scale (`factor`/`base`/`window`) / throttle / guard emit policy.
2. ANALOGINPUT / COUNTER publish VALUE (and eng) on change like DIGITALVALUE, with
   tolerance so LSB noise does not storm CW.
3. IOTIME advances on the IO sample path without full machine storms.
4. Selective fan-out only: **SEND update** to machines that **RECEIVE update** —
   not full `notifyDependents`.
5. Retire plant soft-clocks: no `CLOCKING` / `CLOCKINGWITHENABLE` /
   `L_ClockedAnalogInputs` as the sampling engine for AI/COUNTER.

**Steps:** map paths → iod emit + scale → selective RECEIVE → pilot → remove CW
clock lists (Track D).

**Risk:** Medium–high if LSB noise storms CW — tolerance / window mandatory.

**Status (plant soft-clock retired for this plant):**

| Layer | Role |
|-------|------|
| **IA / COUNTER** | `factor`/`base`/`window` on the map; filter; int `VALUE` + always `ENG`; emit on change/window/safety; `notifyClockedUpdateConsumers()` → **SEND update** only to **RECEIVE update** (A_*). |
| **Plant soft-clock** | **Removed** `L_ClockedAnalogInputs` / `M_ClockedAnalogInputs` / `CLOCKINGWITHENABLE` sampling list. |
| **A_*** | Thin `CLOCKEDANALOGINPUT` / `CLOCKEDCOUTER16BIT`: `VALUE := IA.ENG` (or signed torque) on RECEIVE only. |
| **Plugins** | Live int pointers on IA — no message needed. |

Do **not** reintroduce soft-clock lists for AI/COUNTER or status words. Packed
multi-bit IO stays DIGITALVALUE at iod; CW consumes `VALUE`.

**Live verify:** restart iod; `DESCRIBE IA_CoreOilTemp` (VALUE, ENG, IOTIME);
`DESCRIBE A_CoreOilTemp` follows via RECEIVE; no `M_ClockedAnalogInputs`.

---

## Track D — Plant LPC cleanup

Soft-clock list removed for Core/Grab interest set. Remaining: drop unused
`CLOCKING*` helpers if nothing else needs them; confirm `INPUTONPRESSURE` SetPoint
against site; any other plants still on list clocks migrate the same way
(scale on IA, thin A_, no list timer).

**Status:** In progress (this plant sampling clocks removed).

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
