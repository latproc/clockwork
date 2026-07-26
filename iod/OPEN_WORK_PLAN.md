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

**Status:** Advancing — IO-level emit replaces CLOCKING list for non-critical IO:

**ANALOGINPUT / COUNTER emit policy**
1. **Startup** — one emit when first sample is ready  
2. **Change** — filter tick (`throttle`/`rate` ms, default 100) + tolerance; eng
   `VALUE = raw * factor + base`; change only if `|Δeng| > window` (or raw change
   if window=0)  
3. **Safety** — re-emit at least every `safety_emit` ms (default 1000) even if
   unchanged  
4. **Guard/flag** — `emit` 0/1 on settings, or machine `guard`/`emit_guard` in
   state `off`/`false` freezes emit (same idea as `G_CoreE24` on
   `M_ClockedAnalogInputs`)

Scale OPTIONs on ANALOGINPUT/COUNTER: `factor`, `base`, `window` (same as A_*
CLOCKED wrappers). Settings: `throttle`/`rate`, `safety_emit`, `emit`.

Plant can retire `L_ClockedAnalogInputs` / `M_ClockedAnalogInputs` once A_*
wrappers are simplified or scale is on the IA_* machines.

**Track D:** inventory done (8 CLOCKEDANALOGINPUT + 5 CLOCKEDCOUTER16BIT);
migration optional after plant validation.

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
