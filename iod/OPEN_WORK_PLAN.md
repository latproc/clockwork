# Open work plan (iod / Clockwork)

**Updated:** 2026-07-29  
**This machine / branch:** `2C-120` (hostname) · plant class `2G4C` / process `--name 2GRAB` · `prod-experimental-mqtt-fix` (legacy `iod` / `iod_sdo`)  
**Related:** `feature/iod-elc-kernel-transport` (elc — dual-domain, analog emit, plant C/D)

Multi-session context so thrash/PROCSNAP, idle CPU, memory ownership, channels,
and cross-branch port work do not get lost.

---

## Live plant snapshot (2026-07-29 ~15:05 AEST)

| Field | Value |
|-------|--------|
| Service | `/etc/service/iod` **up** PID **`127846`** since **2026-07-28 11:38:43** (~**27.4 h**) |
| Binary | `/opt/latproc/iod/iod_sdo` mtime **2026-07-28 10:35** (Release, debug symbols) |
| Memory now | RSS **~138 MiB**, VSZ ~1.53 GiB, main `[heap]` ~**59 MiB**, largest worker arena ~**27 MiB**, several ~7.5 MiB arenas |
| Load | `SHOW HEALTH`: **LOAD BUSY ~110–125 loops/s**, THRASH none (HMI/core-panel active) |
| In live binary | Idle-CPU series, dig ASAP / quiet pull, channels handshake, legacy turnOn/pending-out/`SetState` (`7e062d0c` objects) |
| **Not** in live binary | `b985908f` (ITEM DEFAULT / PutSubExpr — `.o` still 2026-07-26), `31fceba5` (`Value::getFromJSON` — `value.cpp.o` still 2026-07-26) |

Earlier long-run MEMSNAPSHOT evidence (PID `3342133`, ~22.7 h, day climb to ~1.8M cJSON / ~311 MiB) remains valid as a **prior** production-day growth profile; this restart has not been re-profiled with MEMSNAPSHOT (needs `/tmp/iod-verbose` + restart or stderr capture).

---

## Done recently (mqtt-fix)

| Item | Notes |
|------|--------|
| SHOW CYCLING / HEALTH / peak PROCSNAP | Track A — done (`c4b183e1`, merge `804c4ede`); **live** |
| Idle CPU: ecat throttle, no usleep, pending-out clear, urgency tiers | See `IDLE_CPU_FIXES.md`; **live** |
| Dig ASAP + analog quiet pull (5 ms) + sched floor 2× CYCLE_DELAY | `53fd85dd` series, `be7aa2cf`, `989240af`; **live** |
| Legacy turnOn/turnOff + bus-rate out service + SetState turning_* | `7e062d0c` — **live**; review later: `OUTPUT_PENDING_SETSTATE_REVIEW.md` |
| Channel client: timeout underflow, REQ reset, handshake thrash | `388b9ee4` … `dda16ce0`; **live** |
| cJSON ITEM DEFAULT + PutSubExpr ownership | `b985908f` — **in git only**; rebuild+restart to deploy |
| cJSON `Value::getFromJSON` scalar clone free | `31fceba5` — **in git only**; rebuild+restart to deploy |
| IOUpdate mask ownership | `6eaac1b8` |
| Prior overnight flat MEMSNAPSHOT after ITEM fix | PID `3342133` 22.7 h — idle paths OK; day growth open (historical) |

Details and measurement: `iod/IDLE_CPU_FIXES.md`, `iod/MEMORY_LEAK_INVESTIGATION.md`.

---

## Track A — Port thrash + PROCSNAP + HEALTH → mqtt-fix

**Status:** Done.

---

## Track B — Idle CPU fixes

**On mqtt-fix:** Done (including dig-ASAP, 5 ms quiet pull, out-service refine).  
**On elc:** Done (hand-merge earlier; see elc `OPEN_WORK_PLAN` history).

**Ops note:** Live plant (PID `127846`, day+ run) shows **LOAD BUSY ~110–125
loops/s** under HMI/core-panel activity (`SHOW HEALTH`, THRASH none). Earlier
quiet-window notes of ~30–60 loops/s may still apply when HMI is settled;
re-measure quiet vs auto after channel/HMI settle.

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
| JSON ITEM DEFAULT + PutSubExpr | **In tree** (`b985908f`); **not** in live PID `127846` binary |
| `Value::getFromJSON` scalar clones | **In tree** (`31fceba5`); **not** in live binary |
| Production-day live cJSON + WEBREQUEST arenas | **In progress** — offline fixes below; plant deploy pending |
| Methodology + plant evidence | `MEMORY_LEAK_INVESTIGATION.md`, `llm-rules/cw_issues/IOD_WEBREQUEST_*` |

**Plant evidence:**

- **Historical** PID `3342133` ~22.7 h: cjson/malloc **flat overnight**; day
  climb to ~1.8M nodes / ~311 MiB in_use / RSS ~334 MiB (ITEM-fix era profile).
- **Current** PID `127846` ~27.4 h (2026-07-28 11:38 → 2026-07-29 15:05): RSS
  **~138 MiB** with production/HMI load; main `[heap]` ~59 MiB; worker arenas
  present but not at prior multi-hundred-MiB RSS. No continuous
  `memory_monitor` CSV on this host right now (`/etc/service/memory_monitor`
  missing; `/opt/latproc/sampling/iod-memory/` absent).
- Main heap still the bounded mapping; worker arenas + live cJSON remain the
  production-growth suspects when load is high.

**Live binary path:** `/opt/latproc/iod/iod_sdo` (not a `staged-json-ownership`
name). Confirm `svstat` / `readlink /proc/$(pgrep -x iod_main)/exe` after any
deploy.

**Offline progress (2026-07-29, macOS warehouse sim + unit tests):**

1. **Done** — `exec_web_request.c`: fixed worker pool (default 4, `WEBREQUEST_POOL_SIZE`),
   atomic `done`/`abort`, per-worker easy-handle reuse. Unit tests:
   basic / POST / 50× repeated (`test_exec_web_request`).
2. **Done (LPC)** — clear `curl.Result` after `result := curl.Result` in
   warehouse `lib/api/samplingline_api.lpc` (and jemalong/rfid in CW sim).
3. **Done** — `apply()` uses `clone_json` / `cJSON_Duplicate` instead of
   Print+Parse (`json_expression.cpp`); ownership tests extended.
4. **Done (side fix)** — float `Value::operator%=` used `other.iValue` (often 0)
   → SIGFPE; now uses `::trunc(other.fValue)`. Generic, not macOS-only.
5. **Open** — catalog poll rate review (`P_BaleCatalogForAssignment`); Linux
   glibc arena plateau proof; plant deploy of this binary set.

**Offline still open:**

1. Linux VM load matrix per playbook (10k sequential, concurrency, RSS plateau).
2. Catalog poll rate / assignment dialog request frequency.
3. Plant deploy + MEMSNAPSHOT under production day load.

---

## Suggested next sequence

```
Plant (approved restart window)
1  Pull prod-experimental-mqtt-fix tip (includes 9106aee5 + 31fceba5 + b985908f)
2  Build Release iod/cw/iod_sdo AND rebuild web_request.so.1.0
3  Stage binaries with named rollback (iod_sdo.prev-*, web_request.so.prev-*)
4  Deploy; confirm new PID + binary mtime; re-enable DEBUG_MEMSNAPSHOT
5  Do not treat current ~138 MiB RSS as proof — JSON/WEBREQUEST fixes not live yet
6  Compare production-day cjson/RSS to 2026-07-28/29 baseline

Offline residual
7  Linux VM playbook load matrix (glibc arenas)
8  Catalog poll rate / P_BaleCatalogForAssignment review (CW/SVN)
9  Optional: SHOW HEALTH quiet vs auto after channel clients settled
```

---

## Non-goals (for now)

- Continuous thrash sampling in the CW loop (on-demand only).
- iocmd thrash-aware protocol changes.
- Blind full cherry-pick of all mqtt idle commits onto elc without review.
- Pre-start of panel CHANNEL publishers for fixed ports.
