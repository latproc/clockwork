# IOD WEBREQUEST Activity-Dependent Memory Growth

## Purpose

This document gives a fresh engineer or LLM enough context to continue the
memory investigation without relying on chat history. Read the reproduction
playbook in this directory before running tests.

## Installation and operational constraints

- Installation: `2G4C-120`
- Machine class: `2G4C`
- Project: Fully Automatic Two Head Grab Control System
- Production **plant** source tree on this machine: `/opt/latproc/code` (site LPC/plugins; not the Clockwork product)
- IOD source tree: `/opt/latproc/iod`
- The production machine is a real-time EtherCAT control system.
- Never run load generators, malloc-wide BPF tracing, heaptrack, Valgrind,
  ASan, or similar profilers while it is doing production work.
- Do not edit, build, deploy, restart IOD, or change live state without current
  operator approval and an agreed rollback.
- Existing monitor data can be read during production. Source and saved logs
  can be analysed offline.

## Source-control state (updated 2026-07-29)

Production investigation and fixes landed on Git branch:

```text
prod-experimental-mqtt-fix
```

Relevant commits (iod tree under `/opt/latproc`):

```text
35407c47 / earlier   Value(cJSON*) frees scalars after conversion (idle scalar path)
b985908f             Fix cJSON leaks on JSON ITEM DEFAULT and PutSubExpr copies
31fceba5             Fix cJSON leak in Value::getFromJSON for scalar fields
```

Earlier investigation branch `investigate/iod-memory-leak-2g4c-120` still has
instrumentation history (`49c1b8f6` etc.). Prefer mqtt-fix tip for fixes above.

Site plant files under `/opt/latproc/code` (this host), including `llm-rules`, are
managed with SVN rather than the parent Git repository. Check `svn status`
before committing these handoff files.

**Offline work:** remaining production-activity fixes do **not** require the
live EtherCAT controller. Use a Linux VM / second host with warehouse CW +
HTTP fixtures. See **Offline work plan (no plant required)** below and
`IOD_WEBREQUEST_REPRODUCTION_PLAYBOOK.md`.

## Runtime state when last inspected

Historical note (2026-07-21 handoff): supervised binary was often
`/opt/latproc/iod/iod_sdo-memory-fix-io-mask`.

**Long run (primary evidence, 2026-07-28 → 2026-07-29):**

```text
PID 3342133
Binary: /opt/latproc/iod/iod_sdo  (includes b985908f ITEM DEFAULT fix)
Start:  2026-07-28T05:01:22Z
End:    2026-07-29T03:46:50Z  (~22.7 hours)
RSS:    89.2 → 334.1 MiB  (+245 MiB, ~0.18 MiB/min average)
```

MEMSNAPSHOT was **on** for almost that entire interval (from ~15:42 local
2026-07-28 until process end). The low-overhead monitor (`memory.csv`) was on
the whole time.

**Re-checked 2026-07-29 ~15:05 AEST (day+ of Jul-28 release, host `2C-120`):**

```text
PID 127846  (started 2026-07-28 11:38:43, ~27.4 h)
/opt/latproc/iod/iod_sdo   (mtime 2026-07-28 10:35 Release)
RSS ~138 MiB, VSZ ~1.53 GiB, Threads 18
main [heap] ~59 MiB RSS
worker arenas: one ~27 MiB + several ~7.5 MiB
SHOW HEALTH: LOAD BUSY ~110–125 loops/s, THRASH none
```

Git branch tip includes JSON ownership commits `b985908f` and `31fceba5`, but
the **live binary does not** (build objects for Expression / PredicateAction /
value still dated 2026-07-26). Live binary does include idle-CPU, channel, and
legacy turnOn/pending-out work through `7e062d0c`.

The service remains `/etc/service/iod`. The low-overhead monitor service is
*intended* as `/etc/service/memory_monitor`, writing to:

```text
/opt/latproc/sampling/iod-memory/memory.csv
/opt/latproc/sampling/iod-memory/events.log
/opt/latproc/sampling/iod-memory/pmap-*.txt
```

**As of 2026-07-29 on 2C-120:** `memory_monitor` service and
`/opt/latproc/sampling/iod-memory/` are **not installed**. Sampler application
logs under `/opt/latproc/sampling/log-YYYYMMDD.txt` are present.

`DEBUG DEBUG_MEMSNAPSHOT on` is **runtime-only** and is cleared by every iod
restart. Re-enable after restart:

```bash
printf 'DEBUG DEBUG_MEMSNAPSHOT on;\n' | /opt/latproc/iod/iosh
```

Do not assume process IDs or binary contents stay current. Recheck with
`svstat`, `readlink /proc/<pid>/exe`, and object/binary mtimes before claiming
a fix is live.

See also **Sampler accounting** (2026-07-28 PID `3294021` request/status
reconciliation) and **2026-07-28/29 long-run findings** below.

## Two distinct memory problems

Do not merge these into one diagnosis.

### Fixed idle JSON scalar leak

The original idle leak was proven with exact cJSON node lifetime tracking. The
allocation stack was:

```text
cJSON_New_Item
  cJSON_Parse
  apply(std::string const&, cJSON*, MachineInstance*)
  ExpressionValue::operator()()
  resolve / eval
  PredicateAction::run()
```

`apply()` returned a newly allocated JSON scalar. `Value` converted scalars to
plain integer/string/bool values but dropped the owned cJSON node. The result
was exactly 24 retained cJSON nodes per minute while idle.

The fix in `35407c47` makes `Value(cJSON *)` and
`Value::operator=(cJSON *)` delete the owned cJSON input when conversion
produces a non-JSON scalar. Regression tests cover construction and assignment.

Before the fix, a two-minute exact trace retained 48 nodes on this stack.
After the fix, a drained trace (track for 60 seconds, stop tracking new nodes,
allow five seconds for in-flight work to finish) reported zero survivors.

Do not reopen this diagnosis merely because RSS changes. First prove that this
exact cJSON stack is retaining nodes again.

### Open production-activity growth (still open after idle fixes)

After the scalar and ITEM DEFAULT fixes, IOD remains **flat overnight / idle**,
then grows while production HTTP/JSON traffic runs. This is **live** growth
(`malloc_in_use` and `cjson_nodes` rise together; free/releasable stay small).

Historical UTC values for PID `1667720` on 2026-07-20:

```text
22:11  RSS approximately 102.4 MiB
23:11  RSS approximately 171.1 MiB
```

That is about 68.7 MiB in one hour (~50 bales reported → ~1.37 MiB RSS per
bale as correlation only).

**Stronger 22.7 h evidence (PID 3342133, 2026-07-28/29)** is in the long-run
section below. Night was flat; morning/day production drove cjson into the
millions.

## 2026-07-28/29 long-run findings (2G4C-120)

### What was monitored

| Source | Coverage |
|--------|----------|
| `memory.csv` | Continuous for all PIDs (daemontools `memory_monitor`) |
| MEMSNAPSHOT | On for PID 3342133 from ~15:42 local 28 Jul until death ~13:46 29 Jul |
| Sampler curl accounting | 28 Jul restart window (see below) |

### Three problems, do not merge

| # | Problem | Status | Night? | Production day? |
|---|---------|--------|--------|-----------------|
| 1 | Idle scalar cJSON from `apply` / `Value(cJSON*)` | **Fixed** earlier | was drip | n/a |
| 2 | `ITEM ${f} OF json DEFAULT` leak when field is JSON null | **Fixed** `b985908f` | **flat** after fix | reduced drip |
| 3 | Production live cJSON + worker-arena growth | **Open** | no growth | **large** |

### Overnight proof that idle paths are fixed

During PID 3342133, first MEMSNAPSHOT of each hour:

```text
28 Jul 17:00 – 29 Jul 08:00 local (approx)
  cjson_nodes     = 449281 every hour
  malloc_in_use   = 143.2 MiB every hour
```

If the DEFAULT/null or idle scalar leaks were still active, cjson would climb
through the night. It did not.

### Daytime production growth (same process)

| Window (local) | cjson | malloc_in_use | Rough rate |
|----------------|------:|--------------:|------------|
| 16:00–17:00 28 Jul | 384k → 449k | 135 → 143 MiB | moderate |
| 17:00–08:00 | **flat 449k** | **flat 143 MiB** | zero |
| 09:00–10:00 29 Jul | 449k → 1.00M | 143 → 212 MiB | **~+430k nodes/h** |
| 10:00–12:00 | 1.00M → 1.72M | 212 → 300 MiB | **~+360k nodes/h** |
| End ~13:46 | **~1.80M** | **~311 MiB** | |
| RSS whole run | 89 → **334 MiB** | | ~0.18 MiB/min avg |

Main `[heap]` mapping stayed ~**73–74 MiB**. Growth sat in:

- live **cJSON** (dominant for `malloc_in_use`);
- **worker-thread anon arenas** (glibc, WEBREQUEST `pthread_create` per request)
  for RSS beyond main-heap live bytes.

Free arena bytes stayed ~4 MiB; releasable ~tens of KiB. This is **not** “mostly
freed memory retained by glibc.”

### Curl Request vs HTTP 200 (not a reliability issue)

See **Sampler accounting** below. Raw greps double-count property clears
(`Request=""`, `Status=0`). Real success rate ~97% in the measured window.

### Fixes shipped on plant binary (as of long run)

Deployed on PID 3342133:

- `b985908f` ITEM DEFAULT + PutSubExpr (no Print+Parse re-clone after assign)

Committed after long run, **built Release but not necessarily deployed**:

- `31fceba5` `Value::getFromJSON` scalar clone free (same ownership class as
  `Value(cJSON*)`; smaller call surface than `apply`/ITEM)

Rollback binary kept on plant as `iod_sdo.prev-memfix-*`.

## Offline work plan (no plant required)

Remaining work is suitable for a **Linux VM / lab host** with warehouse CW +
loopback HTTP fixtures. Do **not** need EtherCAT or 2G4C-120.

### A. Prove retained JSON is live application state vs still-leaked stacks

1. Checkout `prod-experimental-mqtt-fix` with `b985908f` + `31fceba5`.
2. Build Release and Debug; run `test_json_ownership`, `test_value`,
   `test_json_value`.
3. Minimal CW: machines that `SEND start TO curl`, store `result := curl.Result`,
   and run many `ITEM ${...} OF result DEFAULT ...` like warehouse API LPC.
4. Drive with synthetic catalog responses (1 KiB / 16 KiB / 128 KiB / 1 MiB).
5. Measure `cJSON_LiveNodeCount` or MEMSNAPSHOT-equivalent:
   - idle after traffic → must stay flat (regressions);
   - repeated full catalog assign without clear → working-set can grow if CW
     keeps copies (expected, not necessarily iod bug).

### B. WEBREQUEST / glibc worker arenas (primary open iod change)

Files:

```text
iod/src/exec_web_request.c
iod/src/Plugin.cpp          setJsonValue
```

Design direction (smallest safe redesign):

1. **Thread pool or serial worker** instead of `pthread_create` per request
   (stops unbounded arena high-water from short-lived threads).
2. Allocate response buffer and parse **on the completing control path** in a
   consistent arena, or free in the same thread that allocated.
3. Keep existing cleanup: `curl_easy_cleanup`, slist free, join, free result
   body after `setJsonValue`.
4. Optional: clear CW `Result` after consumers extract fields (CW LPC change,
   warehouse) to shrink live working set.

Acceptance:

- equal request count and response sizes vs baseline;
- Linux RSS / private dirty plateau or much lower slope under catalog load;
- `malloc_in_use` / cjson do not climb unboundedly when Result is not retained
  in CW properties.

### C. Cheap iod cleanups (can land with A)

| Item | Notes |
|------|--------|
| `apply()` use `cJSON_Duplicate` instead of Print+Parse | Same ownership; less CPU/frag |
| CW clear large `result` / `curl.Result` after extract | Working-set, not always a leak |
| Reduce `P_BaleCatalogForAssignment` poll rate | Symptom relief; measure first |
| Deploy `31fceba5` | Ownership completeness |

### D. What not to reopen

- Idle 24 nodes/min scalar path after `Value(cJSON*)` free-on-convert.
- Night-flat MEMSNAPSHOT as “leak still idle” without proving a **new** stack.
- Curl Request count vs Status 200 without tab-aware empty/clear accounting.

## Memory-map evidence

The main anonymous/heap-like mapping remained approximately constant:

```text
000055ad23a74000  about 74 MiB RSS/dirty
```

A worker-thread arena changed substantially:

```text
2026-07-20T22:11Z  00007f94b8000000 about 7.3 MiB RSS/dirty
2026-07-20T23:11Z  00007f94b8000000 about 65.2 MiB RSS/dirty
```

Several smaller worker arenas also appeared. This locates the growth in
worker-thread allocator arenas rather than the main mapping. It does not by
itself distinguish live leaked objects from freed memory retained by glibc.

## Activity correlation

The sampler log was:

```text
/opt/latproc/sampling/log-20260721.txt
```

During the observed production period it contained approximately:

- 52 grab-chamber bale-move completions;
- 441 `P_BaleCatalogForAssignment.curl` completions;
- about 2,000 total curl-related request/result events, depending on the exact
  event filter.

The large difference between bale count and HTTP request count makes repeated
WEBREQUEST activity a stronger allocation-rate match than one allocation per
bale. Recalculate exact counts from the source log when precision matters.

## Sampler accounting: why `curl.Request` count ≠ HTTP 200

**Date:** 2026-07-28 (iod PID `3294021`, ~50 minutes after 22:52 UTC restart)

**Log:** `/opt/latproc/sampling/log-20260728.txt`  
**Window:** `20260727T225255` – `20260727T234500` UTC

Naive greps of the sampler log look like a large gap between “requests” and
“HTTP 200”. That gap is mostly a **counting artefact**, not missing responses.

### Sampler record shape (tabs matter)

Property and state lines are tab-separated. Terminal display collapses tabs, so
records look like `curl.Statusvalue200` or `P_Foo.curlIdle7`. Actual fields:

```text
TIMESTAMPZ<TAB>path<TAB>field<TAB>payload
curl.Request    value   "http://..."
curl.Status     value   200
curl.Result     value   {...}
P_Foo.curl      Idle    7
P_Foo.curl      Start   34
P_Foo.curl      Running 66
P_Foo.curl      Done    67
```

Parse with tabs. Do not match concatenated `Statusvalue` / `curlIdle` strings.

### Counts in the restart window

| Metric | Count | Meaning |
|---|---:|---|
| `curl.Request` property changes | 4569 | URL set **or** clear to `""` |
| nonempty `Request` (real URL) | **2288** | Actual outbound request intent |
| `Request` cleared to `""` | **2281** | Lifecycle cleanup, not a new HTTP call |
| `curl.Status` property changes | 4568 | HTTP code **or** reset to `0` |
| Status `200` | **2229** | HTTP success |
| Status `404` | **58** | Real HTTP failure (`bale not found`) |
| Status `0` | **2281** | Property reset after Done — **not** HTTP 0 |
| Machine `.curl` Start | **2288** | Matches nonempty Request |
| Machine `.curl` Done | **2287** | Matches 200+404 |

**Correct comparison:**

```text
nonempty Request  2288
Start             2288
Done              2287
Status 200+404    2287   (2229 + 58)
Status 0          2281   == empty Request clears
```

So:

- **Request_total vs HTTP 200** (~4569 vs ~2229) is misleading: about half of
  Request/Status samples are **idle cleanup** (clear URL / force Status to 0).
- **Real success rate** for this window: **2229 / 2288 ≈ 97.4% HTTP 200**.
- Residual real failures: **58 × HTTP 404**, body
  `{"detail":"bale not found"}` / `{"detail":"Bale not found"}`.
- Unaccounted after 200+404: **1** (boundary / still in flight).

Typical success cleanup sequence (shared leaf properties):

```text
curl.Status = 200
curl.Result = <payload>
curl.Status = 0          ← reset
curl.Request = ""        ← reset
curl.Result = ""         ← reset
machine.curl → Idle
```

### Real 404 owners (not counting noise)

Nearest Done after Status 404 in this window:

| Machine | ~404 Done |
|---|---:|
| `M_GrabBaleGateRFIDUpdate.Request` | 48 |
| `M_*LoadObj` (chamber load object) | ~10 |

These are legitimate API “bale not found” responses during RFID/load paths.
They do **not** explain the Request-vs-200 gap; they explain the small
nonempty-Request vs 200 gap (~59 including the single unaccounted).

### Dominant real request traffic (nonempty URL bases)

| Count | URL base |
|---:|---|
| 722 | `/api/v1/stations/bales/{id}` (`M_StationJSONUpdate.Request`) |
| 616 | `/api/v1/stations/bales/` (`P_BaleCatalogForAssignment`) |
| 522 | `/api/v1/bales/{id}` |
| 123 | `/api/v1/bales/{id}/move` |
| 65 | `/api/v1/scans` (RFID capture) |

`P_BaleCatalogForAssignment` alone: **616 Start/Done** in ~50 minutes
(~12/min) while production is active — still the strongest WEBREQUEST
amplification candidate for arena growth.

### Counting rules for future analysis

1. Split sampler lines on **TAB**.
2. Count **nonempty** `curl.Request` (or machine `.curl` **Start**), not all
   Request property changes.
3. Treat Status **`0`** as cleanup unless a Done path documents otherwise.
4. Compare Start ≈ nonempty Request ≈ Done ≈ (Status 200 + other HTTP codes).
5. Only then treat 200-rate or 404-rate as an HTTP health signal.
6. Shared leaf names `curl.Request` / `curl.Status` / `curl.Result` interleave
   under concurrency; per-URL pairing is approximate. Prefer machine
   `Start`/`Done` for volume, Status histogram for outcomes.

### Link back to memory growth

This accounting does **not** remove WEBREQUEST as the growth driver. It
**raises** confidence that nearly all Starts complete (Done≈Start) and that
~2.3k full request cycles ran in ~50 minutes on this restart — consistent with
~1.25 MiB/min RSS growth in worker arenas over the same interval. The previous
naive “half the requests never get 200” story was wrong; the process is doing
roughly one completed HTTP cycle per Start, plus an equal number of property
clears that inflate raw Request/Status greps.

Important CW configuration:

```text
warehouse/config/panel.lpc
  P_BaleCatalogForAssignment BALESWITHFILTER S_SamplingLineAPI;

warehouse/lib/api/samplingline_api.lpc
  BALESWITHFILTER MACHINE Settings
    curl WEBREQUEST (...)
```

`P_BaleCatalogForAssignment` can request the catalog repeatedly while the
assignment display logic is active. Separately review the requesting state
machine for unintended polling. Reducing excessive requests may reduce the
symptom but is not proof that WEBREQUEST ownership is correct.

## WEBREQUEST implementation and ownership chain

Key implementation files:

```text
/opt/latproc/iod/src/exec_web_request.c
/opt/latproc/iod/src/Plugin.cpp
/opt/latproc/iod/src/MachineInstance.cpp
/opt/latproc/iod/src/symboltable.cpp
/opt/latproc/iod/src/value.cpp
```

Current request chain:

```text
CW SEND start TO curl
  exec_web_request() creates WebRequestData
  pthread_create() starts a new worker for this request
  worker calls curl_easy_init()
  write_cb malloc/reallocs data->result in the worker thread
  worker calls curl_easy_cleanup() and exits
  control thread observes data->done and pthread_join()s
  setJsonValue("Result", data->result) parses/copies JSON into CW Value storage
  control thread frees data->result and WebRequestData
```

Source inspection found cleanup calls for:

- `curl_slist_free_all(headers)`;
- `curl_easy_cleanup(curl)`;
- `pthread_join(data->thread, NULL)` on normal completion and completed abort;
- request, post body, content type, method, result, errors, and WebRequestData;
- replacement of the previous CW `Result` property through `Value` assignment.

No simple missing `free` was identified in the normal successful path.

## Leading hypothesis

The leading hypothesis is allocator retention caused by the plugin's
thread-per-request and cross-thread ownership design:

- curl and response storage allocate in a short-lived worker thread;
- response parsing and final free happen in the long-lived control thread;
- glibc creates/uses worker arenas;
- freed blocks and arenas may remain resident rather than returning to the OS;
- repeated or concurrent WEBREQUEST instances increase arena high-water marks.

This is not yet proven. Alternatives include:

- a genuine libcurl/OpenSSL/thread-local leak;
- an error or abort path that is not joining/cleaning consistently;
- a JSON property-copy leak outside the already-fixed scalar path;
- unbounded retained CW messages/actions triggered by large Result updates;
- excessive concurrent requests causing expected high-water growth;
- repeated catalog polling that amplifies a smaller per-request defect.

## Concurrency correctness concern

`WebRequestData.done` and `WebRequestData.abort` are plain `int` values read and
written by different threads without atomics or a mutex. That is a C data race.
It should be corrected in any redesign, but do not claim it is the memory cause
without reproduction evidence.

## What has not been proven

- Whether `mallinfo2().uordblks` grows with RSS under production activity.
- Whether `malloc_trim(0)` would return most of the growth.
- Whether macOS reproduces the growth.
- Whether Linux reproduces it without the full warehouse configuration.
- Whether the growth plateaus after enough requests.
- Which response types or error paths contribute most.
- Whether request concurrency or response size is the dominant variable.

## Evidence-quality rules

- RSS alone does not prove a live leak.
- A growing glibc arena alone does not prove a live leak.
- ASan/LSan reporting no leak does not disprove allocator retention.
- A plateau on macOS does not disprove Linux glibc retention.
- A fix is not accepted because RSS is lower after restart.
- Compare equal request counts and response workloads after warm-up.
- Separate outstanding/live bytes from resident/retained bytes.

