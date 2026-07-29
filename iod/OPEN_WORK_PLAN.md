# Open work plan (iod / Clockwork)

**Updated:** 2026-07-29  
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
| cJSON `Value::getFromJSON` scalar clone free | `31fceba5` (built Release; deploy when restart allows) |
| IOUpdate mask ownership | `6eaac1b8` |
| Overnight flat MEMSNAPSHOT after ITEM fix | PID 3342133 22.7 h — idle paths OK; day growth open |

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
| JSON ITEM DEFAULT + PutSubExpr | Done (`b985908f`); **night flat on plant** |
| `Value::getFromJSON` scalar clones | Done in tree (`31fceba5`); deploy on next restart |
| Production-day live cJSON + WEBREQUEST arenas | **Open** — offline VM work |
| Methodology + plant evidence | `MEMORY_LEAK_INVESTIGATION.md`, `llm-rules/cw_issues/IOD_WEBREQUEST_*` |

**Plant evidence (do not re-prove idle on controller):**

- PID `3342133` ~22.7 h: cjson/malloc **flat overnight**; day climb to ~1.8M
  nodes / ~311 MiB in_use / RSS ~334 MiB.
- Main heap mapping flat; worker arenas + live cJSON drive production growth.

**Local staged binaries on 2G-120:**  
`iod_sdo.prev-memfix-*`, `iod_sdo.staged-json-ownership-fix` — confirm
`svstat` / running path before treating as production.

**Offline next (no EtherCAT / no 2G4C required):**

1. Thread pool or same-thread free for `exec_web_request.c` workers.
2. CW: clear large `result` / `curl.Result` after field extract (warehouse LPC).
3. Optional: `apply()` via `cJSON_Duplicate` instead of Print+Parse.
4. Catalog poll rate review (`P_BaleCatalogForAssignment`).

---

## Suggested next sequence

```
Plant (only when restarting anyway)
1  Deploy Release with 31fceba5 if not already; re-enable DEBUG_MEMSNAPSHOT
2  Do not chase overnight drip — already flat after b985908f

Offline / other machine (preferred for remaining memory work)
3  Reproduce catalog WEBREQUEST load on Linux VM
4  Implement WEBREQUEST thread pool + Result ownership tests
5  Warehouse LPC Result clear / poll reduce as separate CW change
6  Optional: SHOW HEALTH quiet vs auto after channel clients settled
```

---

## Non-goals (for now)

- Continuous thrash sampling in the CW loop (on-demand only).
- iocmd thrash-aware protocol changes.
- Blind full cherry-pick of all mqtt idle commits onto elc without review.
- Pre-start of panel CHANNEL publishers for fixed ports.
