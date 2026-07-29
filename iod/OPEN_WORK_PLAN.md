# Open work plan (iod / Clockwork)

**Updated:** 2026-07-27 (Stage 4 elc + CW domain isolation proven)  
**Branch:** `feature/iod-elc-kernel-transport` (elc tip includes channel client recovery)  
**Also:** `prod-experimental-mqtt-fix` (legacy iod/iod_sdo)

Multi-session context for thrash/PROCSNAP, idle CPU, analog emit, plant LPC, and
channel/HMI client recovery.

---

## Done (summary)

| Item | Notes / tip commits |
|------|---------------------|
| Multi-domain isolation + Martin safety | elc |
| **Stage 4 dual-domain servo control power-off** (domain bus WC firewall) | **elc + CW 2026-07-27.** elc WC firewall; CW active mirrors + plant re-prove d2 `COMPLETE→INCOMPLETE` (`faults=0x20`); lifecycle hold so dual INVALID/size=0 is not “both bus-failed” |
| `SHOW CYCLING` / `HEALTH` / peak `PROCSNAP` | elc `50900dc8`; also mqtt-fix (Track A) |
| Idle CPU / urgency / pending-out / keep-alive | elc Track B |
| Dig ASAP + analog pace (kernel `domain1_pd`) | elc Track B2 |
| Analog/COUNTER emit; owner-only; `RECEIVE update` only | `f25d9e5e`, `c77c416c`, `30c6aea4`, … |
| Always publish `ENG`; MASK 0 = no mask on DIGITALVALUE | `5e7431f6` |
| Multi-bit POINT/STATUS_FLAG → DigitalValue (not AnalogueInput) | `5e7431f6` / EtherCATSetup |
| Soft-clock sampling list removed (this plant) | plant WC; A_* thin `VALUE := IA.ENG` |
| EL3124 status: live PDO map (SI1 UR, SI2 OR, SI3 L1, SI5 L2, SI7 Error) | plant; tubes open → UR+Error on |
| Sampler `channelName:SAMPLER_CHANNEL` | plant `Common/globals.lpc` |
| Channel client: wait-time underflow | `adf01887` (`now - send_time`) |
| Channel client: REQ reset + recreate after timeout/EFSM/disconnect | `c2ac663e` |
| Housekeeping: old `iod-elc.bak*`, `build-elc/`, rollback snapshot, `/tmp` experiment logs | local clean |

---

## Track A — Port thrash + PROCSNAP + HEALTH → mqtt-fix

**Status:** Done.

---

## Track B — Idle CPU fixes: mqtt-fix ↔ elc

**Status:** Done on elc (hand-merge). Details: `iod/IDLE_CPU_FIXES.md`.

**Note:** Live plant can still show **LOAD BUSY ~30–60 loops/s** under HMI/sampler
activity; re-measure quiet vs auto after channel/HMI settle if CPU still matters.

---

## Track B2 — Digital ASAP vs analog pace

**Status:** Done on elc.

---

## Track C — Analog / COUNTER emit (iod owns sampling)

### Intent (end state)

IO path owns sampling, scale (`factor`/`base`/`window`), and publishing
VALUE/ENG/IOTIME under rate/guard policy. Clockwork must not run soft-clock
lists that re-derive eng. Work = iod emit + selective RECEIVE.

### Status: Done for this plant (bridge cleared)

| Layer | Role |
|-------|------|
| **IA / COUNTER** | Scale on map; int `VALUE` + always `ENG`; emit on change/window/safety; `notifyClockedUpdateConsumers()` only |
| **Plant** | No `L_ClockedAnalogInputs` / `M_ClockedAnalogInputs` |
| **A_*** | Thin eng alias on RECEIVE (`VALUE := IA.ENG`; torque 16-bit signed) |
| **Plugins** | Live int on IA |

**Do not** reintroduce soft-clock lists for AI/COUNTER or status words.

---

## Track D — Plant LPC cleanup

**Status:** Done for 1G2C-122 plant LPC path (SVN committed by site). Residual = oil wiring + optional other plants.

| Item | Status |
|------|--------|
| Soft-clock list removed; A_* thin RECEIVE | Done |
| EL3124 status POINT/DV map + `update_LIST.sh` | Done |
| GenericLib `CLOCKEDANALOGINPUT` → `IA.ENG` | Done (SVN) |
| **SVN plant WC** | Done (site committed most) |
| **INPUTONPRESSURE smoke** | Loaded OK: `I_CoreEjectUp` / `IR_CoreEjectDown` with `onValue` 2800/3000, `Over` on `IA_CoreVB2Pressure` (factor 0.173822). Currently **off** because eject outputs DISABLED (expected idle). Full at-pressure check needs eject enable on site. |
| **Oil wiring** | Tomorrow — software OK; not open-loop like tubes |
| **Other plants** | Optional — only if they still use CLOCKINGWITHENABLE for AI |
| **CLOCKING* helpers** | Legacy in GenericLib; do not use for AI/COUNTER sampling |

**EL3124 status map (reference):**

| CoE SI | Bitlen | Use |
|--------|--------|-----|
| 1 Underrange | 1 | POINT |
| 2 Overrange | 1 | POINT |
| 3 Limit1 | 2 | DIGITALVALUE MASK 0x3 |
| 5 Limit2 | 2 | DIGITALVALUE MASK 0x3 |
| 7 Error | 1 | POINT (not SI5) |
| 17 Value | 16 | ANALOGINPUT |

Regenerate lists only via `./machine/scripts/update_LIST.sh` (do not hand-edit `io_list.lpc`).

---

## Track E — Channel clients / HMI (CW2CW-relevant)

### Intent

Clients (humid, persistd, dbd, modbusd, remote CW) reconnect CHANNEL setup
without process restart. **No panel channel pre-start** — data port comes from
`CHANNEL` reply (fixed preferred port or `uniquePort()`).

### Done

| Item | Commit |
|------|--------|
| Timeout elapsed-time underflow | `adf01887` |
| Reset `sent_request` + recreate setup REQ after timeout/EFSM/disconnect | `c2ac663e` |

### Still open / ops

| Item | Notes |
|------|--------|
| **Redeploy humid** (Core/Grab) with tree that includes `c2ac663e`+ | Otherwise HMI keeps sticky REQ until kill/restart |
| **HMI CHANNELMONITOR kill** | Intentional last-resort (`killall humid`) while client was broken; after fixed humid, keep as safety net or lengthen `killTime` |
| **Optional server harden** | On bind `EADDRINUSE`: try `uniquePort()` / return error — **do not `exit(1)`**. Resolve **7902 clash** `MODBUS_CHANNEL` vs `PANEL_CORE` if both used |
| **CW2CW other projects** | Same client library; deploy `c2ac663e` on **clients**. Server sticky-REQ is not the issue |

### Not planned

- Pre-start `PANEL_*` publishers at boot (keeps data ports assignable at CHANNEL time).

---

## Track F — Memory / JSON ownership (mqtt-fix)

| Item | Status |
|------|--------|
| IOUpdate mask `owns_mask_` | Done (`6eaac1b8`) |
| JSON ITEM DEFAULT + PutSubExpr | Done (`b985908f`); **night flat on plant** |
| `Value::getFromJSON` scalar clones | Done in tree (`31fceba5`); deploy on next restart |
| Production-day live cJSON + WEBREQUEST arenas | **In progress** — offline fixes below |
| Methodology + plant evidence | `MEMORY_LEAK_INVESTIGATION.md`, `llm-rules/cw_issues/IOD_WEBREQUEST_*` |

**Plant evidence (do not re-prove idle on controller):**

- PID `3342133` ~22.7 h: cjson/malloc **flat overnight**; day climb to ~1.8M
  nodes / ~311 MiB in_use / RSS ~334 MiB.
- Main heap mapping flat; worker arenas + live cJSON drive production growth.

**Local staged binaries on 2G-120:**  
`iod_sdo.prev-memfix-*`, `iod_sdo.staged-json-ownership-fix` — confirm
`svstat` / running path before treating as production.

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
1  SVN plant WC          — done (site)
2  Oil physical loop     — tomorrow
3  Humid with client fix — done (deployed, looks good)
4  Track D leftovers     — done for this plant (INPUTONPRESSURE structure OK;
                           full eject-at-pressure when outputs enabled)
5  Optional: server bind/exit(1) + port 7902 clash
6  Optional: quiet-load re-measure after HMI stable
```

---

## Non-goals (for now)

- Continuous thrash sampling in the CW loop (on-demand only).
- iocmd thrash-aware protocol changes.
- Blind full cherry-pick of all mqtt idle commits onto elc without review.
- Pre-start of panel CHANNEL publishers for fixed ports.
- C-side named bit decode of packed status (CW/POINT owns flags).
