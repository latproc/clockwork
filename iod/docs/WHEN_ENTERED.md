# Spec: WHEN … ENTERED &lt;state&gt; (not WAS, not a change to IS)

**Status:** SPEC — not implemented
**Class:** cw_issues   **Serial:** fleet   **Opened:** 2026-10-08   **Last verified:** 2026-10-08
**Fix:** none yet. Target **both** active iod lines: `prod-experimental-mqtt-fix` and `feature/iod-elc-kernel-transport`.

## Why

`WHEN x IS S` is a **level**: true only while `x` is in `S` **now**. Handshake pulses (`Done`, `Clearing`, camera `done`, conveyor `ReadyWait`) last one eval (332 µs–1.2 ms) because ENTER immediately `SET`s `State` away. mqtt-fix still evaluates listeners in that window. iod-elc analog absorb often does not, so `WHEN M_TipControl IS Done` misses and the feeder stays `TipWait`.

`RECEIVE x.Done` is already the **edge**. It is a separate handler, not in the WHEN list, so it does not compete with `TipWait` / first-WHEN-wins. A WHEN-list edge is still useful.

This spec adds that edge **without** changing `IS`.

## Keyword: ENTERED, not WAS

Use **`ENTERED`**.

| | ENTERED | WAS |
|---|---|---|
| Reads as | `x` just **entered** `S` | `x` **was** `S` (when?) |
| Matches LPC | `ENTER` / `LEAVE` | no existing word |
| Stale reading | “this pass only” (below) | sounds like history |

`WAS` invites “Done from last tip.” Do not add `WAS`. A later `LEFT` (mirror of `LEAVE`) is out of scope.

## Syntax

```
WHEN <machine> ENTERED <state>
WHEN SELF IS TipWorking && M_TipControl ENTERED Done
```

- `<machine>` is a **parameter we listen to** (same set as `IS`), not `SELF`, not a nested `State` property.
- `<state>` is a state **name** (identifier), same as `IS Done`.
- Combines with `&&` / `||` like `IS`.
- **`IS` is unchanged:** live only. Guards, PEs, `Ready`, `atposition` stay `IS`.

Illegal / ignored:

- `SELF ENTERED Done` — use `ENTER Done` on this machine.
- `State ENTERED Empty` — nested flag/state machine; still `State IS Empty`.
- `I_Present ENTERED on` — legal **but do not use** on PEs/guards (see breakage).

## Semantics (anti-stale)

On `setState(S)` of source `x`:

1. For each machine `L` that **listens to** `x` (parameter / depends), set one slot  
   `L.entered[x] = S`  
   (last enter wins; no count).
2. `WHEN x ENTERED S` on `L` is true iff that slot is `S` **during L’s WHEN pass**.
3. **At the end of that pass**, clear `L.entered[x]`, whether any WHEN matched or not.

Not a global iod tick. If absorb skips `L` this loop, the slot **stays** until `L` actually runs WHENs once, then dies. `d78f4cc8` (runnable drain) still matters so that pass is soon.

Maximum staleness: one WHEN pass of the listener. Not a stored Done.

## How to choose IS vs ENTERED

- **`IS`** — must still be true: `Guard IS false`, `I_Entry IS off`, `M_TipControl IS Ready` before `CALL Tip`.
- **`ENTERED`** — handshake whose ENTER immediately leaves: `Done`, `Clearing`, camera `done`, `ReadyWait` flash.

Rule: if the source ENTER **SET**s `State` (or itself) to something else in the same tick, listeners that care about that tick use `ENTERED` (or `RECEIVE`), not `IS`.

Do **not** use `ENTERED` on E24, PEs, or interlocks. A 1 ms `false` would fire `interlocked` after Guard is true.

## RECEIVE vs ENTERED

| | RECEIVE x.Done | WHEN x ENTERED Done |
|---|---|---|
| Already exists | yes | no |
| WHEN-list order | no (extra handler) | yes (first WHEN wins) |
| FLAG needed | no if body is the action | no |

Prefer **`RECEIVE`** when the only action is `SET`/`CALL` in the handler (`TipDone` → `SET State TO Ready`). Prefer **`ENTERED`** when the edge must sit **among** other WHENs (`TipWait` / `TipWorking`).

Do **not** add a FLAG per handshake. Source-side timer holds (Clearing/TipDone 200 ms) remain valid and keep `IS` working without new keywords.

## What we will not do

- **`IS` = live OR queued enter.** Breaks levels (Guard, PE) even if the queue is one pass.
- **Keep the edge after the listener’s WHEN pass.** That is stale `WAS`.
- **Cherry-pick mqtt-fix shadow `enableShadows` onto elc** as a substitute; different model (`applyStagedShadows`).
- **Ship ENTERED only on elc.** Language must match on **mqtt-fix and kernel-transport** so plant LPC is switchable.

## Implementation (both branches)

Same parser + runtime on `prod-experimental-mqtt-fix` and `feature/iod-elc-kernel-transport`.

1. **Parser:** `ENTERED` as a comparison in WHEN, like `IS`, token not a state name of the listener.
2. **Runtime:** on `MachineInstance::setState`, fill `entered[source]` on listeners; WHEN eval reads it; clear after that machine’s rule pass.
3. **Tests:**  
   - Source Done 1 eval, listener not run until next processAll → `ENTERED Done` still true once, then false.  
   - Guard false 1 eval → `WHEN Guard IS false` false after Guard true; `ENTERED false` true for one listener pass only.  
   - `IS Done` still false if source already left Done.
4. **LPC:** do not rewrite the fleet. Optional: feeder `TipDone WHEN … && M_TipControl ENTERED Done` once both iods are installed.

## 2G-115 evidence (why this exists)

Sampler `log-20261008.txt`, `cw_short_state_scan.py --max-ms 2`:  
`M_GrabBaleGateControl` **Done** &lt;2 ms ×3; `M_GrabFeeder` has `TipDone WHEN … && M_TipControl IS Done`. Gate `ENTER Clearing` `SET State TO Empty` in the same tick (~968 µs). mqtt-fix still catches it; elc absorb often does not.

## Status

SPEC only. Not in iod. Clearing/TipDone 200 ms holds are the LPC workaround on Grab until this ships on **both** iod lines.
