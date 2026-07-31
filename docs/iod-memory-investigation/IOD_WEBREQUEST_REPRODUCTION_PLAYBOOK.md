# IOD WEBREQUEST Memory Reproduction Playbook

## Objective

Reproduce and explain the production-activity memory growth associated with
repeated CW `WEBREQUEST` use, implement the smallest correct fix, and prove that
the fix preserves request behavior while preventing unbounded live or resident
memory growth.

Read `IOD_WEBREQUEST_MEMORY_GROWTH_20260721.md` first — especially
**2026-07-28/29 long-run findings** and **Offline work plan**.

**Plant is not required** for the remaining work. Overnight on 2G4C-120 was
flat after the ITEM DEFAULT fix; daytime live cJSON + worker arenas are what
to reproduce offline.

## Preferred test environments

### Linux VM: primary allocator reproduction (preferred for open work)

Use a Linux VM when possible because the production symptom is concentrated in
glibc worker arenas **and** live cJSON under catalog-sized JSON. Record the
distribution, glibc version, libcurl version and TLS backend, compiler, CMake,
CPU count, RAM, swap, and IOD commit. Try to match the production
Debian/glibc/libcurl environment before varying versions.

### macOS warehouse simulation: functional and true-leak test

The existing macOS CW warehouse simulation is valuable because it exercises
real CW usage. Use Instruments Allocations and Leaks. Apple malloc differs from
glibc, so a flat macOS RSS result does not rule out Linux arena retention.

### Production controller: final confirmation only

Do not use the operational controller for initial reproduction. A second CW
process on another command port may be used only with explicit operator
approval, a CPU/memory limit, no EtherCAT access, no production channels, and a
defined stop condition. Never run heavy profilers beside active control.

## Preserve production evidence

If the operator wants the original artifacts retained, record or copy these
read-only files without changing the running process:

```text
/opt/latproc/sampling/iod-memory/memory.csv
/opt/latproc/sampling/iod-memory/events.log
/opt/latproc/sampling/iod-memory/pmap-*.txt
/opt/latproc/sampling/iod-memory/memsnapshot.log   # iod-elc stream filter
/opt/latproc/sampling/log-YYYYMMDD.txt
# optional full stderr: /tmp/iod.log when /tmp/iod-verbose is on
```

Historical short pmaps (2026-07-20) and the 22.7 h series for PID `3342133`
(2026-07-28/29) are already summarised in
`IOD_WEBREQUEST_MEMORY_GROWTH_20260721.md`. Prefer that summary over
re-deriving from the plant unless more detail is needed.

Do not copy API credentials or production identifiers into test fixtures.

### MEMSNAPSHOT note

```bash
# Persist: DEBUG_MEMSNAPSHOT in /opt/latproc/etc/iod.conf
printf 'DEBUG DEBUG_MEMSNAPSHOT on;\n' | /opt/latproc/iod/iosh
tail -f /opt/latproc/sampling/iod-memory/memsnapshot.log
```

Runtime flag alone is cleared on every iod restart unless set in `iod.conf`.
Night-flat / day-climb conclusions for the long run used continuous MEMSNAPSHOT
after it was re-enabled mid-afternoon 28 Jul.

## Establish the source baseline

On the test machine:

1. Check out `prod-experimental-mqtt-fix` (or a branch that includes the commits
   below). Older notes referenced `investigate/iod-memory-leak-2g4c-120`.
2. Confirm these ownership fixes are present:
   - idle scalar free-on-convert (`Value(cJSON*)` path; historically `35407c47`);
   - `b985908f` ITEM DEFAULT + PutSubExpr;
   - `31fceba5` `Value::getFromJSON` scalar clones.
3. Record `git status`, including unrelated local changes.
4. Build Release and, if useful, Debug; use ccache and suitable parallelism.
5. Run `test_json_ownership`, `test_value`, `test_json_value`, and other relevant
   tests.
6. Do not mix unrelated fixes into the reproduction branch.

Without the idle/DEFAULT fixes, night/idle drip will contaminate activity
measurements. With them present, idle must stay flat (plant already proved
that).

## Local HTTP fixture

Use a server bound to loopback with deterministic synthetic endpoints:

```text
GET /small       small JSON object
GET /catalog     representative array/object
GET /large       configurable large JSON
GET /invalid     malformed JSON
GET /error       HTTP 500 with a body
GET /slow        delayed longer than the CW timeout
POST /echo       echoes representative PostData
```

Record exact response sizes. Include approximately 1 KiB, 16 KiB, 128 KiB,
and 1 MiB cases.

## Minimal CW workload

Create a small configuration containing:

- one `WEBREQUEST` instance;
- requested/completed/error/aborted counters;
- sequential mode;
- configurable concurrency using 1, 2, 4, and 8 instances;
- reset behavior matching the warehouse API classes;
- no EtherCAT, Modbus, MQTT, sampler, or production endpoint;
- a distinct command port.

Exercise:

```text
Idle -> Start -> Running -> Done/Error -> reset -> Idle
```

Also force a transition out of `Running` to test abort cleanup. Separately use
the warehouse simulation to exercise `P_BaleCatalogForAssignment` and determine
why it requested about 441 catalogs for roughly 50 bales.

## Workload matrix

Warm up with 100 requests. Measure at 0, 100, 500, 1,000, 2,000, 5,000, and
10,000 completions.

Run:

1. sequential small successful responses;
2. sequential catalog-sized responses;
3. sequential large responses;
4. concurrency 2, 4, and 8;
5. repeated HTTP errors;
6. timeouts;
7. forced abort/reset;
8. malformed JSON;
9. mixed production-like traffic.

After each active phase, stop requests and observe for at least five minutes
without restarting the process.

## Measurements

Always record request/error/abort counts, response bytes, RSS, virtual size,
thread count, FD/socket count, elapsed time, and request latency if available.

### Linux

Low-overhead observations include:

```sh
ps -p PID -o pid,rss,vsz,nlwp,etime
cat /proc/PID/smaps_rollup
pmap -x PID
```

In a test VM also use ASan/LSan, Valgrind for smaller runs, heaptrack, and
`malloc_info()` or `mallinfo2()` to compare in-use and arena bytes. A
test-only `malloc_trim(0)` after requests stop can distinguish retained freed
memory from live allocations; it is not the preferred production fix.

Compare default glibc behavior with `MALLOC_ARENA_MAX=1` and `2` only in the
VM. A global arena limit can introduce allocator contention and must not be
deployed to the controller merely because it lowers RSS.

### macOS

Use Activity Monitor or `ps` plus Instruments Allocations and Leaks. Examples,
adjusted for the installed Xcode version:

```sh
leaks PID
xcrun xctrace record --template 'Allocations' --attach PID --time-limit 10m
```

Look for outstanding allocations attributed to `exec_web_request`, `write_cb`,
libcurl/TLS, cJSON, `Value`, `MachineInstance::setValue`, and channel message
encoding.

## Experiments that distinguish causes

### Live leak versus allocator retention

Evidence for a live leak includes outstanding bytes growing with request count,
growing `mallinfo2().uordblks`, and retained objects in LSan, Valgrind, or
heaptrack.

Evidence for allocator retention includes in-use bytes plateauing while RSS and
arena bytes remain high, growth isolated to worker arenas, substantial recovery
after test-only `malloc_trim(0)`, sensitivity to arena limits, and flat macOS
live allocations under the equivalent workload.

### Worker/libcurl versus JSON handoff

Measure around response allocation, `curl_easy_cleanup`, `pthread_join`,
`setJsonValue`, and response free. Compare:

- receiving then discarding a response without `setJsonValue`;
- repeatedly passing synthetic JSON through `setJsonValue` without curl.

This separates worker/libcurl allocation from CW JSON/property replacement.

### Concurrency

Record maximum simultaneously Running WEBREQUEST instances. Compare one
sequential instance with multiple instances. Growth driven by maximum
concurrency supports the worker-arena hypothesis.

## Candidate fixes

Prove the mechanism before changing implementation. Evaluate, one at a time:

- a persistent worker thread and bounded request queue;
- a bounded shared worker pool;
- CURL easy-handle reuse where libcurl permits;
- ownership that keeps allocation and destruction in a controlled context.

Any redesign must also:

- replace unsynchronised `done` and `abort` flags with atomics or locked state;
- join each created thread exactly once;
- document ownership of every request and response pointer;
- bound response size and handle realloc failure safely;
- preserve timeout, abort, error, method, Content-Type, TLS, and non-JSON paths;
- prevent callbacks accessing freed `WebRequestData`;
- bound queues when a server is slow;
- shut workers down safely.

`malloc_trim()` after every request and global `MALLOC_ARENA_MAX` settings are
diagnostic or interim mitigations, not preferred fixes. Reducing excessive
catalog requests may be worthwhile but does not replace correcting unbounded
memory behavior.

## Required tests

- Existing IOD tests pass.
- A new repeated-request regression test passes.
- Successful JSON and non-JSON results remain correct.
- GET, POST, and method overrides remain correct.
- Header and TLS-option behavior remains correct.
- HTTP, curl, timeout, malformed JSON, and abort paths clean up.
- ASan/TSan find no use-after-free or data race where supported.
- Thread, FD, and socket counts return to baseline after quieting.
- Slow-server tests cannot create an unbounded queue.

## Acceptance criteria

1. At least 10,000 production-sized sequential requests complete on Linux.
2. After warm-up, outstanding/in-use bytes do not grow linearly with requests.
3. RSS reaches a measured and explained plateau.
4. Error, timeout, abort, and concurrency workloads also plateau.
5. Functional behavior matches the existing plugin.
6. The macOS warehouse simulation shows no live allocation slope or regression.
7. Exact commands and raw measurements are preserved.
8. Production rollout has operator approval, rollback, and a shutdown window.

Report growth per 1,000 requests after warm-up. “Less growth” is not sufficient.

## Production rollout after laboratory proof

1. Commit the isolated fix on the IOD investigation branch.
2. Run the project checker and inspect its dependency graph.
3. Build with ccache and operator-approved parallelism.
4. Run unit and repeated-request tests.
5. Install a versioned candidate beside the rollback binary.
6. Switch the supervised service only during an approved shutdown break.
7. Confirm EtherCAT Operational state and expected slave count.
8. Keep the low-overhead memory monitor active.
9. Compare equal request/bale intervals with the 2026-07-20 baseline.
10. Roll back for any control, timing, EtherCAT, request, or memory regression.

## Test report template

```text
Platform and OS:
glibc or macOS allocator version:
libcurl and TLS backend:
IOD commit and build type:
Sanitizer/profiler:
Fixture response sizes:
Sequential/concurrent workload:
Requests completed/errors/aborts:
Start, peak, and quiet RSS:
Start, peak, and quiet in-use bytes:
Thread/FD/socket baseline and final:
malloc_trim result (VM only):
Conclusion: live leak / allocator retention / inconclusive
Candidate change and regression results:
Raw artifact paths:
```

