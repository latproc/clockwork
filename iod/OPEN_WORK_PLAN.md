# Open work plan (iod / Clockwork)

**Updated:** 2026-07-28  
**This machine / branch:** `2G-120` · `prod-experimental-mqtt-fix` (legacy `iod` / `iod_sdo`)  
**Related:** `feature/iod-elc-kernel-transport` (elc — dual-domain, analog emit, plant C/D)

Multi-session context so thrash/PROCSNAP, idle CPU, memory ownership, channels,
and cross-branch port work do not get lost.

---

## Done recently (mqtt-fix)

| Item | Notes |
|------|--------|
| SHOW CYCLING / HEALTH / peak PROCSNAP | Track A — done (`c4b183e1`, merge `804c4ede`) |
| Idle CPU: ecat throttle, no usleep, pending-out clear, urgency tiers | See `IDLE_CPU_FIXES.md` |
| Dig ASAP + analog quiet pull (5 ms) + sched floor 2× CYCLE_DELAY | `53fd85dd` series, `be7aa2cf`, `989240af` |
| Legacy turnOn/turnOff + bus-rate out service + SetState turning_* | `7e062d0c` |
| Channel client: timeout underflow, REQ reset, handshake thrash | `388b9ee4` … `dda16ce0` |
| cJSON ITEM DEFAULT + PutSubExpr ownership | `b985908f` (see `MEMORY_LEAK_INVESTIGATION.md`) |
| IOUpdate mask ownership | `6eaac1b8` |

Details and measurement: `iod/IDLE_CPU_FIXES.md`, `iod/MEMORY_LEAK_INVESTIGATION.md`.

---

## Track A — Port thrash + PROCSNAP + HEALTH → mqtt-fix

**Status:** Done.

---

## Track B — Idle CPU fixes

**On mqtt-fix:** Done (including dig-ASAP, 5 ms quiet pull, out-service refine).  
**On elc:** Done (hand-merge earlier; see elc `OPEN_WORK_PLAN` history).

**Ops note:** Live plant can still show **LOAD BUSY ~30–60 loops/s** under
HMI/sampler activity; re-measure quiet vs auto after channel/HMI settle.

---

## Track C / D — Analog emit + plant LPC (primarily elc + plant WC)

**Status on elc / 1G2C-style plant path:** Done for that plant (iod owns AI/COUNTER
emit; soft-clock lists removed; A_* thin RECEIVE). Not the active mqtt-fix
legacy focus unless ported.

**Residual (plant, not C code):** oil physical wiring; full eject-at-pressure when
outputs enabled — see elc plan if working that site.

---

## Track E — Channel clients / HMI (CW2CW)

**Intent:** Clients reconnect CHANNEL setup without process restart. No panel
channel pre-start — data port from CHANNEL reply.

### Done on mqtt-fix (this branch tip)

| Theme | Commits (representative) |
|-------|---------------------------|
| Timeout elapsed-time underflow | `388b9ee4` |
| REQ reset after timeout/disconnect | `a315c815` |
| Handshake status latch / WAITSTART | `08eb69ab` |
| Always poll router/CTRL during handshake | `501fa881` |
| Stop status-retry thrash during UPLOADING | `6cb346a1` |
| Safe setup-REQ recreate (no monitor hang) | `81a79e01` |
| Stop setup-REQ recreate storm while reconnecting | `dda16ce0` |

### Still open / ops

| Item | Notes |
|------|--------|
| Deploy channel-fixed binaries to all clients (humid, etc.) | Sticky REQ until kill if old client |
| Optional server bind harden | On `EADDRINUSE`: uniquePort / error — **do not exit(1)**; port-clash review if needed |
| Quiet-load re-measure | After HMI/channel stable (`SHOW HEALTH`) |

---

## Track F — Memory / JSON ownership (mqtt-fix)

| Item | Status |
|------|--------|
| IOUpdate mask `owns_mask_` | Done (`6eaac1b8`) |
| JSON ITEM DEFAULT + PutSubExpr | Done in tree (`b985908f`); plant slope confirm after deploy |
| Methodology + traces | `MEMORY_LEAK_INVESTIGATION.md` + `sampling/iod-memory/` |

**Local staged binaries on 2G-120 (not necessarily live service):**  
`iod_sdo.prev-memfix-*`, `iod_sdo.staged-json-ownership-fix` — confirm
`svstat` / running path before treating as production.

---

## Suggested next sequence (mqtt-fix / 2G-120)

```
1  Confirm which iod_sdo binary the service runs (json + turnOn fixes)
2  Post-deploy MEMSNAPSHOT / memory.csv slope under HMI JSON load
3  SHOW HEALTH quiet vs auto after channel clients settled
4  Optional: server bind/exit(1) harden if port clashes appear
5  elc-only plant residuals only if working that branch/site
```

---

## Non-goals (for now)

- Continuous thrash sampling in the CW loop (on-demand only).
- iocmd thrash-aware protocol changes.
- Blind full cherry-pick of all mqtt idle commits onto elc without review.
- Pre-start of panel CHANNEL publishers for fixed ports.
