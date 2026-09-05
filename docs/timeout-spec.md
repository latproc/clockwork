# Timeout Semantics

Status: proposal

This document defines deadlines for blocking Clockwork actions and handlers. It is
intended to define language behavior independently of the legacy and v2 runtime
implementations.

## Goals

- Bound how long a blocking action, block, or handler may remain active.
- Allow a timeout to be recovered close to its source or by an enclosing handler.
- Make nested timeouts and timeout/error races deterministic.
- Preserve Clockwork's rule that a machine executes at most one active handler at a
  time.
- Avoid implying that timing out can undo effects which have already occurred.

## Syntax

`<duration>` is an integer expression evaluated in milliseconds, consistent with
`TIMER` and `WAIT`. Explicit unit suffixes such as `500ms` and `5s` are not supported
by this proposal; they may be considered in a future language revision.

```text
<statement> WITH TIMEOUT <duration>;
<statement> WITH TIMEOUT <duration> ON TIMEOUT { <statements> }

TRY { <statements> }
    WITH TIMEOUT <duration>
    [ ON TIMEOUT { <statements> } ]
    [ ON ERROR { <statements> } ]

<handler> { <statements> }
    [ WITH TIMEOUT <duration> ]
    [ ON TIMEOUT { <statements> } ]
    [ ON ERROR { <statements> } ]
```

The initial statement forms apply to actions which may suspend, including `CALL`,
`WAIT`, `WAITFOR`, and a non-local `SET`. `WITH TIMEOUT` is also legal on an action
which cannot suspend. Such an action completes synchronously, so its deadline has no
observable effect.

The timeout block must be introduced by `ON TIMEOUT`. A bare block after
`WITH TIMEOUT`, such as `WITH TIMEOUT 5000 { ... }`, is not part of this proposal;
requiring the keywords avoids ambiguity with ordinary action and command blocks.

For a handler only, this shorthand is permitted:

```text
<handler> { <statements> }
    AFTER <duration> { <timeout-statements> }
```

It is exactly equivalent to:

```text
<handler> { <statements> }
    WITH TIMEOUT <duration>
    ON TIMEOUT { <timeout-statements> }
```

`AFTER` does not run after successful completion. It runs only if the handler's
deadline expires.

## Deadline Lifecycle

The duration expression is evaluated once, when execution enters the decorated
action, block, or handler. The deadline is calculated from a monotonic clock:

```text
deadline = start_time + evaluated_duration
```

- A positive duration establishes a deadline.
- A zero duration allows synchronous completion without suspension, but times out as
  soon as the construct would suspend.
- A negative duration is a runtime error. A constant negative duration should be a
  compile-time error.
- Wall-clock adjustments do not affect a running deadline.
- A deadline is cancelled when its construct completes by any route.
- Re-entering a handler evaluates the expression and creates a new deadline.

The timer begins when the construct starts executing, not when the containing
message is placed on a machine's queue. For `CALL`, this is immediately before the
request is dispatched. Completion means receipt of the command's completion reply,
not merely acceptance of the request by the target machine.

## Timeout Outcome

Timeout is a distinct control-flow outcome. It is not an alias for `ABORT`, an error,
or a thrown message.

When a deadline expires, the runtime:

1. Stops waiting in the construct whose deadline expired.
2. Cancels the remaining local execution of that construct.
3. Creates a timeout context containing the evaluated duration and source location.
4. Runs the nearest applicable `ON TIMEOUT` block.
5. Propagates an unhandled timeout to the next enclosing timed or timeout-aware
   construct.

An `ON ERROR` block does not handle a timeout. This distinction lets a program apply
different recovery policies to communication delay and action failure.

If an `ON TIMEOUT` block completes normally, the timeout is handled and its decorated
construct completes normally. Statements following that construct may execute. If
the timeout block uses `ABORT`, `RETURN`, or `THROW`, that control-flow operation has
its usual meaning.

Timeout is not directly catchable as a typed event in this proposal. A program may
translate it into an existing catchable message by using `THROW` in `ON TIMEOUT`.

An unhandled timeout escaping the outermost active handler fails that handler and is
reported by the runtime. The implementation must not silently continue after the
timed-out action.

## `ON TIMEOUT` Scope

An `ON TIMEOUT` attached to an action handles only that action's deadline:

```clockwork
WAITFOR customer IS clean
    WITH TIMEOUT 3000
    ON TIMEOUT { THROW drain_timeout; }
```

An `ON TIMEOUT` attached to a block or handler handles either:

- the deadline attached to that block or handler; or
- an unhandled timeout propagated by an action nested within it.

Therefore, a handler may provide common recovery without having its own deadline:

```clockwork
COMMAND run {
    SET switch TO on;
    WAITFOR flag IS on WITH TIMEOUT 2000;
}
ON TIMEOUT { LOG "run timed out after " + TIMEOUT + "ms"; }
```

Adding `WITH TIMEOUT` to the handler also bounds its total execution time:

```clockwork
COMMAND run {
    SET switch TO on;
    WAIT 2000;
    WAITFOR flag IS on WITH TIMEOUT 2000;
}
WITH TIMEOUT 3000
ON TIMEOUT { LOG "run timed out after " + TIMEOUT + "ms"; }
```

In this example, `TIMEOUT` is `2000` if the `WAITFOR` deadline expires first and
`3000` if the command deadline expires first.

## Nested Deadlines

Every active timed scope retains its own absolute deadline. The first deadline to
expire determines the timeout context. An inner deadline does not pause, reset, or
extend an outer deadline.

If an inner timeout has a local `ON TIMEOUT` block, that block runs under the still
active outer deadline. The outer deadline may therefore expire while inner recovery
is suspended. It then cancels the inner recovery and transfers control to the outer
timeout block, if one exists.

If two deadlines have the same deadline instant, the innermost deadline wins. This
rule makes the result independent of timer queue insertion order.

## Completion Races

An action which completes synchronously before it suspends succeeds, including under
`WITH TIMEOUT 0`.

After suspension, completion succeeds only if its recorded completion time is
strictly earlier than the deadline. Completion at or after the deadline loses to the
timeout. Implementations must use the recorded event times rather than whichever
event happens to be dequeued first.

These rules apply equally to a `WAITFOR` state change, a `CALL` completion reply, and
other awaitable actions.

## The `TIMEOUT` Value

Within `ON TIMEOUT`, `TIMEOUT` evaluates to the duration which was evaluated for the
deadline that expired, in milliseconds. It is the configured budget, not the actual
elapsed time. This proposal does not expose actual elapsed time as a separate value.

`TIMEOUT` is a read-only contextual value. It is available only while executing the
corresponding `ON TIMEOUT` block and any `ON ERROR` block entered synchronously from
that block. It is not a mutable property of the machine.

`RETURN` or normal completion removes the timeout context. `THROW` does not copy the
context into the thrown message, so a receiving `CATCH` cannot use `TIMEOUT`. Code
which needs that information must log it or copy it into an ordinary property before
throwing.

This scoping is important even though a machine currently has only one active
handler: a timeout recovery block may suspend, and different machines may execute
timeout recovery concurrently.

## Interaction With Errors

`ON TIMEOUT` and `ON ERROR` handle different outcomes:

```clockwork
ENTER off {
    SET light TO off WITH TIMEOUT 10;
}
ON TIMEOUT {
    LOG "light timed out after " + TIMEOUT + "ms";
    ABORT;
}
ON ERROR {
    LOG "failed to turn light off";
}
```

- If `SET` fails before the deadline, only `ON ERROR` runs.
- If the deadline expires, `ON TIMEOUT` runs.
- Because this timeout block executes `ABORT`, `ON ERROR` then runs with the same
  timeout context still available.
- If the timeout block completes normally, `ON ERROR` does not run.

If action failure and deadline expiry have the same recorded time, deadline expiry
wins. This is consistent with the rule that successful completion must occur strictly
before the deadline.

## External Effects And Cancellation

A timeout cancels the caller's local wait and remaining local statements. It does not
roll back completed actions, property writes, state changes, messages, or plugin I/O.

In particular, timing out a `CALL` does not imply that the target command stops. The
target may finish later and produce effects. Its late completion reply must be
discarded or correlated so it cannot complete a later call. Cooperative cancellation
of a target command requires a separate protocol and is outside this proposal.

## Examples

### Action-Local Recovery

```clockwork
COMMAND load {
    CALL find ON db
        WITH TIMEOUT 5000
        ON TIMEOUT {
            LOG "find timed out after " + TIMEOUT + "ms";
            THROW db_miss;
        }
}

CATCH db_miss {
    LOG "database lookup failed";
}
```

### Caller Bounds A Command

```clockwork
COMMAND drain {
    WAITFOR customer IS clean;
}

CALL drain
    WITH TIMEOUT 3000
    ON TIMEOUT { LOG "drain timed out"; }
```

The timeout stops the caller waiting for `drain`; it does not automatically cancel
the executing `drain` command.

### Whole-Handler Deadline

```clockwork
ENTER INIT {
    CALL load;
    CALL setup;
    CALL start;
}
AFTER 5000 {
    LOG "failed to start";
    SHUTDOWN;
}
```

### Bounded Subsequence

```clockwork
COMMAND start {
    CALL load;
    TRY {
        CALL setup;
        CALL start;
    }
    WITH TIMEOUT 10000
    ON TIMEOUT {
        LOG "setup timed out";
        SHUTDOWN;
    }
}
```

## Legacy Syntax Compatibility

The legacy language already accepts forms such as:

```clockwork
CALL calc ON calculator ON ERROR bad_call ON TIMEOUT call_timeout;
```

Here, `call_timeout` names a message handled by `CATCH`; it is not a duration or an
inline timeout recovery block. A parser supporting both forms can distinguish them
by the required `WITH TIMEOUT <duration>` clause in this proposal. The legacy form
must retain its existing behavior unless it is separately deprecated.

## Implementation Requirements

- Use monotonic absolute deadlines rather than decrementing counters.
- Associate each deadline with the active action or scope and a generation/token so
  stale timer entries are harmless.
- Store timeout context on the active execution stack, not in shared machine
  properties.
- Remove or invalidate a deadline on every completion route.
- Correlate asynchronous replies so a late reply cannot satisfy a later action.
- Preserve the source location of the timed construct for diagnostics.
