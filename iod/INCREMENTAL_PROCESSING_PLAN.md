# Incremental Clockwork processing with coherent I/O snapshots

## Goal

Reduce Clockwork CPU by eliminating repeated, broad predicate and dependency
evaluation, without changing control outcomes or losing I/O transitions under
load.

The new path is an opt-in runtime mode. The current implementation remains
available as an immediate configuration rollback.

## Core processing model

1. Receive an EtherCAT process image as one coherent I/O snapshot with its
   DC/application timestamp.

2. Apply every changed I/O value from that image before running any `WHEN`
   tests. Stamp every affected I/O machine's `IOTIME` from that same snapshot
   timestamp according to its existing update rules:

   - POINT/digital edge values receive the snapshot `IOTIME` on a real edge.
   - Analog/counter paths retain their regular-poll/filter behavior, but any
     published value and `IOTIME` belong to the same snapshot/sample.
   - No predicate may observe a new I/O value with an earlier snapshot's
     `IOTIME`.

3. Use a precomputed reverse dependency index to identify only the predicate
   conditions that read the changed state/property/I/O sources. Mark each
   affected condition once for that snapshot.

4. Evaluate the affected conditions and process resulting state transitions,
   actions, messages, timers, and further invalidations in deterministic
   dependency/work-queue order.

5. Give incoming I/O ingestion priority over control draining. If the control
   cascade is larger than one cycle, retain queued snapshots and work in
   arrival order, allow latency/backlog to grow temporarily, and catch up when
   load falls. Do not replace pending snapshots with only the newest values.

## Runtime implementation

- Add `CLOCKWORK_PROCESSING_MODE=legacy|incremental_io`, selectable by startup
  configuration or configuration reload. `legacy` retains current behavior
  unchanged; `incremental_io` activates the new snapshot path.
- Compile each parsed predicate during configuration fix-up into an immutable
  evaluation form:
  - Direct source handles for static properties, states, constants, and timer
    operands.
  - Explicit dynamic-resolution operations for references, JSON expressions,
    plugins, and other dynamic language behavior.
  - Retain source text and existing diagnostics for inspection.
- Build and maintain a reverse index from `(machine, property/state/I/O
  source)` to consuming predicate conditions. Dynamic reference binding, LIST
  membership changes, local bindings, and reloads update only the relevant
  index entries.
- Replace broad “notify all dependent machines and recheck all conditions”
  behavior for I/O-originated changes with targeted condition invalidation.
- Use deduplicated queued work items keyed by `(snapshot generation,
  machine/condition)` so one condition is evaluated at most once per input
  snapshot unless its own evaluation changes one of its inputs.
- Preserve current command, received-message, timer, and direct property-write
  ordering. Only values within one EtherCAT process image are coalesced into a
  common observation point.
- Implement incremental LIST subscriptions:
  - Track which aggregates and predicates observe each LIST/member.
  - Update `ANY`, `ALL`, `COUNT`, and similar supported aggregates from member
    transitions rather than rescanning the LIST.
  - Fall back to current full evaluation for unsupported/dynamic aggregate
    expressions, preserving compatibility.

## Overload and timing behavior

- Never hold I/O ingestion behind an unbounded control drain.
- Queue each coherent I/O snapshot with its timestamp and changed-source set;
  process snapshots FIFO.
- Do not drop or merge snapshots under normal overload handling, because lost
  intermediate edges can change control behavior.
- Expose diagnostics for snapshot queue depth, oldest snapshot age, I/O ingest
  time, control-drain time, invalidated/evaluated predicate counts, cascade
  depth, and catch-up rate.
- Add warning thresholds only; do not add an automatic work cutoff in the first
  version.

## Validation and rollout

- Run legacy and incremental modes against identical recorded EtherCAT
  snapshots and compare final machine states, values, `IOTIME`, messages,
  actions, and timer scheduling.
- Add scenarios for simultaneous bit changes, mixed analog/digital changes,
  dependency chains/diamonds, state/action cascades, timer interactions, large
  LISTs, LIST mutation, and dynamic references.
- Test overload by injecting snapshots faster than control processing, then
  verify FIFO handling, correct `IOTIME`, preserved transitions, and eventual
  catch-up.
- Measure CPU and latency on representative plant configurations. Make
  incremental mode the default only after demonstrating parity and a material
  reduction in predicate/dependency work.
- Keep the legacy runtime mode deployed and documented as the operational
  backout throughout validation.

## Separate C-export track

Generated C remains a separate project. Before it can be considered a
full-runtime alternative, it needs a native runtime model for LIST membership,
LIST-mutating actions, iteration, aggregate predicates, dependencies, and the
dynamic behaviors currently missing from exported examples.
