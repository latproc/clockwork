# Open work plan (iod / Clockwork)

**Updated:** 2026-07-29  
**This machine / branch:** `2C-120` (hostname) · plant class `2G4C` / process `--name 2GRAB` · `prod-experimental-mqtt-fix` (legacy `iod` / `iod_sdo`)  
**Related:** `feature/iod-elc-kernel-transport` (elc — dual-domain, analog emit, plant C/D)

Multi-session context so thrash/PROCSNAP, idle CPU, memory ownership, channels,
and cross-branch port work do not get lost.

---

## Live plant snapshot (2026-07-29 evening, post-`9106aee5`)

| Field | Value |
|-------|--------|
| Service | `/etc/service/iod` **up** (evening restart ~20:16 AEST) |
| Binary | `/opt/latproc/iod/iod_sdo` mtime **2026-07-29 ~20:02** — includes `9106aee5` (WEBREQUEST pool, `31fceba5`, apply clone) |
| Plugin | `/opt/latproc/code/plugins/web_request.so.1.0` mtime **2026-07-29 ~20:16** |
| Memory smoke | After hundreds of leak-tester GETs: RSS **~101–104 MiB**, threads **22** (flat) |
| Load | `SHOW HEALTH`: LOAD BUSY variable with HMI; THRASH none during smoke |

**Earlier same-day snapshot (2026-07-29 ~15:05 AEST, pre-pool deploy):** PID `127846` since 2026-07-28 11:38, binary mtime 2026-07-28 10:35, RSS ~138 MiB — **JSON ownership + WEBREQUEST pool were not yet live**. Superseded by evening deploy.

Earlier long-run MEMSNAPSHOT evidence (PID `3342133`, ~22.7 h, day climb to ~1.8M cJSON / ~311 MiB) remains valid as a **prior** production-day growth profile (pre-pool). Full production-day re-profile with MEMSNAPSHOT still open.

---

## Done recently (mqtt-fix)

| Item | Notes |
|------|--------|
| SHOW CYCLING / HEALTH / peak PROCSNAP | Track A — done (`c4b183e1`, merge `804c4ede`); **live** |
| Idle CPU: ecat throttle, no usleep, pending-out clear, urgency tiers | See `IDLE_CPU_FIXES.md`; **live** |
| Dig ASAP + analog quiet pull (5 ms) + sched floor 2× CYCLE_DELAY | `53fd85dd` series, `be7aa2cf`, `989240af`; **live** |
| Legacy turnOn/turnOff + bus-rate out service + SetState turning_* | `7e062d0c` — **live**; review later: `OUTPUT_PENDING_SETSTATE_REVIEW.md` |
| Channel client: timeout underflow, REQ reset, handshake thrash | `388b9ee4` … `dda16ce0`; **live** |
| cJSON ITEM DEFAULT + PutSubExpr ownership | `b985908f` (see `MEMORY_LEAK_INVESTIGATION.md`); **live** after evening binary |
| cJSON `Value::getFromJSON` scalar clone free | `31fceba5` — **live** in `9106aee5` binary |
| WEBREQUEST fixed worker pool + easy-handle reuse | `9106aee5` — **live** on 2C-120 (2026-07-29 evening) |
| `apply()` clone_json (no Print+Parse) | `9106aee5` |
| Float `Value::operator%=` SIGFPE | `9106aee5` (use `trunc(other.fValue)`) |
| IOUpdate mask ownership | `6eaac1b8` |
| Prior overnight flat MEMSNAPSHOT after ITEM fix | PID `3342133` 22.7 h — idle paths OK |

Details and measurement: `iod/IDLE_CPU_FIXES.md`, `iod/MEMORY_LEAK_INVESTIGATION.md`,
`code/llm-rules/cw_issues/IOD_WEBREQUEST_*`.

---

## Track A — Port thrash + PROCSNAP + HEALTH → mqtt-fix

**Status:** Done.

---

## Track B — Idle CPU fixes

**On mqtt-fix:** Done (including dig-ASAP, 5 ms quiet pull, out-service refine).  
**On elc:** Done (hand-merge earlier; see elc `OPEN_WORK_PLAN` history).

**Ops note:** Live plant can show **LOAD BUSY ~110–125 loops/s** under
HMI/core-panel activity (`SHOW HEALTH`, THRASH none). Earlier quiet-window
notes of ~30–60 loops/s may still apply when HMI is settled; re-measure quiet
vs auto after channel/HMI settle.

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

### Done (do not re-open without a new stack)

| Item | Status |
|------|--------|
| IOUpdate mask `owns_mask_` | Done (`6eaac1b8`) |
| Idle scalar free-on-convert (`Value(cJSON*)`) | Done; exact cJSON stack closed |
| JSON ITEM DEFAULT + PutSubExpr | Done (`b985908f`); **night flat on plant**; **live** after evening binary |
| `Value::getFromJSON` scalar clones | Done (`31fceba5`); **in live** `9106aee5` binary |
| WEBREQUEST worker pool (default 4, `WEBREQUEST_POOL_SIZE`) | Done (`9106aee5`); **live on 2C-120** |
| Atomic `done`/`abort`, per-worker curl easy-handle reuse | Done (`9106aee5`) |
| `apply()` → `clone_json` / `cJSON_Duplicate` | Done (`9106aee5`) |
| Float `%=` SIGFPE | Done (`9106aee5`) |
| Methodology + historical plant evidence | `MEMORY_LEAK_INVESTIGATION.md`, `llm-rules/cw_issues/IOD_WEBREQUEST_*` |

**Plant smoke (2026-07-29 evening, live `9106aee5` binary + `web_request.so`):**

- `M_WebRequestLeakTest` → `S_SamplingLineAPI.root_url` =
  `http://172.29.54.1:8000/api/v1/` (real warehouse API).
- Hundreds of sequential GETs (`clear_result true`): Status **200**, threads
  **flat at 22**, RSS **plateau after ~1 MiB warm-up** (no linear growth with
  request count).
- Does **not** replace a production-day MEMSNAPSHOT under multi-machine warehouse
  traffic; only proves the pool path under the leak tester.

**Historical plant evidence (do not re-prove idle drip):**

- PID `3342133` ~22.7 h: cjson/malloc **flat overnight**; day climb to ~1.8M
  nodes / ~311 MiB in_use / RSS ~334 MiB (pre-pool binary).
- Main heap mapping flat; growth was worker arenas + live retained cJSON under
  production HTTP/JSON — arena half is what the pool targets.
- Afternoon PID `127846` (~138 MiB, pre-pool) is a mid-day snapshot only.

**Rollback / staged names on plant:**  
`iod_sdo.prev-memfix-*`, `iod_sdo.staged-json-ownership-fix` — confirm
`svstat` / running path before treating as production.

### Residuals only (true open work)

| Residual | Notes |
|----------|--------|
| Production-day MEMSNAPSHOT under real load | Re-enable `DEBUG DEBUG_MEMSNAPSHOT on` after restart; compare equal bale/request windows to 2026-07-28/29. Expect arena slope lower with pool; prove cjson/malloc_in_use do not climb unboundedly. |
| Warehouse LPC `curl.Result` clear after extract | **Not** on plant `warehouse/lib/api/samplingline_api.lpc` (still `result := curl.Result` only). Leak tester clears Result; production API machines keep both copies → working-set risk. Port clear-after-copy if day cJSON still climbs. |
| Catalog poll rate (`P_BaleCatalogForAssignment`) | Historical ~441 catalogs / ~50 bales. Measure then reduce if still excessive — symptom relief, not a substitute for ownership. |
| Full Linux playbook matrix | 10k sequential, concurrency 2/4/8, errors/timeouts/abort; optional ASan/LSan or `malloc_info`. Unit-test binary on plant may be stale (rebuild `test_exec_web_request` when quiet). |
| Multi-instance / production concurrency | Leak tester is **one** sequential WEBREQUEST; production has many concurrent warehouse curls. |

**Not residuals (closed or non-goals):**

- Idle 24 nodes/min scalar path.
- Night-flat MEMSNAPSHOT as “still leaking idle” without a **new** stack.
- Naive `curl.Request` vs HTTP 200 counts (tab-aware empty/clear accounting).
- Re-implementing the WEBREQUEST thread pool (already live).
- Claiming JSON/WEBREQUEST ownership fixes are “git only” after evening `9106aee5` deploy.

---

## Suggested next sequence

```
Plant (validation — binary already deployed)
1  Confirm live binary still 9106aee5 (pool + getFromJSON + apply clone)
2  Re-enable DEBUG_MEMSNAPSHOT; do not chase overnight drip (already flat)
3  Production day: watch MEMSNAPSHOT + sampler curl accounting under real traffic

CW / working-set (if day cjson still climbs)
4  Clear curl.Result after result := curl.Result in warehouse API LPC
5  Review P_BaleCatalogForAssignment poll frequency

Optional offline / quiet host
6  Rebuild and run test_exec_web_request / test_json_ownership
7  Playbook matrix (10k, concurrency) if stronger allocator proof needed

Ops
8  Optional: SHOW HEALTH quiet vs auto after channel clients settled
```

---

## Non-goals (for now)

- Continuous thrash sampling in the CW loop (on-demand only).
- iocmd thrash-aware protocol changes.
- Blind full cherry-pick of all mqtt idle commits onto elc without review.
- Pre-start of panel CHANNEL publishers for fixed ports.
