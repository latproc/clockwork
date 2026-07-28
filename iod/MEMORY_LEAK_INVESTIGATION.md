# Investigating Memory Leaks in Clockwork `iod`

This document describes the method used to find and validate memory leaks in the
Clockwork `iod` process while it is running a live machine.

The aim is not merely to find code that allocates memory. A useful investigation
must identify memory that remains owned after its legitimate lifetime, prove the
responsible call path, make the smallest safe ownership correction, and verify the
result under a comparable workload.

**Updated:** 2026-07-28  
**Branch:** `prod-experimental-mqtt-fix`  
**Sampling tree:** `/opt/latproc/sampling/iod-memory/` (monitor CSV + bpf traces)

## Safety rules for a live machine

1. Start with read-only observation. Do not restart `iod`, replace a binary, change
   configuration, or alter machine state merely to obtain a cleaner measurement.
2. Check service and EtherCAT health before and after any tracing or deployment:
   service PID and uptime, EtherCAT Operational state, slave count, link state, and
   frame loss.
3. Use sampled tracing on hot allocation paths. Tracing every allocation in a
   high-frequency process can add unacceptable overhead.
4. Keep diagnostic collection bounded. Every trace must have an automatic end time.
5. Build a distinct replacement binary and preserve the previous binary so rollback
   remains possible.
6. Treat a restart as an operational change. Only restart after the fix builds and
   relevant tests pass, and only when deployment has been authorised.
7. After restart, distinguish normal startup growth from steady-state growth. Do not
   calculate a leak rate from the first few minutes.
8. Do not call a leak fixed from one RSS reading. Require a trend across multiple
   comparable samples.

## Investigation sequence

### 1. Establish a baseline

Record:

- executable path, PID, process start time, and uptime;
- RSS, virtual size, data size, swap, thread count, and file-descriptor count;
- `/proc/<pid>/smaps_rollup` values such as PSS, private dirty memory, and anonymous
  memory;
- CPU work counters or another workload indicator;
- service restarts and the exact binary used by each run.

The memory monitor writes these values to:

```text
/opt/latproc/sampling/iod-memory/memory.csv
```

Use a period long enough to separate noise from a trend. Compare both the whole run
and a recent window. RSS alone can include allocator arenas and shared pages, so
confirm suspicious growth with private/anonymous memory and allocator statistics.

### 2. Decide whether growth is retention or allocator behaviour

`mallinfo2()` snapshots are emitted by the instrumented processing thread as
`MEMSNAPSHOT` records (opt-in: `DEBUG DEBUG_MEMSNAPSHOT on`). Important fields are:

- `malloc_in_use_kb`: bytes currently allocated to the application;
- `malloc_free_kb`: free bytes retained inside allocator arenas;
- `malloc_arena_kb`: total arena space obtained by the allocator;
- `malloc_mmap_kb`: separately mapped large allocations;
- `malloc_releasable_kb`: top-of-heap space potentially returnable to the OS.

If RSS rises but `malloc_in_use_kb` is flat, fragmentation, allocator caching, shared
memory, or non-heap mappings may be responsible. If `malloc_in_use_kb` and
private/anonymous memory rise together, live allocations are being retained and an
ownership trace is justified.

Enable briefly:

```bash
printf 'DEBUG DEBUG_MEMSNAPSHOT on;\n' | /opt/latproc/iod/iosh
# stderr / verbose log lines start with MEMSNAPSHOT
printf 'DEBUG DEBUG_MEMSNAPSHOT off;\n' | /opt/latproc/iod/iosh
```

### 3. Rule out known accumulating containers

The instrumented `MEMSNAPSHOT` also records:

- pending scheduler entries;
- live triggers;
- pending events;
- active actions;
- machine mail items;
- throttled channel items;
- message-log entries;
- live cJSON nodes.

An increasing counter provides a direct lead. A stable counter rules out that class
as the main explanation for steady growth. Counters should represent currently live
objects, not only cumulative constructor calls.

### 4. Use lifecycle probes for important C++ classes

Temporary `bpftrace` uprobes can count constructors and destructors for classes such
as:

- `Message`;
- `Package`;
- `State`;
- `Action`;
- `Predicate`;
- `Value`;
- `CStringHolder`;
- `DynamicValueBase`;
- cJSON nodes (`cJSON_New_Item` / `cJSON_Delete` / create helpers).

Constructor and destructor counts should be compared over the same interval.
Cumulative counts alone can be misleading because objects may legitimately remain
live at the end of a sample. Prefer an outstanding count or allow a drain period
before interpreting a difference as a leak.

Traces collected under this investigation (examples under
`sampling/iod-memory/`):

- `bpf-malloc-drain-*.txt` — sampled malloc with free drain
- `bpf-cjson-create-stacks-*.txt` / `bpf-cjson-lifetime-*.txt` — cJSON create stacks
- `bpf-value-lifecycle-*.txt`, `bpf-complete-lifecycle-*.txt`

### 5. Trace allocation lifetimes, not allocation volume

Allocation hot spots are not necessarily leaks. CW performs millions of legitimate
short-lived allocations. The useful trace is:

1. Sample calls to `malloc`.
2. Record the allocation address, size, and user-space stack.
3. Observe all `free` calls and remove tracked addresses when released.
4. Stop collecting new allocations after a fixed collection window.
5. Continue observing frees during a drain window.
6. Report only allocations that survive the drain window, grouped by allocation
   stack.

The temporary trace used for this investigation is:

```text
/tmp/iod_malloc_lifetime_drain.bt
```

Typical invocation:

```bash
bpftrace /tmp/iod_malloc_lifetime_drain.bt PID COLLECTION_SECONDS \
  TOTAL_SECONDS SAMPLE_RATE
```

For example, `120 240 16` collects one in sixteen allocations for two minutes,
then observes frees for another two minutes. Reported sizes and counts are scaled by
the sample rate. Sampling introduces estimation error, so a stack is considered
strong evidence when its estimated retained rate is close to the independently
measured allocator growth and it repeats in another sample.

### 6. Map the surviving stack to an exact source line

Use the exact binary running in the process. Build IDs or checksums should match the
symbol file used for resolution.

Useful tools include:

```bash
nm -C BINARY
addr2line -Cfipe BINARY ADDRESS
objdump -dC --line-numbers BINARY
```

Optimisation can associate an allocation with a return address or an inlined caller
rather than the precise source expression. Inspect the surrounding disassembly and
source code, then follow the returned pointer through its complete ownership path.

### 7. Perform an ownership audit

For each suspected allocation, answer:

1. Who allocates it, and with which allocator (`new`, `new[]`, `malloc`, cJSON, or a
   library allocator)?
2. Who receives the pointer?
3. Is ownership transferred, shared, or borrowed?
4. Which success, error, retry, and shutdown paths release it?
5. Is the matching release correct (`delete`, `delete[]`, `free`, or the library's
   release function)?
6. Can the same wrapper sometimes contain owned memory and sometimes borrowed
   memory?
7. Are copy and move operations safe for the ownership model?

If a wrapper can carry either owned or borrowed memory, ownership must be explicit.
Do not unconditionally free the pointer unless every producer transfers ownership.

## Examples found during this investigation

### Example A — EtherCAT update mask ownership (`IOUpdate`)

**Commit:** `6eaac1b8` Fix EtherCAT update mask ownership

`IOComponent::getUpdates()` created a fresh EtherCAT output mask with:

```cpp
new uint8_t[...]
```

The mask was stored in an `IOUpdate`. `ProcessingThread` sent the update and deleted
the `IOUpdate`, but `IOUpdate::~IOUpdate()` did not release the mask. The destructor
could not simply delete every mask because `IOComponent::getDefaults()` stores a
shared static default mask in the same wrapper.

The allocation-lifetime trace attributed retained allocations to
`IOComponent::getUpdates()` and the measured retained rate closely matched the
process's allocator growth. The correction makes mask ownership explicit:

- dynamically generated update masks are owned and released with `delete[]`
  (`setMask(mask, true)` → `owns_mask_`);
- the shared default mask remains borrowed and is not released by `IOUpdate`
  (`setMask(default_mask)` → `owns_mask_ == false`).

This illustrates why both tracing and source-level ownership analysis are required.
The allocation site identified the path, while the two producer functions explained
why the destructor had originally avoided freeing the pointer.

**Current code (still correct):**

```cpp
// getUpdates
res->setMask(mask, true);   // owned

// getDefaults
res->setMask(default_mask); // borrowed; destructor must not delete[]

IOUpdate::~IOUpdate() {
    if (owns_mask_) {
        delete[] mask_;
    }
}
```

### Example B — cJSON trees from JSON ITEM / DEFAULT and PutSubExpr

**Commit:** `b985908f` Fix cJSON leaks on JSON ITEM DEFAULT and PutSubExpr copies  
**Tests:** `iod/tests/test_json_ownership.cpp` (incl. null-field DEFAULT regression)

`json_expression::apply()` always allocates a new cJSON tree (or returns
`nullptr`). Callers that branch on JSON null and take a DEFAULT without handing the
tree to `Value` leaked one node (or tree) on every evaluation of:

```text
ITEM ${field} OF some_json DEFAULT ...
```

when `field` was JSON `null`. Live cJSON node counts and cJSON create stacks rose
with plant JSON traffic (HMI / channels / recipes).

**Ownership rules that fixed it:**

1. **Always construct `Value` from `apply()`'s result** so scalar and null nodes are
   freed after conversion; empty `Value` means “use DEFAULT” without retaining the
   clone.
2. **`PutSubExpr` / predicate JSON assign:** mutate in place. Do **not**
   `cJSON_Print` + `cJSON_Parse` a full document clone after `assign()` — that
   doubled peak memory and is unnecessary once the tree is already updated. Keep
   the local `lhs.json` pointer in sync if `assign()` replaces the root.

Related earlier ownership work on the same branch (not a substitute for B):

| Commit | Theme |
|--------|--------|
| `d2f86252` / `844fa06d` | `assign` / `assign_take` ownership API |
| `37f7e75f` | JSON ownership unit tests |
| `4b126e1e` | null JSON value guards |

**Regression test idea (already in tree):** call `apply()` on a null field path,
wrap in `Value`, confirm no live node remains after scope exit when DEFAULT would
have been taken.

## Fixing and testing rules

1. Make the smallest correction that expresses the real ownership contract.
2. Avoid unrelated cleanup while diagnosing a production leak.
3. Run `git diff --check`.
4. Build the same configuration used in production, not only a debug or ASAN build.
5. Run focused unit tests and the relevant existing regression tests
   (`test_json_ownership`, safety/ownership suite).
6. Use ASAN/LSAN where the environment permits it. If LeakSanitizer cannot operate
   because the process is under `ptrace`, record that limitation rather than
   interpreting the tool failure as a product failure.
7. Do not overwrite or remove the previous production binary.
8. Deploy under a distinct filename so the active executable is unambiguous.
   Local practice on plant: `iod_sdo.prev-memfix-*`, `iod_sdo.staged-json-ownership-fix`.

## Post-deployment validation

After deploying:

1. Confirm the new executable path and a new PID.
2. Confirm EtherCAT is Operational, all expected slaves are present, the link is up,
   and frame loss remains zero.
3. Allow at least five minutes for normal startup and cache warm-up.
4. Measure RSS, private/anonymous memory, and `malloc_in_use_kb` over several
   one-minute samples.
5. Compare the new slope with the same workload period from the previous binary.
6. If growth remains, repeat the drained allocation trace on the new PID. A correct
   fix should remove the old surviving allocation stack; any remaining growth must
   be attributed independently.
7. Validate again during a busy machine period because leaks tied to messages,
   state transitions, JSON handling, or EtherCAT updates may scale with activity.
8. For JSON fixes specifically: watch MEMSNAPSHOT live cJSON node count and
   HMI/channel traffic paths that evaluate `ITEM ... DEFAULT` heavily.

## Completion criteria

A leak is considered resolved only when:

- the ownership defect is identified and corrected;
- the previous survivor stack no longer remains live;
- builds and relevant tests pass;
- the live service and EtherCAT remain healthy;
- steady-state allocator and private-memory slopes are flat or reduced to explained,
  bounded behaviour;
- the result holds under representative busy operation.

Until all of these are true, report the result as an improvement or a candidate fix,
not as a completed leak fix.

## Known fixed leaks (summary)

| Issue | Fix commit | Status |
|-------|------------|--------|
| `IOUpdate` mask `delete[]` ownership | `6eaac1b8` | Fixed in tree; ownership still explicit via `owns_mask_` |
| JSON ITEM DEFAULT / null `apply()` leak; PutSubExpr full clone | `b985908f` | Fixed in tree + unit test; plant deploy as staged binary if not yet live |

Until plant slopes confirm B under busy HMI/JSON load, treat B as a **candidate
fix** per the completion criteria above if the service has not yet been
restarted onto that binary.
