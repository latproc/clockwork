# Lost TIMER events on `prod-experimental-mqtt-fix` — root cause and identification

**Branch under investigation:** `prod-experimental-mqtt-fix` @ `3ce87578`
**Worktree:** `/Users/mike/src/RemoteAdminLinux/latproc-mqtt`
**Written:** 2026-09-16. **Corrected** after an empirical reproduction — an
earlier draft of this note blamed `ArmFutureOnly`; that was wrong.

> **Line-number scope.** Every `iod/src/...` line reference below is on
> `prod-experimental-mqtt-fix` @ `3ce87578`. The same functions sit at different
> lines on `feature/iod-elc-kernel-transport`. Do not mix the two.

Related site notes (outside this repo):
`llm-rules/2G4C/PIDLISTCLOCK_TIMER_STALL_20260805.md`,
`llm-rules/2G4C/120_IOD_TIMER_BUSY_ANALOG_STALL_20260916.md`,
`llm-rules/2G4C/120_ANALOG_FIRST_TICK_20260916.md`,
`llm-rules/cw_issues/IOD_TIMER_SOFT_CLOCKS_AND_COMMANDCLOCK_20260812.md`.

---

## 1. Root cause (confirmed)

The wake is **not** lost at the TIMER. It is **erased by the caller of the
evaluation that just re-requested it**.

Sequence, with the machine parked in a matched holding rule whose `TIMER` is
already overdue:

| # | Step | Code |
|---|---|---|
| 1 | Scheduler item becomes due; `fireDueItems` pops it, fires the trigger, and **deletes the item** | `Scheduler.cpp:399-419` (pop `411`), dispatch/delete `369-397` (`378`) |
| 2 | `Trigger::fire` → `MachineInstance::triggerFired` → `setNeedsCheck` | `Trigger.cpp:128-136`, `MachineInstance.cpp:354-360` |
| 3 | `setNeedsCheck` takes the **coalescing early return** — machine already pending — and only bumps `needs_check` | `MachineInstance.cpp:182-187` |
| 4 | The pending machine is evaluated; `setStableState` clears the flag at the top | `MachineInstance.cpp:3554` |
| 5 | The **matched holding** rule is reached and the overdue TIMER is correctly recovered: `RecoverOverdue` calls `setNeedsCheck`, which re-inserts the machine into `pending_state_change` and increments the counter again | `MachineInstance.cpp:3658-3670` (subconditions `3651-3655`) |
| 6 | `scheduleTimerEvents` returns `ptd = 0` for the overdue case (no future arm) | `Expression.cpp:496-531` |
| 7 | `setStableState` returns false (no state change) | |
| 8 | **`checkStableStates` erases the machine from `pending_state_change`** — discarding exactly the entry step 5 created | `MachineInstance.cpp:1465-1471` |

End state: `needs_check != 0`, but **no `pending_state_change` membership, no
runnable membership, and no scheduled item**. Nothing will evaluate that
machine again until an unrelated event calls `setNeedsCheck`. The counter is
inert — `ProcessingThread` builds `to_process` only from runnable entries that
are `queuedForStableStateTest()` (`ProcessingThread.cpp:1790-1806`).

The defect is the erase at `MachineInstance.cpp:1465-1471`:

```cpp
if (!mi->executingCommand() && mi->mail_queue.empty()) {
    if (!mi->enabled() || !mi->getStateMachine()->allow_auto_states ||
        !mi->setStableState()) {          // <-- step 5 re-queues during this call
        std::lock_guard<std::mutex> lock(pending_state_change_mutex);
        pending_state_change.erase(mi);   // <-- step 8 discards it
    }
}
```

`RecoverOverdue` exists specifically to make a late check survive
(`Expression.cpp:524-529`, comment: *"Dropping a late wake left TIMER
soft-clocks stuck until an unrelated input"*). The erase immediately undoes it.
This is pre-existing code — the erase predates the 2026-09-15 patches.

**Why this is the documented symptom.** It produces exactly the `PIDLISTCLOCK`
residual on both boxes: the clock holds `on` with `TIMER ≫ rate`, no further
edge, and recovery only on iod restart or an unrelated input.

## 2. What the two 2026-09-15 patches change here

- **`0e163446`** (live SSTimer trigger) is correct and independent: it removes a
  stack-copy `StableState` whose destructor disabled the trigger
  (`MachineInstance.cpp:2354-2373`). Verified: the copy is gone.
- **`3ce87578`** (`fireDueItems` on the processing thread) is what makes the
  above reachable *while busy*: it drains and deletes due items from
  `processAll` (`1387-1395`), `checkStableStates` (`1481-1489`) and
  `ProcessingThread::operator()` (`1774`, `1784`, `1812`). Before it, an idle
  handshake could re-fire the machine later; after it, the item is consumed
  inside the same pass whose erase discards the recovery. It is an
  **enabler/aggravator**, not the origin.

**The stall is not universal.** Any evaluation that actually *changes state*
re-arms via `setState` (`MachineInstance.cpp:2366-2369`), so transition-driven
clocks are safe. The failure needs a **matched hold with no state change** —
exactly a soft clock holding on `TIMER < rate`.

## 3. Empirical confirmation

A purpose-built probe was linked against the real `3ce87578` `cw_interpreter`
and exercised the real `Scheduler`/`Trigger`/`Predicate`/`StableState`/
`MachineInstance` code:

```
cmake -S iod -B _repro-mqtt/build -DRUN_TESTS=OFF -DCMAKE_BUILD_TYPE=Release
CCACHE_DIR=$PWD/_repro-mqtt/ccache cmake --build _repro-mqtt/build \
    --target cw_interpreter cw_client -j8
./_repro-mqtt/repro_timer_wake
```

| Phase | Result |
|---|---|
| 1 | `fireDueItems` returned 1, trigger fired, `total_machines_needing_check` unchanged ⇒ coalescing early return taken, item deleted |
| 2 | overdue + `ArmFutureOnly` ⇒ `ptd=0`, no recheck; overdue + `RecoverOverdue` ⇒ `setNeedsCheck` (0→1) |
| **3** | after the real `checkStableStates`: `schedPending=0`, `pending_state_change` membership `=0`, `is_pending=0`, but the global counter moved `0→1` — proving `RecoverOverdue` ran **and its entry was erased** |
| 4 | a state-changing evaluation re-arms (`schedPending=1`) ⇒ transitions are safe |

Artifacts (untracked, in the mqtt worktree): `_repro-mqtt/RESULT.md`,
`repro_timer_wake.cpp`, `run.log`, `build/`.

Also noted while reading: `SetStateAction.cpp:331-357` is dead code —
`stable_state_xref` is never populated anywhere in the repo, so
`isStableState()` is always false on that path.

## 4. What to read in production (all existing instrumentation)

1. **`SHOW SCHEDULER`** (`Scheduler::getStatus`, `Scheduler.cpp:74-105`) — live
   item count and readiness. This is the decisive reading: when a TIMER-driven
   machine is stuck and the scheduler queue is **empty**, the wake was consumed
   and nothing re-armed. Pair with **`SHOW PROCSNAP` / `SHOW LOAD`** for
   `runnable` / `stable` / `exec` / `mail` counts.
2. **`DEBUG DEBUG_STALLSNAP on`** — `STALLSNAP` lines carry `machine=`, `stage=`,
   `runnable=`, `stable=`. Stuck machine with `runnable=0 stable=0` and an empty
   scheduler is the fingerprint of this defect.
3. **`DEBUG DEBUG_SCHEDULER on`** — `"Scheduler activating scheduled item …"`
   (`Scheduler.cpp:409`), `"Scheduler firing trigger"` (`375`),
   `"Predicate scheduling item … for <t>"` (`Expression.cpp:507`), and
   `setState`'s `"Scheduling timer for"` (`MachineInstance.cpp:2357`). A state
   change with **no** following `"Scheduling timer for"` while the queue is
   empty means wedged.
4. **`DEBUG DEBUG_MESSAGING on`** — `"::setNeedsCheck()"` and `"queued for
   stable state checks"` (`170`/`211`); the coalescing early return is silent at
   the log level used, which is why this was invisible.
5. **`SHOW TRIGGERS`** (`IODCommands.cpp:1145`, rendered by
   `Trigger::getTriggers`, `Trigger.cpp:54-79`) — trigger names with `refs=` and
   `age=`, identifies which `Timer <machine>` / `SSTimer <machine> <state>` is
   missing.

Minimal capture for one stall:

```text
DEBUG DEBUG_SCHEDULER on
DEBUG DEBUG_STALLSNAP on
# at the stuck moment, from iosh:
SHOW SCHEDULER
SHOW TRIGGERS
SHOW PROCSNAP
```

**Question the capture must answer:** after the due item was fired, did the
target remain in `pending_state_change` or gain a fresh scheduler item? No to
both = this defect.

## 5. Fix direction

Fix the caller, not `Expression.cpp`. After `setStableState()` returns, the
erase must be conditional on the machine **not** having an outstanding wake
request:

- if `needs_check` is non-zero after the evaluation (a `RecoverOverdue`
  re-queue, or any other mid-evaluation `setNeedsCheck`), **keep** the
  `pending_state_change` entry so the next pass re-evaluates it; and
- only erase when the evaluation genuinely left nothing outstanding.

The counter alone is not sufficient — `checkStableStates` builds its work list
from `pending_state_change` membership, so the membership is what must survive.
This is also the place to consider whether `fireDueItems` should be consuming
items while busy at all (`3ce87578`), but that is a separate, second question
and should be staged separately.

## 6. Unknowns and cautions

- `2G-120` was reverted with the first-tick LPC in the **same bounce**, so that
  correlation remains unisolated. Nothing here changes that.
- `2G-115` still carries both patches. Any change must be staged per
  `iod/docs/BRANCHES.md` (line B is elc-only; the legacy bus line is separate).
- The reproduction is a linked-interpreter probe, not a live plant run: it
  proves the mechanism and the erased-entry end state, not which production
  stall was caused by it.
- No box was touched and no binary was installed. Build output lives only under
  `_repro-mqtt/` in the mqtt worktree.


---

## Addendum — regression test (verified 2026-09-16)

A gtest (`TimerWakeRetention`) was written and verified against the real
googletest/CTest harness:

- `OverdueMatchedHoldKeepsItsRecheck` — drives a machine parked in a matched
  hold whose rule TIMER is overdue, through `checkStableStates`, and asserts the
  invariant: **if a re-check was requested (the global counter is non-zero after
  `checkStableStates` zeroed it at entry), the machine must remain reachable**
  (queued for a stable-state test, runnable, or holding a scheduled item).
- `StateChangeStaysQueued` — control; the transition path must stay queued.

### Evidence on `prod-experimental-mqtt-fix` @3ce87578

```
[ RUN      ] TimerWakeRetention.OverdueMatchedHoldKeepsItsRecheck
Value of: queued          Actual: false   Expected: true
Google Test trace: re-check requested but machine unreachable
[  FAILED  ] TimerWakeRetention.OverdueMatchedHoldKeepsItsRecheck
[       OK ] TimerWakeRetention.StateChangeStaysQueued
```

Then, with the fix direction applied only in a scratch worktree (conditional
erase in `checkStableStates`: keep the `pending_state_change` entry while
`needsCheck()` is still set):

```
[       OK ] TimerWakeRetention.OverdueMatchedHoldKeepsItsRecheck
[       OK ] TimerWakeRetention.StateChangeStaysQueued
[  PASSED  ] 2 tests.
```

So the test is a valid acceptance gate on this line: it fails on the defect and
passes on the fix.

### `feature/iod-elc-kernel-transport` — NOT yet demonstrated

The same test compiles and runs there, but fails at its *precondition*
(`recheck_requested` false), because that line routes the freshly enabled
machine to the action path (`setNeedsCheck` -> `SharedWorkSet`) instead of the
stable-state queue, so `checkStableStates` skips it and the overdue recovery
never runs. That is a test-harness gap (no activated `ProcessingThread`), **not**
evidence of the defect. Do not treat the test as a gate on that line until the
harness drives the real activation path.

The erase code read on that line (`pending_state_change.erase(mi)` after
`!mi->setStableState()`) is the same shape, so the defect is *likely* present
there, but it is unproven.


---

## Addendum 2 — the defect is NOT introduced by the two 2026-09-15 patches

Experiment (2026-09-16), on `prod-experimental-mqtt-fix` checked out at
`3ce87578`:

| Build | `OverdueMatchedHoldKeepsItsRecheck` | `StateChangeStaysQueued` |
|---|---|---|
| `3ce87578` unmodified | **FAIL** | PASS |
| `3ce87578` with `3ce87578` + `0e163446` reverted (`git revert --no-commit`) | **FAIL** | PASS |
| `3ce87578` + conditional-erase fix in `checkStableStates` | PASS | PASS |

So the wake-loss is present in the **base** and survives removal of both TIMER
patches. `checkStableStates`'s unconditional erase predates them and is not
touched by them. Conclusion: reverting those two commits will **not** fix the
lost-TIMER symptom; the patches are an enabler/aggravator at most.

### Test caveat found and fixed

The first version of the test passed spuriously on the base: `machineIsReachable`
counted `Scheduler::pendingCount() > 0`, and `enable()` leaves a state TIMER for
the machine under test in the queue, so the invariant was satisfied by that
leftover. The fixture now drains scheduled items after `enable()`, so the only
way the post-evaluation assertion can hold is if the overdue recovery actually
re-queued the machine. Any future test here must not count unrelated queue
entries as evidence.

### Environment note

`git checkout`/`git reset` restoring a source file with an older mtime than its
object file makes `make` skip the rebuild, so a "revert then re-run" can silently
reuse the previous binary. Force the rebuild (touch the source or delete the
object) before trusting any post-revert result.


---

## Addendum 3 — fix attempt gated: no regressions, but a livelock

Test suite now has three cases: `OverdueMatchedHoldKeepsItsRecheck` (invariant),
`OverdueWakeLeadsToProgressNotSpin` (stall + livelock), `StateChangeStaysQueued`
(control).

| Build (`3ce87578`) | KeepsItsRecheck | ProgressNotSpin | Control | Full CTest |
|---|---|---|---|---|
| unmodified | FAIL | FAIL | PASS | — |
| + conditional-erase fix | PASS | **FAIL** | PASS | **25/25 PASS** |

**Step 1 (stall is real): confirmed.** The wake-loss leaves the machine in
neither queue, and repeated processing passes never select it; the overdue TIMER
is never handled and the state never advances.

**Step 2 (gate the fix):**
- No regressions: the other 25 CTest targets all pass with the fix applied.
- **But the naive fix livelocks.** With the fix, after the due item is consumed
  the machine stays permanently `needsCheck()`-pending, re-requesting a check
  every pass that never resolves (`rechecks` stays 1 forever; state never
  advances). That is the same class of failure that got the earlier
  `3bd56a56`/`6c8f6545` attempt reverted, so this fix must not be landed as-is.

  A correct fix needs to clear `needs_check` / release the `pending_state_change`
  entry once the requested re-check has actually been performed, rather than
  holding it forever. Alternatively the livelock may be partly a harness
  artifact: the test drives `checkStableStates` only, whereas production also
  drains the queued `SetStateAction` via the machine's action path. That must be
  resolved before concluding the fix is unworkable — but the test is a useful
  gate either way, because it fails loudly on the unbounded-recheck shape.

**Still open:** making the test faithful on `feature/iod-elc-kernel-transport`
(step 3) and the 2G-120 correlation (step 4).


---

## Addendum 4 — the 2G-120 correlation: patches are not implicated

The premise behind blaming `3ce87578` for the 2G-120 analog stall was that
draining due TIMERs on the busy processing thread changed the wake behaviour.
It does not change *whether* the wake is lost, only **which thread drains it
first**:

- Scheduler thread (unpatched): `Scheduler::idle` -> `fireDueItems` ->
  `dispatchScheduledItem` -> `Trigger::fire` (`Scheduler.cpp:373-378`).
- Processing thread (patched): `fireDueItems` -> same
  `dispatchScheduledItem` -> same `Trigger::fire` (`Scheduler.cpp:399-419`).

Both funnel into `Trigger::fire` (`Trigger.cpp:128-136`) ->
`MachineInstance::triggerFired` -> `setNeedsCheck`, and the loss happens in
`setNeedsCheck`'s coalescing early return (`MachineInstance.cpp:182-187`) plus
the unconditional erase in `checkStableStates` — neither of which the patches
touch.

This is corroborated by the revert experiment (Addendum 2): the defect still
fails **with both patches reverted**, so the wedge does not depend on them.

**Conclusion:** `3ce87578` is at most a timing/aggravation factor in the 2G-120
episode, not the cause. What is NOT established is the positive explanation for
why the analog stall correlated with that binary — that needs a plant-side
timing capture, not this test. Note also that these tests call
`checkStableStates` directly, so they do not exercise the busy-drain ordering at
all; they cannot resolve that question either way.

## Status vs objective

- Step 1 (prove the stall): **done** — machine stays unwedged-state, no revival
  path, state never advances.
- Step 2 (gate the fix): **done** — 25/25 CTest pass with the fix, but the fix
  **livelocks**, confirmed independent of the harness (with `idle()` draining the
  action path each pass, the machine sits permanently at
  `needsCheck=1 queued=1 actions=0`). The conditional erase alone is not a
  correct fix.
- Step 3 (other line): **scoped, not made faithful** — recorded in the test's
  SCOPE comment; that line needs an activation-based harness.
- Step 4 (2G-120): **answered** — patches are not implicated (above).

**Next design question for the fix:** `RecoverOverdue` calls `setNeedsCheck`
whenever the matched rule TIMER is overdue, and for a hold that produces no state
change this re-creates the wake every pass (livelock). Rejecting the erase
therefore needs the re-check to be *cleared once served*, not merely retained.

> **Superseded — see Addendum 10.** The livelock above is real but is not an
> argument against retaining the wake: it is the missing second half of the fix.
> `c6ebcb6b` (10.7) bounds recovery to one follow-up per **absolute deadline**,
> which is exactly "cleared once served", and it works — `test_timer_wake` passes
> on mqtt and still fails on elc. The current state of play, the elc gaps, and the
> `max_time` correction are all in Addendum 10; read 10.1, 10.7, 10.8 and 10.9
> before acting on anything above this line.


---

## Addendum 5 — the fix is probably safe after all; my repro was the problem

Addendum 3's "the fix livelocks" conclusion was drawn from a **degenerate rule**
(`held WHEN TIMER > 1`: the holding state is also the target state, with no due
rule to transition to). Real soft clocks use a due/hold pair. Re-tested against a
realistic pair (`on` holds while `TIMER < 1`; `off` when `SELF IS on && TIMER >= 1`):

| Build | degenerate rule | realistic due/hold rule | Full CTest |
|---|---|---|---|
| `3ce87578` base | FAIL | **PASS** | — |
| + conditional-erase fix | PASS | **PASS** | **25/25 PASS** |

So:
- The fix does **not** livelock with a realistic clock rule, and causes no
  regressions. It is not the dangerous patch Addendum 3 implied.
- **But the base already passes the realistic case**, because the false-rule scan
  arms a future wake and re-evaluation finds the due state true. The discarded
  wake costs at most one poll interval, not a stall.

**This is the important caveat: I still have no realistic reproduction that
stalls on the base.** The two failing cases in `test_timer_wake` fail because the
degenerate rule has no re-arm path; that is what makes the discard fatal. In
production the equivalent of "no re-arm path" is a clock whose rules are all
matched/overdue with no future arm — possible (the Aug 5 PIDLISTCLOCK freeze was
real) but not yet reproduced here.

**Consequence for landing:** committing `test_timer_wake` as-is would add a test
that fails on base for a rule shape no plant uses. Before either the test or the
fix lands, the missing work is a reproduction that uses production rule shapes
*due/hold with the arms suppressed* to show the discard actually wedges a clock.
Until that exists, the fix is unvalidated against the real failure mode and the
failing tests are not trustworthy coverage.

**Both lines carry the defect:** `feature/iod-elc-kernel-transport`
(`checkStableStates` ~1822-1835) has the same drop-through erase, with an extra
`steps`/`keep_pending` structure and its own "Bound stops flip-flop livelock"
comment. Nothing there rejects a re-check requested during evaluation.


---

## Addendum 6 — verdict: real code defect, no demonstrated production impact

After building a faithful reproduction that honours the processing loop's actual
selection rule (a machine is evaluated only while `queuedForStableStateTest()`):

| Scenario | base `3ce87578` | + conditional-erase fix |
|---|---|---|
| due/hold clock (real shape: hold `TIMER < rate`, due `TIMER >= rate`) | **reaches due state** | reaches due state |
| degenerate rule (holding state == target state, condition true while overdue) | drops out of the work set | not evaluated to a stall |

Why the real shape is safe: when the due item's wake is consumed, the evaluating
pass scans the states, finds the due rule true, and queues the transition via
`SetStateAction`. The clock leaves its hold on the *first* evaluation, so the
discarded re-check costs one evaluation rather than wedging anything.

The degenerate shape does stall, because the re-check is the only thing that
would have re-queued it — but that shape is not a clock. Real clocks alternate
on/off, so the hold condition goes false at the threshold and the due rule takes
over.

**Conclusion:** the code path is genuinely defective (a re-check requested during
evaluation is discarded), and on both lines. But no production-shaped
reproduction has shown it wedging a real clock, and the fix changes nothing for
real clocks. Landing it would be a change without a demonstrated defect.

This does NOT explain the Aug 5 PIDLISTCLOCK freeze or the Aug 12 recurrence.
Those notes record that the LPC re-arm fix was live at the time and the residual
failure was a check evaluated more than ~2 ms late; that is a different
mechanism (lateness, not re-check discard) and remains the better explanation for
the field incidents.

Committed on `prod-experimental-mqtt-fix`: `41f68f93` — the due/hold progress
guard and its scope notes. No product code change on either line.


---

## Addendum 7 — lateness mechanism: mostly hardened, plus a diagnostic trap

Investigated `scheduleTimerEvents` (`Expression.cpp`) and the SSTimer arming in
`setState` for the Aug 5 / Aug 12 field signature. Evidence, current code @3ce87578:

### The two arming sites have different conditions and thresholds

| Site | Arms when | Silent when |
|---|---|---|
| Predicate Timer (`Expression.cpp:496-526`) | `t = (scheduled_time - current_time) * 1000 > 0` | `t <= 0` — calls `setNeedsCheck` **only** under `RecoverOverdue` (`:520-525`) |
| SSTimer (`MachineInstance.cpp:2366-2373`) | `timer_val > 0` | `timer_val < -2` — arms nothing and activates nothing; `-2..0` activates. NOTE: read from source only; unlike the Predicate-Timer row this branch was **not** probed with a test, so treat its practical reachability as unverified. |

Four `scheduleTimerEvents` call sites, three policies:

| Call site | Policy |
|---|---|
| `MachineInstance.cpp:3685` — false-rule scan | `ArmFutureOnly` (implicit) |
| `MachineInstance.cpp:3653` — active subcondition not satisfied | `RecoverOverdue` |
| `MachineInstance.cpp:3663` — matched holding rule | `RecoverOverdue` |
| `SetStateAction.cpp:345` | `RecoverOverdue` — but **dead code** (`stable_state_xref` is never populated, so `isStableState()` is always false) |

**So the matched-hold and subcondition paths are covered**: the `RecoverOverdue`
calls added by `4e7ec4ba` do recover an overdue check. The lateness hole the
2026-08-12 note described has been addressed for the paths that matter.

**The remaining hole is `ArmFutureOnly`**: an overdue clause reached while
scanning a *false* rule arms nothing and requests nothing. Demonstrated in
`iod/tests/test_timer_lateness.cpp`:

```
LATEGAP: requested=0 scheduled=0 queued=1 state=idle
```

No wake is produced by the evaluation. Reaching this harmfully requires a clause
that is the only thing that could wake the machine, which for an overdue clause
means the predicate is already satisfied — i.e. it should have transitioned that
same evaluation. I could **not** construct a case where this gap alone wedges a
machine that a real clock uses.

### Diagnostic trap (real defect, misleading evidence)

`clockwork.cpp:1049-1062`:

```cpp
bool subcond_uses_timer = false;      // declared
if (ss.subcondition_handlers) { ... ch.uses_timer = true; ... }   // ch set, never accumulated
if (subcond_uses_timer || ss.uses_timer) {   // subcond_uses_timer is ALWAYS false
```

`subcond_uses_timer` is never assigned. `MachineInstance::uses_timer` therefore
does **not** get set for a machine whose only timers live in subconditions. The
field is not used for scheduling (only debug output at `MachineInstance.cpp:984`
and `:1809`), so this is not itself the stall — but it means `SHOW MACHINE` /
debug output can report **"Uses Timer: no"** for a machine that does have timer
subconditions. Anyone diagnosing a stuck clock from that field would be misled.

### Conclusion

The lateness mechanism is largely fixed. No production-shaped repro of a lateness
stall was found; the false-rule `ArmFutureOnly` gap is real but not demonstrably
harmful for a clock that transitions. The best remaining explanation for the
field residual is the **class-B whole-loop starvation** already recorded in the
site notes (Aug 12: ~1.39 s with no CW/`calcAdjust` at all), against which no
arming fix can help. The `subcond_uses_timer` dead assignment should be fixed
regardless, because it corrupts the diagnostic used to investigate these stalls.


---

## Addendum 8 — fix 1 landed (diagnostic), and the starvation lead

### Fix 1: `subcond_uses_timer` accumulation

`clockwork.cpp:1049-1062` declared `subcond_uses_timer = false`, never assigned it,
then read it to set `MachineInstance::uses_timer`. Fixed by setting it when a
subcondition handler's predicate uses a timer.

Verified behaviour-free: every read of `MachineInstance::uses_timer` is debug
output only (`MachineInstance.cpp:984`, `:1809`); the scheduling decisions use
the per-state `StableState::uses_timer` (`:2279`, `:3658`, `:3678`) and the
per-handler `ConditionHandler::uses_timer` (`:1937`), both set independently of
this flag. Full suite **26/26 pass** with the change.

Effect: `SHOW MACHINE` no longer reports "no timer" for a machine whose only
timers live in subconditions. No test was added — exercising it needs a parsed
program with a timer subcondition and a full `semantic_analysis()` pass, which
would need more scaffolding than the one-line fix justifies. The change is
self-evident from the code; testability is noted as a gap rather than papered
over.

### Starvation lead (class-B whole-loop gap)

Established from source on `3ce87578`:

- The main poll is **bounded**: `pollZMQItems` (`ProcessingThread.cpp:273-286`)
  clamps the timeout to >= 1 ms and returns on idle, so the poll itself cannot
  produce a 1.39 s hole. Something *inside* a loop iteration must block.
- The global `IOComponent` lock is taken **only** by the processing thread
  (`ProcessingThread.cpp:296`, `:608`, `:832`, `:1543` — the only
  `IOLockHelper`/`IOComponent::lock()` users in `iod/src`). No cross-thread
  contention is possible, so it is not the stall.
- **Prime suspect: synchronous command/channel work on the processing thread.**
  `Channel::handleChannels()` runs in-line at the `StageChannelsCommands`
  marker (`:1589`), and inbound ZMQ commands are parsed and executed in the same
  poll loop, up to 32 messages per iteration (`:1640-1668`,
  `IODCommand *command = parseCommandString(buf)`). Any handler that blocks or
  is slow stalls the whole control loop — which matches the field signature
  (EtherCAT keeps moving, no CW/`calcAdjust` at all).

What I did **not** establish: which handler actually blocked on Aug 12. That
needs a field capture, not more source reading.

**Concrete next step:** enable STALLSNAP on the affected box and read the stage
from the next gap. The existing trace already names the stage
(`StageChannelsCommands`, `StageScheduler`, `StageEcatHandle`, …; see
`StallTrace.h:22-33` and the `markStage` sites listed at
`ProcessingThread.cpp:607,830,1176,1476,1589,1758,1782,1787,1881`), so one
occurrence will discriminate between the candidates instead of guessing.


---

## Addendum 9 — 2G-120 STALLSNAP capture, 2026-09-16 (field data)

Read via `/Users/mike/src/RemoteAdminLinux/dsh-plant` from `journalctl` on
`2G-120` (SNAP goes to stderr -> syslog; `/tmp/iod.log` is empty because
`/tmp/iod-verbose` is absent).

Two distinct stall families appear. **Startup transient** (~5 occurrences, all
at iod start, `stage=outer`):

```
STALLSNAP duration_us=9004632 stage=outer stage_enter_us=702056 machine=-
  runnable=0 stable=0 exec=0 mail=0
  ring=[outer@702056, zmq_poll@9693256, outer@9693311, channels_cmd@9700584]
```

~9.0 s at startup with an empty machine set, bounded by the
`inproc:// monitor started` line at `21:14:20` and the first
`---- Plugin in use:` at `21:14:29`. This is not a wedge — it is startup.
(Exact correlation to the iod start line not yet verified.)

**Production stalls** — the ones that matter, all `stage=poll_machines`, with
large machine sets and ~150-400 ms durations:

```
duration_us=149490 machine=F_Grabbed10   runnable=843 stable=571 exec=3 mail=2 events=843
  ring=[...,poll_machines@90247061, stable@90378719, outer@90379086,...]
  -> stable stage consumed 131.7 ms

duration_us=390719 machine=P_CoreOutputImage runnable=1 stable=0 exec=1
  ring=[...,poll_machines@11552892, stable@11933093]
  -> stable stage consumed 380.2 ms

duration_us=266280 machine=I_GrabRestuffLeftUp runnable=2 stable=0 exec=2
duration_us=231172 machine=G_GrabRestuffDown    runnable=2 stable=1 exec=1
duration_us=196471 machine=F_Grabbed04          runnable=1 stable=0 exec=1
duration_us=187635 machine=V_GrabMotorRunTimeTenHours runnable=1 stable=0 exec=0
duration_us=187108 machine=R_GrabIndexRetract   runnable=2 stable=0 exec=2
duration_us=122511 machine=FixModulesList       runnable=2 stable=0 exec=2
```

### What this establishes

**Ring semantics (verified in source, `StallTrace.cpp:264-276`):** `markStage`
pushes a ring entry ONLY when the stage *changes*; `writeHeartbeat` runs while
the stage is unchanged. So a gap between consecutive ring entries is time spent
in the EARLIER stage. `stage=` is the stage entered at `stage_enter_us` where
the heartbeat then stopped.

`StagePollMachines` is marked immediately *before* `poll_machines()`
(`ProcessingThread.cpp:1781-1783`) and `StageStableStates` immediately before
the stable-state block (`:1787-1789`). So for the worst record:

```
poll_machines@90247061 -> stable@90378719   = 131.7 ms inside poll_machines()
stable@90378719        -> outer@90379086    = 0.4 ms inside stable
```

**The time is consumed in `poll_machines()`, not in the stable-state stage.**
Same shape in the others: `poll_machines@11552892 -> stable@11933093` = 380.2 ms
inside `poll_machines()` with `stable=0 runnable=1 exec=1`.

1. `poll_machines()` calls
   `MachineInstance::processAll(to_process, 150000, NO_BUILTINS)`
   (`ProcessingThread.cpp:667`). It is given a 150 ms budget and observed to run
   **150-390 ms** — overrunning its own budget by up to 2.6x.
2. The heavy record runs with `runnable=843 ... events=843, pend_ev=28,
   stable=571` — a mass machine activation, not a timer problem.
3. `poll_machines()` selects machines that are `executingCommand()`, have
   pending events, or have mail (`:650`) and runs their commands/actions on the
   **processing thread**. This is synchronous command/event work blocking the
   control loop, which is the mechanism Addendum 8 flagged from source reading.
4. Against `C_ClockFreq` at 20 ms and `C_ClockPosition` at 50 ms, a 150-390 ms
   hole misses several clock edges — the enable-on / analog-0 signature.

**This supersedes the TIMER-arming and wake-discard mechanisms as the
explanation for the field stalls.** A correctly armed wake still cannot fire
while the processing thread is inside `poll_machines()` for up to 390 ms.

### Open

- `start_us`/`recover_us` are µs since process start; mapping to wall clock to
  bracket the reported incident window is not yet done.
- **Trigger for the `runnable=843` burst: operator reports it was the
  Idle -> Manual -> Auto transition** (operator knowledge, 2026-09-16).
  Consistent with the code: `enable()` calls `setNeedsCheck()`
  (`MachineInstance.cpp:4013`), and `setNeedsCheck` re-queues plus cascades to
  dependents via `propagateNeedsCheckToDependents` (`:427-433`). There is **no
  bulk-enable helper** in `iod/src` — the wave must come from that cascade, so a
  plant-wide mode change propagating through the dependency graph is a
  plausible single-shot cause of `runnable=843 events=843 pend_ev=28
  stable=571`. NOT yet verified by timestamp correlation: the 843 record is at
  `start_us=90247061` in its process, and mapping that to a wall-clock Manual->
  Auto edge has not been done.
- The ~9.0 s `stage=outer` records at process start are a startup transient, not
  a production wedge.
- Two core dumps exist in `/etc/service/iod`: three from 2026-08-18 and one from
  2026-09-15. Unexamined.

**Correction (Addendum 10):** item 1 above ("given a 150 ms budget … overrunning
its own budget by up to 2.6x") is wrong — `max_time` is a dead parameter on both
lines, so there is no budget and no overrun. The conclusion (a single
`poll_machines()` pass blocks the loop for the whole batch) stands and is in fact
stronger: the pass is unbounded by construction. See Addendum 10.


---

## Addendum 10 — corrections from a re-read of both lines, and a trap to avoid

Written 2026-09-16 (second session, same day). Nothing below is a field result:
10.1–10.3 are source findings (with targeted runtime instrumentation on the elc
line and `cw`), 10.4 is a correction to a mistake made earlier in this same
session, and two of the findings invalidate conclusions earlier in this document.
Line anchors are given as greppable symbols to avoid the cross-line drift problem
the scope note at the top warns about.

### 10.1 The 150 ms "budget" does not exist — `max_time` is dead on both lines

Addendum 9 concluded the stall is a budget overrun (`150000` µs given, 390 ms
observed). There is no budget. The parameter is vestigial:

| Line | `MachineInstance::processAll` | `MachineInstance::checkStableStates` |
|---|---|---|
| `prod-experimental-mqtt-fix` | only use of `max_time` is inside `#if 0` (`:1402`) | `// Warning: max_time is ignored in this method` (`:1454`, signature `:1456`) |
| `feature/iod-elc-kernel-transport` | same, only use inside `#if 0` (`:1731`) | same warning (`:1783`, signature `:1785`) |

Both call sites pass `150000` (`ProcessingThread::poll_machines`, and
`handle_machines.cpp:41`/`:69`).

Consequences:

- A 380 ms `poll_machines()` pass is **not** an overrun; it is simply how long the
  batch took. Nothing was violated, and no deadline was missed by the code's own
  terms — because no deadline is checked.
- Therefore the fix direction is not "make the budget stick". It is "bound the
  batch", e.g. checkpoint the deadline in the select loop and carry the remainder
  into the next iteration. That is a design change, not a bug fix, and it is the
  only thing that would shorten the 150–390 ms holes.
- The two field sub-cases in Addendum 9 are mechanically different and should not
  be described together:
  - `runnable=843 … stable=571` — the *sum* of per-machine work across a large
    activation wave (a cascade from `propagateNeedsCheckToDependents`); no single
    machine is slow.
  - `runnable=1 stable=0 exec=1 … machine=P_CoreOutputImage`, 380 ms — the whole
    hole is **one** machine's `idle()`/command work. That is the case that most
    resembles a single blocking handler, and it is the one worth chasing; a batch
    bound alone would only cap it, not explain it.

### 10.2 `feature/iod-elc-kernel-transport` is missing `0e163446`

§2 of this document records `0e163446` as "correct and independent … Verified:
the copy is gone". That verification holds on the mqtt line only. The elc line
never received the commit, and still builds the SSTimer on a stack copy in
`MachineInstance::setState`:

```cpp
if (earliestTimerState) {
    StableState s(*earliestTimerState);          // elc MachineInstance.cpp:2732
    ...
    s.trigger = new Trigger(this, trigger_name);
    Scheduler::instance()->add(
        new ScheduledItem(stable_state_timer_base, timer_val * 1000, s.trigger));
}
```

The mqtt version of the same block has no `s` at all and names
`earliestTimerState->` (`MachineInstance.cpp:2367` on the pre-fast-forward
worktree; `:2373` at `c6ebcb6b`). Confirmed by direct comparison of both
worktrees and by `git log -S "StableState s(*earliestTimerState)"`, which shows
`0e163446` as the only commit that removes it.

Traced through the real objects in `cw` on the elc line, with a real parsed
TIMER program (`tests/timer.cw`, `on WHEN SELF IS off AND TIMER > 1000`):

1. The arm block is genuinely reached. Instrumenting the site showed
   `machine=test state=on uses_timer=1 earliest=1000` on every evaluation — so
   this is live code, not a dead branch (see 10.4 for the log trap that made it
   look dead).
2. At the scheduler's firing path, the SSTimer item is present but its trigger is
   already dead: a breakpoint at the `item->trigger->enabled()` guard
   (`Scheduler.cpp:529` inside `Scheduler::idle`; mqtt has the same guard in
   `dispatchScheduledItem`, `Scheduler.cpp:374`) shows `is_active = false`,
   `name = "SSTimer test on"`.
3. The `StableState` copy's destructor is the only mechanism in the arm block
   that can disable it (`StableState::~StableState` → `trigger->disable()`).

**Not resolved directly by the above**, but settled later in 10.8 by a better
probe: the machine still cycles on this line, and re-arming disables superseded
triggers by design, so observing *some* disabled `SSTimer …` item at the firing
path does not prove it was the arm block's item. 10.8 builds a minimal machine
where there is only one such item and shows the trigger is disabled at arming
time, which is the unambiguous result. Treat the trace in this section as
corroboration, not proof.

**Backport `0e163446` to elc regardless.** It is a one-block change, it is already
reviewed and landed on the other line, and leaving the two lines divergent on
trigger ownership is exactly the kind of difference that makes cross-line
conclusions wrong.

### 10.3 `StableState`'s copy constructor drops `timer_val` (latent, not behavioural)

`StableState::StableState(const StableState &other)` initializes
`timer_val(0)` (`StableState.cpp:25`, identical on both lines) while copying
`uses_timer` from the source. Nothing re-derives `timer_val` after a copy, and
`MachineInstance::setStateMachine` populates an instance's `stable_states` by
copying the class's. So an instance's `StableState::timer_val` can be 0 even
though the class's is not.

Measured in `cw` with a temporary print in the copy constructor (since reverted):

```
SSCOPY state=on uses_timer=1 other.timer_val=1000 copied=0
```

Consequence — **none today, and this is why the misreading matters**: the arm
branch reads `s.timer_val` only for the `t_symbol`/`t_integer` selection, and
uses the loop's own `earliestTimer` for the `timer_val > 0` test. Instrumentation
at the arm site confirms the correct value survives:

```
SSARM machine=test state=on uses_timer=1 s.timer_val=0 earliest=1000
```

The copy's `timer_val` is 0, the value actually used is 1000, and the branch
takes the arm path. So the copy losing `timer_val` is a latent wart, not a
behavioural defect, and it does **not** gate the arm. (It did, however, gate my
probe: a hand-built `MachineClass` gets its value zeroed when the temporary is
copied into the vector, which is why the probe's `setState` armed nothing. Do not
build TIMER probes by hand-constructing `StableState` values.)

### 10.4 Investigation trap: `DBG_M_SCHEDULER` is per-machine, so its absence proves nothing

An earlier pass at this addendum concluded from log inspection that the
`timer_val > 0` arm was dead, because the message

```cpp
DBG_M_SCHEDULER << _name << " Scheduling timer for " << timer_val << "ms\n";
```

never appeared in `cw -l - -c iod.conf` output even though the machine armed an
`SSTimer` item with a 1000 ms delay. That conclusion was wrong. The macro is:

```cpp
#define M_MSG(l, m) if (!(m->debug() && LogState::instance()->includes((l)))) ; else ...
```

It is gated on **`MachineInstance::debug()`**, not just on the debug group. The
neighbouring `DBG_SCHEDULER` messages in `Scheduler` and `Scheduler::add` are
global and print fine, which is what made the omission look meaningful. An lldb
breakpoint on the line showed it executing with `timer_val = 1000`.

For any future session: verify scheduling paths with a breakpoint, or with
`MachineInstance::debug()` enabled for the target — do not infer execution from
`DBG_M_*` output.

### 10.5 Related review, and what it changes

`docs/timer-loss-review_martin.md` reviews this document independently. Its
substantive points, which this addendum accepts:

- **The core queue defect is real**, and `checkStableStates` erasing a
  `pending_state_change` entry created during the evaluation is the right
  diagnosis — but "erase unless `needs_check`" alone is unsafe.
- **The fix is two-sided.** `RecoverOverdue` re-requests on every evaluation of a
  still-overdue deadline (`TIMER >= N` stays overdue forever), so preserving every
  wake recreates the load storm recorded in Addendum 3. The fix needs both
  (a) preserve mid-evaluation wake requests in `checkStableStates`, and
  (b) one-shot recovery per **absolute deadline** (`start_time + threshold`, not
  just the threshold, so a new state entry or a changed threshold recovers again).
- **The terminal state is a runnable orphan, not an unreachable machine**: with a
  real `ProcessingThread`, `setNeedsCheck` re-inserts and activates, and
  `checkStableStates` erases only `pending_state_change`. So the machine is in
  `runnable` with `pending_state_change` absent. The `is_pending=0` phase-3 probe
  result fits a harness without an installed `ProcessingThread`.
- **An unrelated `setNeedsCheck` may not recover it**, because the coalescing
  early return fires on `is_pending || queuedForStableStateTest || active_actions
  || mail`. Note the elc line's guard has exactly that shape
  (`MachineInstance::setNeedsCheck`: `needs_check > 0 && (ProcessingThread::is_pending(this) || queuedForStableStateTest() || ...)`).
- **The diagnostic fingerprint should be** `runnable=1, pending_state_change=0,
  needs_check>0, and no Timer/SSTimer item for that machine` — not `runnable=0`.

Those points are consistent with Addenda 5 and 6 and do not revive the
"production impact proven" claim; they sharpen the fix.

### 10.6 Baseline refresh

Full `ctest` on `feature/iod-elc-kernel-transport` @`bafe98b7`, Release,
`RUN_TESTS=ON`: **103/104 pass** sequentially; **102/104** under `ctest -j8`. Both
shortfalls are environmental, not regressions:

- `test_two_dbd` — timing-sensitive two-`dbd` RECORD_APPLY
  (`"two dbd did not both RECORD_APPLY Ann"`); passed on two immediate re-runs.
- `runtime_try_body_completes`, `runtime_try_sync`, `runtime_try_immediate` —
  these bind `cw`'s client port 5555. A stray `cw` left listening on 5555 by an
  interrupted debugger session made every concurrent `cw` fail with
  `Error: trying port 5555: Address already in use` and fall through to
  5557–5559. After killing the stray process, all three pass. **Watch for this
  when running the elc suite**: a single leftover `cw` produces three unrelated
  failures that look like real breakage.

These replace the "25/25" figures quoted in Addenda 3/5/6, which refer to an older
target set. On the mqtt line at `c6ebcb6b` the suite is **27/27**, including the
new `test_timer_wake`.

Artifacts: scratch probe sources are untracked in the mqtt worktree
(`iod/tests/test_sstimer_arm_probe.cpp`, `test_sstimer_diag.cpp`, registered on
that line's test `CMakeLists.txt`). The elc tree was left clean — the temporary
probes and all instrumentation traces were reverted, and `git status` shows no
modified source files.
### 10.7 The queue fix landed on the mqtt line (`c6ebcb6b`) — and is still needed on elc

`origin/prod-experimental-mqtt-fix` advanced from `4692653e` to **`c6ebcb6b`**
("scope: iod-core: preserve bounded overdue TIMER wakes", Martin Leadbeater,
2026-09-16 21:13), implementing exactly the two-sided fix 10.5 argues for:

- `MachineInstance::checkStableStates` now computes
  `erase_pending = !changed_state && !mi->needsCheck()` and only erases when that
  holds — the caller stops discarding work created during evaluation.
- `Predicate` gained `has_recovered_overdue_deadline` /`recovered_overdue_deadline`;
  `scheduleTimerEvents` recovers a given absolute deadline
  (`timed_machine ? timed_machine : target` → `start_time + scheduled_time * 1000`)
  at most once, so preserving the wake cannot recreate the load storm. `operator=`
  resets both fields.
- New `iod/tests/test_timer_wake.cpp` with three cases: wake preserved during
  evaluation; one follow-up per overdue deadline; a new deadline can recover again.

Verified here on the mqtt worktree at `c6ebcb6b` (Release): all three
`TimerWakeTest` cases pass.

**The same test fails on elc**, which is the useful result: it shows the elc line
still has the discard and has *not* received the fix.

```
[ RUN      ] TimerWakeTest.PreservesWakeRequestedDuringStableStateEvaluation
  machine_->queuedForStableStateTest(): Actual: false  Expected: true   FAILED
[ RUN      ] TimerWakeTest.OverdueHoldingTimerQueuesOnlyOneFollowUpPerDeadline
  machine_->queuedForStableStateTest(): Actual: false  Expected: true   FAILED
[ RUN      ] TimerWakeTest.NewAbsoluteDeadlineCanRecoverAgain               OK
```

The third case passes because it calls `scheduleTimerEvents` directly and never
goes through `checkStableStates`. The two failures are the same defect this
document opened with, still present on elc — so the `RecoverOverdue` half of
Martin's change is not the only part elc needs; the caller half applies there too.
Note that elc's `checkStableStates` has a different shape (the `steps`/`keep_pending`
loop with its own "Bound stops flip-flop livelock" comment), so the elc port is a
real edit, not a cherry-pick.

### 10.8 SSTimer A/B probe: the stack copy is a live defect on elc

The hand-built probe of 10.3 was fixed (re-derive `timer_val` after the copy, as
a parsed program effectively has) and then run unchanged on both lines. It builds
a machine whose `on` rule is `SELF IS on AND TIMER < 5`, drives `setState` via
`enable()`, and asserts the SSTimer armed on entry is still enabled, then fires it
and asserts a re-check is requested.

| Observation | mqtt @`c6ebcb6b` | elc @`bafe98b7` |
|---|---|---|
| `live_trigger` (live `StableState::trigger`) | `SSTimer sstimer_probe on` | **`<null>`** |
| `live_enabled` | yes | no |
| item scheduled on entry | yes | yes |
| `sched_enabled` (the item's trigger) | **yes** | **no** |
| after firing the armed item: `needs_check` / `queued` | `1` / yes | **`0` / no** |
| test result | 2 × PASS | **2 × FAIL** |

So on elc the wake is armed and then dead: the trigger is disabled before it can
fire, firing it requests nothing, and the machine is left with an unhandled
overdue TIMER. On mqtt the same probe passes because the trigger is hung on the
live `StableState`. This closes the runtime question 10.2 left open, and it makes
the probe a valid gate for the `0e163446` backport: it fails on elc as-is and is
expected to pass once the stack copy is removed.

### 10.9 Open items handed forward

1. **Backport `0e163446` to elc** (10.2/10.8). Pass condition: the SSTimer A/B
   probe above goes from 2 FAIL to 2 PASS.
2. **Port the queue fix to elc** (10.7), adapting it to elc's `steps`/`keep_pending`
   loop rather than copying the mqtt block. Pass condition: `test_timer_wake`'s
   first two cases go from FAIL to PASS, and the full elc `ctest` stays green.
3. **Decide whether the `setState` SSTimer arm should exist at all.** Predicate
   TIMER wakes via `checkStableStates` are the path that drives clocks on both
   lines; the arm block duplicates a subset of that with weaker bookkeeping. If it
   is vestigial, delete it rather than keep repairing it — that also retires the
   `timer_val` wart in 10.3 and the `-2..0` fallback flagged in Addendum 7.
4. **Decide the `max_time` question deliberately** (10.1): wire the deadline up, or
   delete the parameter so the "budget" cannot mislead a third reader. The
   `runnable=1`, 380 ms `P_CoreOutputImage` record in Addendum 9 remains the field
   symptom most worth chasing, and it is a single-machine block, not a batch size.
5. **Re-check `3ce87578`** (busy-thread `fireDueItems`) now that the queue fix has
   landed: with the erase repaired and recovery bounded, the enabler argument in
   §2/Addendum 4 should be re-evaluated rather than left as "aggravator at most".

### 10.10 Scratch artifacts and tree state

The elc tree is clean: both temporary probes, every instrumentation trace
(`SSCOPY`/`SSARM`/`SSSEL`/`TRIGDIS`), and the `CMakeLists.txt` registrations were
reverted; `git status` shows no modified source files. Build directories
`_build-elc/` and `_ccache/` are now ignored via `.gitignore`.

Untracked scratch retained in the mqtt worktree (its `CMakeLists.txt` differs from
`HEAD` only by these two registrations):

- `iod/tests/test_sstimer_arm_probe.cpp` — the 10.8 A/B gate.
- `iod/tests/test_sstimer_diag.cpp` — the 10.3 `timer_val` demonstration.

Also note the local mqtt worktree was fast-forwarded to `c6ebcb6b`; the `cw`
binaries used for the log captures came from `_build-elc/` (elc) and
`build/` (mqtt) and are not installed anywhere.


---

## Addendum 11 — "should we revert the two 2G-115 patches?" — No

Asked 2026-09-16. The question comes from reading Addendum 2/4 correctly but
drawing the wrong conclusion from them: those addenda show the patches are **not
the cause** of the wake loss. That is a statement about causality, not about
removability, and the two must not be run together. Neither patch should be
reverted, and here is the specific cost of each if it were.

### `0e163446` (SSTimer on the live `StableState`) — required, verified

This is not a mitigation awaiting proof; it is the fix for a defect reproduced in
both directions (Addendum 10.8). With the stack copy restored, the trigger the
entry just armed is disabled by `~StableState` before it can fire, and firing it
requests nothing. The probe result on elc — which still has the copy — is
`live_trigger=<null>`, `sched_enabled=no`, and `needs_check=0` after the fire.

Reverting it would also reintroduce a hazard the current code does not have: the
stack copy's destructor does `s.trigger->release()` on a pointer the
`ScheduledItem` has already retained, leaving the live `StableState::trigger` a
stale, doubly-released pointer for the next state entry to overwrite. Keeping the
trigger on the live state removes both problems.

**Action: keep on A, and port to B** (still open, Addendum 10.9 item 1).

### `3ce87578` (busy-pass `fireDueItems`) — required, not an aggravator to drop

The doc's "enabler/aggravator at most" phrasing undersells this commit. Reading
the full diff (it is larger than §2 implies: it introduces
`Scheduler::fireDueItems` and `dispatchScheduledItem`, and converts the
scheduler thread's drain loop into a single call), the load-bearing part is that
it is the **only** path that fires due TIMERs while the processing thread is
busy:

- The scheduler thread fires only inside `Scheduler::idle`'s `e_running` block,
  which it enters only after a handshake (`"sched"` → `"continue"`).
- The processing thread answers that handshake only when `processing_state ==
  eIdle` — `service_scheduler_in_wait` returns early otherwise
  (`ProcessingThread.cpp:1296-1298`), and the outer `e_waiting` path has the same
  guard (`:1730`, `if (status == e_waiting && processing_state == eIdle)`).
- `poll_machines`/`checkStableStates` are exactly the region where
  `processing_state != eIdle`.

So without `3ce87578`, a wake armed during a busy pass waits for the whole pass to
complete. That is the 150–390 ms hole measured in Addendum 9 — the same mechanism
the document identifies as the field signature, with `C_ClockFreq` at 20 ms and
`C_ClockPosition` at 50 ms missing several edges inside it. Removing this patch
makes the lateness worse, not better, and it is also the only thing that drains
items with sub-`machine_check_delay` latency during that window.

It is fair to call it an aggravator *of the erase defect*, because firing a wake
mid-pass is what puts the wake in front of `checkStableStates` before the pass
ends. But that interaction is now fixed at the other end by `c6ebcb6b`
(Addendum 10.7): the processed entry survives because `erase_pending` is false.
The two commits are complementary now, not in tension.

**Action: keep on A, and port the busy-pass drain to B with `scope: iod-core`.**
Note that B already has a busy-pass `due_n`/`last_due` drain structure in its
`poll_machines`/`checkStableStates`, so the port is a semantics comparison, not a
patch application — confirm B actually reaches an equivalent `fireDueItems` and
that the "do not open the scheduler handshake here" rule holds there too.

### What would justify reverting either

Only a demonstrated regression attributable to it. There is none: the mqtt line
at `c6ebcb6b` with **both patches plus** the queue fix is 27/27 green, and the
`test_timer_wake` acceptance gate passes. The 2G-120 correlation that motivated
blaming `3ce87578` remains unisolated (Addendum 4, and the 2G-120 bounce in
Addendum 9's "Open"), but the correct way to settle that is a plant-side A/B with
the queue fix now in place — not a speculative revert that reintroduces two known
costs in exchange for nothing.

**Summary position:** the two patches are the wrong thing to blame and the wrong
thing to remove. The defect was the unconditional erase in `checkStableStates` and
the unbounded `RecoverOverdue`; both are now fixed on line A. The remaining real
gaps are line B's missing backports (10.9 items 1 and 2) and the unbounded
busy-pass batch (10.1), none of which reverting these commits would help.


---

## Addendum 12 — line B state check (2026-09-16, after `b333e8f8`)

Direct check of `feature/iod-elc-kernel-transport` @`bafe98b7` (build `_build-elc`,
Release, `RUN_TESTS=ON`). Confirms the two open items and closes the "is B
structurally different?" question.

### Feature inventory: B has only one of the five TIMER commits

`git merge-base --is-ancestor` against B's HEAD:

| Commit | In B? | What it does |
|---|---|---|
| `bafe98b7` accum. `subcond_uses_timer` | **yes** (same change as A's `4692653e`) | diagnostic flag only |
| `0e163446` live `StableState` trigger | no | fixes the disabled SSTimer arm |
| `3ce87578` busy-pass `fireDueItems` | no | fires due TIMERs while processing is busy |
| `41f68f93` due/hold progress guard test | no | A-side test only |
| `c6ebcb6b` bounded overdue wakes | no | the queue fix from Addendum 10.7 |

B's `Scheduler.cpp` contains **no `fireDueItems` and no `dispatchScheduledItem`**;
its scheduler still drains inline in `Scheduler::idle`'s
`while (state == e_running && is_ready)` loop, and the handshake is still refused
unless `processing_state == eIdle` (`ProcessingThread.cpp:1374`, `:1550`, `:1791`).
So B has the same structural gap `3ce87578` closed on A, and it is the only line
that has never had busy-pass TIMER firing.

### Both gates fail on B, as predicted

Built with the A-side probes registered temporarily, then removed:

**Gate 1 — SSTimer arm (`0e163446`):**

```
DIAG live-state uses_timer=1 timer_val=5
SSTIMER-PROBE live_trigger=<null> live_enabled=no scheduled=yes sched_enabled=no
SSTIMER-PROBE after-fire needs_check=0 queued=no
[  FAILED  ] SSTimerArm.TriggerArmedOnStateEntryIsStillFireable
[  FAILED  ] SSTimerArm.FiringTheArmedWakeRequestsARecheck
```

**Gate 2 — `test_timer_wake` (queue fix):**

```
[ RUN      ] TimerWakeTest.PreservesWakeRequestedDuringStableStateEvaluation
  machine_->queuedForStableStateTest(): Actual: false  Expected: true   FAILED
[ RUN      ] TimerWakeTest.OverdueHoldingTimerQueuesOnlyOneFollowUpPerDeadline
  machine_->queuedForStableStateTest(): Actual: false  Expected: true   FAILED
[ RUN      ] TimerWakeTest.NewAbsoluteDeadlineCanRecoverAgain               OK
```

Same results as Addendum 10.7/10.8, so B has no accidental partial fix.

### Port caution: B already has its own overdue-recovery plumbing

B is **not** missing `TimerOverduePolicy`. `78f62e0e` (2026-08-25,
"mask DIGITALVALUE idle wakeups and load-safe TIMER recovery") already ported
`9acb2656`/`4fb15794`/`4e7ec4ba`/`2f13765d` and added `TimerOverduePolicy`,
`RecoverOverdue` on matched holds, and `ArmFutureOnly` on false-rule scans —
the same surface `c6ebcb6b` modifies on A. The part B lacks is only the one-shot
bounding: B's `RecoverOverdue` re-requests on every evaluation of a still-overdue
deadline, which is the load-storm risk Addendum 10.5 describes.

So the `c6ebcb6b` port to B is **smaller and more delicate** than a cherry-pick:

1. add `has_recovered_overdue_deadline`/`recovered_overdue_deadline` to B's
   `Predicate` and bound the `RecoverOverdue` branch in `scheduleTimerEvents`
   (the deadline arithmetic is the same: `start_time + scheduled_time * 1000`);
2. repair B's `checkStableStates` — **not** by copying A's block, because B's is
   the `steps`/`keep_pending` loop. In B the erase at the end runs when
   `!keep_pending`, and the "machine has other work" branch erases then
   activates. The fix must preserve the entry when a wake was requested during
   `setStableState()` while keeping B's 32-step bound and its
   `SharedWorkSet`/`activate` behaviour intact;
3. do **not** reset the recovered-deadline fields in B's copy/assign paths
   without checking them — `c6ebcb6b` added that to A's `Predicate::operator=`,
   and B's `Predicate` copy/assign surface differs.

Gate for the port: `test_timer_wake`'s first two cases must pass on B and the
`SSTimerArm` probe must still be reviewed against the `0e163446` backport
(item 1), then the full B suite must stay green.

### Baseline

Full sequential `ctest` on B, with no product change beyond the doc commit:
**104/104 pass** (197 s). This is the clean baseline for the port. Note it
supersedes the 103/104 figure in Addendum 10.6 — `test_two_dbd` passes when run
sequentially, confirming it as a load-sensitive flake.

The B tree was left clean after the check: both temporary probe files and the
`CMakeLists.txt` registrations were removed; `git status` shows no modifications.
