# Review of `doc/TIMER_LOSS_INVESTIGATION.md`

## Overall assessment

The document identifies a **real queue-management defect**, but its root-cause explanation is incomplete and several claimed runtime consequences are incorrect.

The most important valid finding is:

> `setStableState()` can request another evaluation by calling `setNeedsCheck()`, but `checkStableStates()` can erase the newly inserted `pending_state_change` entry after `setStableState()` returns.

That is a genuine lost-work bug.

However, the proposed fix—always retaining the entry when `needs_check != 0`—is unsafe by itself. `RecoverOverdue` currently requests another check every time it sees the same overdue deadline. If that request is always preserved, a matched overdue rule such as `TIMER >= N` can run continuously. Repository history shows that blanket overdue recovery previously caused enable/load storms.

The correct repair therefore requires **two coordinated changes**:

1. Preserve legitimate wake requests made during evaluation.
2. Limit overdue TIMER recovery to one follow-up per absolute deadline.

---

## What the investigation gets right

### 1. The caller can erase work created by its callee

The core sequence is correct:

1. `checkStableStates()` evaluates a machine.
2. `setStableState()` clears `needs_check`.
3. Something during evaluation calls `setNeedsCheck()`.
4. That call restores `needs_check`, inserts the machine into `pending_state_change`, and activates it.
5. `setStableState()` returns `false` because no state transition was requested.
6. `checkStableStates()` unconditionally erases the machine from `pending_state_change`.

The original code effectively treats:

setStableState() == false

as equivalent to:

no work was generated during evaluation

Those are not equivalent. A stable evaluation may leave the current state unchanged while still generating a legitimate request for another pass.

This is not exclusively a TIMER problem. Any `setNeedsCheck()` made during stable-state evaluation could be lost in this way.

### 2. `needs_check` alone is not enough

The document is correct that a nonzero `needs_check` counter does not make the machine selectable.

Stable-state collection requires the machine to be:

- present in the processing thread’s runnable set; and
- present in `pending_state_change`, as reported by `queuedForStableStateTest()`.

If the stable-state membership is erased, the next stable-state scan skips the machine.

### 3. `3ce87578` exposes the interaction more readily

Calling `fireDueItems()` from the processing thread while processing is busy makes it possible for a due TIMER to be consumed within the same broader processing pass in which stable-state evaluation and queue cleanup occur.

It is reasonable to describe that change as an enabler or aggravator rather than the origin of the queue-erasure bug.

### 4. The live `StableState` trigger repair is independent

The description of `0e163446` is sound. Keeping the trigger on the live `StableState` avoids the temporary copy’s destructor disabling the scheduled trigger. That is a separate lost-trigger defect.

---

## Corrections required

### 1. The final queue state is described incorrectly

The document says the terminal state is:

needs_check != 0
pending_state_change absent
runnable absent
scheduled item absent

With a real initialized `ProcessingThread`, the reproduced state is instead:

needs_check != 0
pending_state_change absent
runnable present
scheduled item absent

The reason is:

1. `setStableState()` calls `ProcessingThread::suspend(this)` at its start.
2. `RecoverOverdue` later calls `setNeedsCheck()`.
3. `setNeedsCheck()` reinserts the stable-state entry and calls `ProcessingThread::activate(this)`.
4. `checkStableStates()` erases only `pending_state_change`; it does not suspend the machine again.

The machine is therefore a **runnable orphan**: it is runnable, but stable-state selection skips it because `queuedForStableStateTest()` is false.

The phase-3 probe result claiming `is_pending=0` likely ran without a real installed `ProcessingThread`, in which case `ProcessingThread::activate()` is a no-op. It did demonstrate the pending-entry erase, but it did not reproduce the production runnable state.

### 2. An unrelated `setNeedsCheck()` may not recover the machine

The document says the machine remains stuck until an unrelated event calls `setNeedsCheck()`.

That is too optimistic.

In the runnable-orphan state, a later call sees:

needs_check > 0 && ProcessingThread::is_pending(this)

and takes the coalescing early return. It increments `needs_check`, but it does not restore `pending_state_change`.

Therefore, an ordinary unrelated wake may not repair the machine. Recovery requires something that reconstructs stable-state membership, creates executable work, changes state, or otherwise clears the orphaned condition.

### 3. The proposed fix is incomplete and potentially dangerous

The document proposes:

> If `needs_check` is nonzero after evaluation, retain `pending_state_change`.

That is necessary for general mid-evaluation wake correctness, but it is unsafe with the current implementation of `RecoverOverdue`.

An overdue matched predicate such as:

TIMER >= N

remains overdue on every later evaluation. Current `RecoverOverdue` calls `setNeedsCheck()` each time it sees that condition. If every such request survives, the machine follows this loop:

evaluate
→ same deadline is overdue
→ setNeedsCheck
→ preserve pending entry
→ evaluate again
→ same deadline is overdue
→ ...

This recreates the TIMER processing storm that earlier recovery changes caused. The history around `6c8f6545`, its revert `7ada2a6d`, and the later load-safe recovery change documents this risk.

The fix must therefore address both the caller erase and repeated recovery of the same overdue deadline.

### 4. `RecoverOverdue` is not simply “correct” as originally implemented

The document treats every `RecoverOverdue` call as a fresh wake that must survive.

That misses an important distinction:

- the first recovery for a missed absolute deadline is legitimate;
- repeated recovery of that same already-overdue deadline is duplicate work.

The scheduler item is one-shot, but the predicate deadline must also have one-shot recovery semantics.

### 5. The production diagnostic fingerprint is wrong

The document suggests:

runnable=0 stable=0

as the fingerprint.

For this mechanism, the more accurate state is:

target runnable=1
target stable/pending_state_change=0
target needs_check>0
no scheduled Timer/SSTimer item for the target

Global `SHOW PROCSNAP` counts may make attribution difficult, but a persistent runnable entry without corresponding stable membership is the relevant condition.

### 6. An empty scheduler is supporting evidence, not decisive proof

`SHOW SCHEDULER` reports the global scheduler queue.

An empty queue supports the claim that no TIMER was rearmed, but it does not prove that this particular erase caused the problem. Conversely, a nonempty scheduler does not disprove it because the remaining items may belong to other machines.

The stronger question is:

> Is the expected `Timer <machine>` or `SSTimer <machine> <state>` item present for the affected machine?

### 7. The state-change logging claim is too broad

The document says that a state change with no following `"Scheduling timer for"` means the machine is wedged.

That is true only if the newly entered state is known to require a TIMER. Many legitimate state changes require no timer.

Similarly, “transition-driven clocks are safe” should be qualified: state transitions avoid this exact matched-hold erase path, but their safety still depends on correct trigger ownership and correct arming of the new state.

### 8. Production attribution remains unproven

The mechanism is now reproduced in a focused test, but that proves a code-level defect rather than proving that every observed plant stall was caused by it.

A more accurate heading would be:

> Confirmed code-level lost-work mechanism; production incident attribution pending

---

# The fix

## Part 1: Preserve wake requests generated during evaluation

`checkStableStates()` now distinguishes between:

- an unchanged evaluation that generated no further work; and
- an unchanged evaluation that called `setNeedsCheck()`.

Conceptually:

bool erase_pending =
    !mi->enabled() ||
    !mi->getStateMachine()->allow_auto_states;

if (!erase_pending) {
    const bool changed_state = mi->setStableState();

    // setStableState() clears needs_check at entry. If it is nonzero now,
    // something requested another evaluation during this pass.
    erase_pending = !changed_state && !mi->needsCheck();
}

if (erase_pending) {
    pending_state_change.erase(mi);
}

This restores the proper caller/callee contract:

> Work created during `setStableState()` belongs to the next processing pass and must not be erased by the current pass.

This is a general correctness repair, not TIMER-specific behavior.

## Part 2: Recover each absolute TIMER deadline only once

Preserving every `RecoverOverdue` request would create an infinite evaluation loop. To prevent that, each TIMER predicate now remembers the absolute deadline it most recently recovered.

The absolute deadline is calculated as:

timer machine start_time + scheduled threshold

For a self TIMER:

deadline = target->start_time + scheduled_time * 1000;

For `other_machine.TIMER`, the other machine’s `start_time` is used.

Recovery then behaves as follows:

if (deadline is overdue &&
    deadline != last recovered deadline) {
    remember deadline;
    target->setNeedsCheck();
}

Consequences:

- The first evaluation of a missed deadline requests one follow-up.
- `checkStableStates()` preserves that follow-up.
- The follow-up sees the same absolute deadline and does not requeue again.
- Entering a new state changes `start_time`, producing a new absolute deadline.
- A changed TIMER threshold also produces a different deadline.
- Recovery therefore works again for the next genuine deadline.

This gives `RecoverOverdue` bounded, one-shot semantics.

## Why the absolute deadline is the correct identity

Using only the TIMER threshold would be insufficient because the same threshold is reused on every state entry.

For example:

TIMER >= 100

may occur every time a soft clock enters `on`. The threshold remains `100`, but each state entry has a different `start_time`.

The absolute deadline:

start_time + 100 ms

uniquely identifies the occurrence being recovered.

---

# Regression coverage

A new `test_timer_wake` target exercises the real `MachineInstance`, `Predicate`, `ProcessingThread`, and stable-state queue behavior.

It verifies:

### 1. General mid-evaluation wake preservation

A predicate deliberately calls `setNeedsCheck()` while being evaluated.

After `checkStableStates()`:

needs_check = true
pending_state_change membership = true
runnable membership = true

This proves the caller no longer erases work generated by its callee.

### 2. One follow-up for an overdue TIMER deadline

An already-overdue matched TIMER hold is evaluated.

The first pass leaves:

needs_check = true
pending_state_change membership = true
runnable membership = true

### 3. No repeated follow-up for the same deadline

The retained request is evaluated again.

The same absolute deadline is recognized as already recovered, so the second pass settles to:

needs_check = false
pending_state_change membership = false
runnable membership = false

This proves the repair does not create a permanent processing loop.

### 4. Recovery works for a new deadline

The same predicate is evaluated after changing the timer’s state-entry timestamp. The resulting absolute deadline differs, so recovery is permitted again.

---

# Verification

The focused TIMER test passed repeatedly.

The existing expression tests, including prior overdue-policy coverage, passed.

The complete configured test suite passed:

100% tests passed, 0 tests failed out of 26

The final added new-deadline case also passed in the focused suite.

---

# Recommended rewrite of the investigation’s conclusion

The core conclusion should be revised to something like:

> A stable-state evaluation can request a subsequent evaluation by calling `setNeedsCheck()`. The existing `checkStableStates()` caller treats “no state transition” as “no remaining work” and erases the newly created `pending_state_change` entry. The machine remains runnable with `needs_check != 0`, but stable-state selection skips it because its pending-state membership is absent.
>
> The caller must preserve wake requests generated during evaluation. However, overdue TIMER recovery must be bounded: retaining every recovery request would repeatedly requeue the same permanently overdue deadline and recreate the historical TIMER load storm. The implemented fix therefore preserves general mid-evaluation wake requests and allows only one `RecoverOverdue` follow-up per absolute TIMER deadline.

The document’s statement “fix the caller, not `Expression.cpp`” should be replaced with:

> Fix both sides of the contract: preserve newly generated work in `checkStableStates()`, and make `RecoverOverdue` one-shot per absolute deadline.

That is the key conclusion learned from implementing and testing the repair.
