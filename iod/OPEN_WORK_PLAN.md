# Open work plan (iod / Clockwork)

**Updated:** 2026-07-26 (evening)  
**Branch:** `feature/iod-elc-kernel-transport` (elc tip includes channel client recovery)  
**Also:** `prod-experimental-mqtt-fix` (legacy iod/iod_sdo)

Multi-session context for thrash/PROCSNAP, idle CPU, analog emit, plant LPC, and
channel/HMI client recovery.

---

## Done (summary)

| Item | Notes / tip commits |
|------|---------------------|
| Multi-domain isolation + Martin safety | elc |
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

**Status:** Mostly done on 1G2C-122 WC; SVN commit still open (auth failed).

| Remaining | Notes |
|-----------|--------|
| **SVN commit** plant WC | `Beckhoff/core_io.lpc`, `grab_io.lpc`, `Common/globals.lpc`, `Core/globals.lpc`, `Grab/globals.lpc`, GenericLib `generic_analog_input.lpc`, Grab `update_LIST.sh` |
| **Oil wiring** | Flags/software OK; oil not open (raw ~k counts). Tubes (unwired) show UR+Error. Physical loop. |
| **INPUTONPRESSURE** | Restored from GenericLib SVN; smoke eject at-pressure on site |
| **Other plants** | Migrate any remaining CLOCKINGWITHENABLE AI lists the same way |
| **CLOCKING* helpers** | Left in GenericLib as legacy; do not use for AI/COUNTER sampling |

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

## Suggested next sequence

```
1  SVN commit plant WC when credentials available
2  Oil physical loop (wiring)
3  Redeploy humid with channel client fix; leave monitors as safety net
4  Track D leftovers (INPUTONPRESSURE smoke; other plants)
5  Optional: server bind/exit(1) + port clash
6  Optional: quiet-load re-measure (SHOW HEALTH) after HMI stable
```

---

## Non-goals (for now)

- Continuous thrash sampling in the CW loop (on-demand only).
- iocmd thrash-aware protocol changes.
- Blind full cherry-pick of all mqtt idle commits onto elc without review.
- Pre-start of panel CHANNEL publishers for fixed ports.
- C-side named bit decode of packed status (CW/POINT owns flags).
