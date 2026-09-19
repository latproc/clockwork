# COMMANDCLOCK in `cw` (Clockwork runtime without EtherCAT)

**Branch:** `feature/iod-elc-kernel-transport`  
**Written:** 2026-09-19. Confirmed by inspection and by running the real `cw`
before and after the fix.

## Symptom

A Clockwork program that uses `COMMANDCLOCK` runs under `cw` but the clock never
fires: the controller's `calcAdjust` (or whatever `command` names) never runs.
`cw` is the Clockwork runtime with no EtherCAT transport, and the clock only
ticks under `iod-elc`.

## Root cause

`COMMANDCLOCK` cadence has exactly one dispatch site, and it hangs off the
EtherCAT/IO sample path:

```
IOComponent::handle_io_sampling()            (IOComponent.cpp)
  -> MachineInstance::dispatchCommandClocks() (MachineInstance.cpp)
       <- called only from here
ProcessingThread::sampleRegularPolls()        (ProcessingThread.cpp)
  -> handle_io_sampling()
       guarded by: machine_is_ready && sample_clock != 0
```

In `cw` (built `EC_SIMULATOR`, no `USE_ETHERCAT`):

- `ECInterface::active` is false and `master_state.link_up` is 0, so
  `machine_is_ready` never latches (`ProcessingThread.cpp`, the
  `io_bus_usable` block).
- No EtherCAT thread publishes the sample clock, so `global_clock` stays 0.
- Both guards fail, `handle_io_sampling()` never runs, and
  `dispatchCommandClocks()` is never reached.

`cw`'s idle path also means the outer loop rarely runs, so this is not a
"starts late" problem — the clock never ticks at all.

Verified on the real binary with a minimal program: the clock reached state
`on` (so registration, guard and state machine were all fine) and no
`iod calcAdjust` line was ever produced.

## Fix

`cw` must drive the clock cadence from its own monotonic clock, not from the IO
sample path. `ProcessingThread::operator()()`'s in-wait loop is where a
transport-less runtime spends its idle time, so:

- dispatch registered clocks there (`MachineInstance::dispatchCommandClocks()`),
  but only while no scheduler handshake owns the machines
  (`status == e_waiting && processing_state == eIdle`), and
- poll **to the next clock boundary**: `CommandClock::nextDueUs()` reports the
  absolute µs the clock next needs a dispatch at, `MachineInstance::
  nextCommandClockWakeUs()` takes the earliest across registered clocks, and the
  wait loop caps its `zmq::poll` timeout to that deadline.

The deadline cap is what makes the cadence accurate. Without it the loop merely
woke on its own schedule (idle absorb up to 20 ms, or a stale `paced_only`
deadline), so a due tick was dispatched up to a whole period late — measured at
a median 22.9 ms for a 10 ms clock once a dependant was running. With the cap a
due tick is dispatched within ~1 ms of its boundary, and because the slot comes
from the absolute boundary a late dispatch realigns instead of drifting.

Measured on the real binary (10 ms clock, dependant handling `calcAdjust`),
before → after:

| `notify_period` | before median | after mean / median | after p95 |
|---|---|---|---|
| 10 ms | 22.9 ms | 10.0 / 10.0 ms | 11.4 ms |
| 5 ms | — | 5.0 / 4.8 ms | 6.3 ms |
| 20 ms | — | 20.0 / 20.0 ms | 21.2 ms |
| 50 ms | — | 50.0 / 49.9 ms | 51.4 ms |
| 200 ms | — | 200.0 / 200.0 ms | 200.5 ms |

Both additions are under `#if !defined(USE_ETHERCAT)`, so `iod-elc` behaviour is
unchanged: on the plant line the existing `handle_io_sampling()` dispatch stays
the single site and clocks still wait for a live bus sample clock.

`iod-elc` deliberately does **not** tick clocks when the bus is not ready:
`calcAdjust` must not run against a stale process image. `cw` has no process
image to be stale, which is why the fallback is scoped to the no-transport
runtime.

## Regression gate

- `iod/tests/test_command_clock.cpp` — deterministic unit tests for
  `CommandClock::nextDueUs()`: arm/next-boundary, not-yet-dispatched slot wakes
  immediately, late dispatch realigns to the absolute boundary (no drift),
  phase offset, period change re-arms, disabled needs no wake.
- `tests/command_clock.cw` defines a `COMMANDCLOCK`, a guard and a dependant that
  logs on every `calcAdjust`; CTest `runtime_command_clock` runs it under `cw` and
  requires the marker. It fails on the base (no tick) and passes with the fix. It
  is generic (no site machines) and is covered by
  `no_site_tree_in_product_tests`.

## Limits

- The `zmq::poll` timeout is milliseconds, so ~1 ms is the floor on dispatch
  lateness. A 1 ms `notify_period` therefore measures ~1.8 ms in practice; periods
  of 5 ms and above track their boundary to about ±1.5 ms. That is well inside
  the plant's shortest useful controller periods.
- Only `cw`/`cw-scaffold` (`EC_SIMULATOR` without `USE_ETHERCAT`) take this
  path. `cw_ecat` and `iod-elc` are unchanged, so on `iod-elc` ticks are still
  quantised to the bus/IO poll rate (fine when the bus period divides the
  `notify_period`, otherwise late by up to one bus period).

