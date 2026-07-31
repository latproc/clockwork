# Investigating Memory Leaks in Clockwork `iod`

This document describes the method used to find and validate memory leaks in the
Clockwork `iod` process while it is running a live machine.

The aim is not merely to find code that allocates memory. A useful investigation
must identify memory that remains owned after its legitimate lifetime, prove the
responsible call path, make the smallest safe ownership correction, and verify the
result under a comparable workload.

**Updated:** 2026-07-30  
**Branch:** `prod-experimental-mqtt-fix` / plant elc transport as installed  
**Recent fix commit:** `9106aee5` (WEBREQUEST pool, apply clone, float `%=`)  
**Plant host (source pack):** `2C-120` (2G4C / `--name 2GRAB`)  
**Sampling tree (when monitor installed):** `/opt/latproc/sampling/iod-memory/`  
**MEMSNAPSHOT file (iod-elc stream filter):**  
`/opt/latproc/sampling/iod-memory/memsnapshot.log`  
**Verbose file (runtime switch, no svc -t):** `/tmp/iod.log` via `/tmp/iod-verbose`
or `/opt/latproc/scripts/iod_verbose.sh` — see `TRANSPORT.md` / `TOOLS.md`.

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

Enable briefly (runtime) or leave `DEBUG_MEMSNAPSHOT` in `/opt/latproc/etc/iod.conf`
for restarts:

```bash
printf 'DEBUG DEBUG_MEMSNAPSHOT on;\n' | /opt/latproc/iod/iosh
# Always captured (when filter running): sampling/iod-memory/memsnapshot.log
tail -f /opt/latproc/sampling/iod-memory/memsnapshot.log
# Full stderr/stdout (PROCSNAP/ECDOMAIN/…) without restart:
#   /opt/latproc/scripts/iod_verbose.sh on 1800   # → /tmp/iod.log
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

### Example C — `Value::getFromJSON` scalar clone leak

**Commit:** `31fceba5` Fix cJSON leak in Value::getFromJSON for scalar fields  
**Tests:** `iod/tests/test_json_ownership.cpp` (`ValueGetFromJSONScalarDoesNotLeak`)

```cpp
// Bug: clone then assign_value for number/string/null/bool dropped the clone.
res = assign_value(clone_json(::getFromJSON(json, key)));

// Fix: ownership through Value(cJSON*) (frees non-object trees after convert).
return Value(clone_json(::getFromJSON(json, key)));
```

`get_value(cJSON *)` remains a **borrow** API (does not free). Callers that own
a new tree from `apply` / `Parse` / `clone_json` must use `Value(cJSON *)`.

### Example D — production-activity live cJSON (plant open; offline fixes landed)

**Evidence (historical):** 2G4C plant PID `3342133`, ~22.7 h, 2026-07-28/29 (see
`llm-rules/cw_issues/IOD_WEBREQUEST_MEMORY_GROWTH_20260721.md`).

| Condition | cjson / malloc_in_use |
|-----------|----------------------|
| Night / idle many hours | **Flat** (449281 / 143 MiB for ~12 h) |
| Morning–day production | Climbed to **~1.8M nodes / ~311 MiB** in_use |
| Main `[heap]` mapping | ~74 MiB flat |
| Worker anon arenas | Grew with WEBREQUEST traffic |
| Free / releasable | Stayed small (~4 MiB / tens of KiB) |

**Evidence (current run, RSS/smaps only — no MEMSNAPSHOT):** 2C-120 PID
`127846`, started 2026-07-28 11:38:43, sampled ~2026-07-29 15:05 (~**27.4 h**).
Binary `/opt/latproc/iod/iod_sdo` mtime 2026-07-28 10:35.

| Metric | ~27 h sample |
|--------|----------------|
| RSS / VSZ | **~138 MiB** / ~1.53 GiB |
| Main `[heap]` | ~**59 MiB** RSS |
| Largest worker anon | ~**27 MiB**; several ~7.5 MiB arenas |
| `SHOW HEALTH` | LOAD BUSY ~110–125 loops/s, THRASH none |

This restart is **healthier on RSS** than the historical day climb to ~334 MiB,
but it is **not** a validation of `b985908f` / `31fceba5` / `9106aee5`: build
objects for `Expression.cpp`, `PredicateAction.cpp`, and `value.cpp` predate
those commits (still 2026-07-26). Live binary includes idle-CPU, channels, and
turnOn/pending-out work through `7e062d0c`.

**Interpretation:**

1. Idle ITEM DEFAULT / scalar ownership fixes were effective when deployed
   (historical overnight flat on PID `3342133`).
2. Remaining growth was **live application retention** under production JSON/HTTP
   load, plus worker-thread arena high-water from per-request `pthread_create`.
3. Offline code changes for (2) are in tree as of `9106aee5` (Examples E–G).
   **Plant day-growth is not re-measured yet** — treat plant slope as open until
   a staged deploy + production-day MEMSNAPSHOT comparison.

### Example E — WEBREQUEST thread-per-request → fixed worker pool

**Commit:** `9106aee5`  
**File:** `iod/src/exec_web_request.c` (built into `web_request.so` via plugin include)

Previous design:

```text
CW SEND start
  pthread_create(worker) per request
  worker: curl_easy_init → perform → curl_easy_cleanup → done=1
  control: pthread_join → setJsonValue(Result) → free body
```

Problems:

- short-lived threads → glibc **per-thread arenas** high-water under catalog load;
- plain `int done` / `int abort` shared across threads (data race).

Current design:

```text
CW SEND start
  enqueue job on fixed pool (default 4 workers; env WEBREQUEST_POOL_SIZE)
  worker (long-lived): reuse thread-local CURL easy handle (curl_easy_reset)
  atomic_int done / abort
  control: poll done (no per-request join) → setJsonValue → free body
```

**Tests:** `iod/tests/test_exec_web_request` — basic GET, POST, 50× repeated.

**Offline stress (macOS warehouse sim, 2026-07-29):** ~876 catalog-sized HTTP
completions in 90 s (~9.7/s), last_status 200, **thread count flat at 12**,
RSS +~464 KiB then plateau. Does **not** prove Linux glibc arena behaviour.

**Deploy note:** plant must rebuild and install **`web_request.so.1.0`** as well
as `iod`/`cw`/`iod_sdo`; the plugin compiles `exec_web_request.c` into the .so.

### Example F — `apply()` Print+Parse → `clone_json`

**Commit:** `9106aee5`  
**File:** `iod/src/json_expression.cpp`

`apply()` must return a newly owned tree. It previously did
`cJSON_Print` + `cJSON_Parse` (CPU + allocator churn). It now uses
`clone_json` / `cJSON_Duplicate` (same ownership contract, less cost).

**Tests:** `test_json_ownership` (`ApplyJsonNull…`, `ApplyReturnsIndependentClone`,
scalar apply path).

### Example G — float `Value::operator%=` SIGFPE

**Commit:** `9106aee5`  
**File:** `iod/src/value.cpp`

Same-kind float modulus checked `other.fValue == 0` but divided by
`other.iValue` (often 0) → **`SIGFPE` / exit 136** (seen on macOS warehouse
sim startup). Fix: divisor is `(int64_t)::trunc(other.fValue)`. **Generic**
logic bug, not macOS-specific.

### CW LPC (not in this git repo)

Warehouse API machines should clear the WEBREQUEST working copy after handoff:

```lpc
ENTER done {
  result := curl.Result;
  curl.Result := "";
}
```

Implemented offline under warehouse `lib/api/samplingline_api.lpc` (and related
API LPC). That lives in **SVN** project trees, not the iod git tree. Reduces
live JSON working set when `result` is the consumer-owned copy.

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
| JSON ITEM DEFAULT / null `apply()` leak; PutSubExpr full clone | `b985908f` | Fixed in tree + unit test; **not linked into live PID `127846` binary** (rebuild+restart required); historical night flat when deployed |
| `Value::getFromJSON` scalar clone free | `31fceba5` | Fixed in tree + unit test; **not linked into live binary** (rebuild+restart required) |
| WEBREQUEST worker pool + atomic done/abort + easy reuse | `9106aee5` | Fixed in tree + unit/stress offline; **plant day RSS not re-proven** |
| `apply()` via `clone_json` not Print+Parse | `9106aee5` | Fixed in tree + unit tests |
| Float `Value::operator%=` divisor (SIGFPE) | `9106aee5` | Fixed in tree; generic |

Until a process is running a binary that includes B/C/E/F **and** plant slopes
confirm under busy HMI/JSON/HTTP load, treat those as **candidate fixes** per the
completion criteria above — do not infer success from the current ~138 MiB RSS
alone.
