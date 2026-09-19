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
- shorten the idle absorb to the process poll floor while any clock is
  registered, so `notify_period` below the 20 ms idle wait is honoured.

Both additions are under `#if !defined(USE_ETHERCAT)`, so `iod-elc` behaviour is
unchanged: on the plant line the existing `handle_io_sampling()` dispatch stays
the single site and clocks still wait for a live bus sample clock.

`iod-elc` deliberately does **not** tick clocks when the bus is not ready:
`calcAdjust` must not run against a stale process image. `cw` has no process
image to be stale, which is why the fallback is scoped to the no-transport
runtime.

## Regression gate

`tests/command_clock.cw` defines a `COMMANDCLOCK`, a guard and a dependant that
logs on every `calcAdjust`; CTest `runtime_command_clock` runs it under `cw` and
requires the marker. It fails on the base (no tick) and passes with the fix. It
is generic (no site machines) and is covered by
`no_site_tree_in_product_tests`.

## Limits

- Cadence in `cw` is bounded by the runtime poll, not by a bus clock. While a
  clock's dependants are busy the loop runs at the poll floor; when fully idle
  the wait floor is `get_polling_time()` (default 5 ms quiet), so very short
  `notify_period` values are approximate in simulation.
- Only `cw`/`cw-scaffold` (`EC_SIMULATOR` without `USE_ETHERCAT`) take this
  path. `cw_ecat` and `iod-elc` are unchanged.
