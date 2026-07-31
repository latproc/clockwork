# Grok / agent install instructions

**Package:** `iod-memory-investigation`  
**Source plant:** 2G4C-120 (`prod-experimental-mqtt-fix`)  
**Goal:** Install investigation docs + low-overhead memory monitoring on another
IOD host; optionally apply ownership patches offline. Do **not** restart plant
iod without operator approval.

---

## Safety (read first)

1. This is a **real-time EtherCAT** environment on many sites.
2. **Default install does not restart iod, does not replace binaries.**
3. Never run Valgrind, ASan, heaptrack, or full malloc BPF on a live control
   process without explicit approval.
4. `DEBUG_MEMSNAPSHOT` is low rate (~1 line/min). Prefer enabling in
   `/opt/latproc/etc/iod.conf` for persistence across restarts; also toggle at
   runtime via iosh. Still get operator OK if the site is cautious about debug flags.
5. Applying patches requires a **source tree build** and a controlled deploy —
   that is a separate step after install.

---

## 1. Unpack

```bash
# Example locations
cd /opt/latproc   # or wherever latproc lives
tar xzf iod-memory-investigation-YYYYMMDD.tar.gz
cd iod-memory-investigation   # or path from tar
```

If the tarball expands with a versioned directory name, `cd` into it.

Set root if not `/opt/latproc`:

```bash
export LATPROC=/opt/latproc
```

---

## 2. Install docs + tools (always)

```bash
chmod +x install.sh scripts/*.sh tools/iod_memory_monitor.sh
./install.sh
# or: LATPROC=/path/to/latproc ./install.sh
```

This copies:

- docs → `$LATPROC/docs/iod-memory-investigation/` (+ llm-rules/cw_issues if present)
- `iod_memory_monitor.sh` → llm-rules/tools or `$LATPROC/bin`
- helper scripts → `$LATPROC/scripts/`

---

## 3. Install continuous memory monitor (recommended)

```bash
./install.sh --monitor
```

Creates:

- `/etc/service/memory_monitor` (daemontools) if `/etc/service` exists
- data dir: `/opt/latproc/sampling/iod-memory/`
  - `memory.csv` — ~30 s samples
  - `events.log` — iod start/stop
  - optional `pmap-*.txt`

**If the plant is not named `2GRAB`**, the packaged monitor accepts:

```bash
export IOD_MEMORY_IOD_NAME=YourPlantName   # matches --name on iod cmdline
# or pin PID:
export IOD_MEMORY_IOD_PID=12345
```

For a one-shot check:

```bash
$LATPROC/scripts/iod_memory_status.sh
```

---

## 4. Enable MEMSNAPSHOT (optional, after iod is up)

**Persist across restarts** (recommended when investigating):

```bash
# In $LATPROC/etc/iod.conf — enable the token (uncomment / add):
#   DEBUG_MEMSNAPSHOT
```

**Runtime** (also re-assert after restart if conf was not updated):

```bash
$LATPROC/scripts/iod_enable_memsnapshot.sh
# or:
printf 'DEBUG DEBUG_MEMSNAPSHOT on;\n' | /opt/latproc/iod/iosh
```

Watch (iod-elc stream filter; **not** journalctl by default):

```bash
tail -f /opt/latproc/sampling/iod-memory/memsnapshot.log
```

Lines appear after process age ≥ ~5 minutes, then ~1/min while the flag is on.

**Full stderr/stdout** (PROCSNAP, ECDOMAIN, etc.) without restarting iod:

```bash
$LATPROC/scripts/iod_verbose.sh on 1800    # or: echo 1800 > /tmp/iod-verbose
tail -f /tmp/iod.log
$LATPROC/scripts/iod_verbose.sh off
```

Requires a site iod boot stream filter that honors `/tmp/iod-verbose` (if used).
See `/opt/latproc/TRANSPORT.md`. Plant LPC/rules may live under a site tree.

---

## 5. Apply ownership patches (offline / lab / approved source tree)

Patches live in `patches/`:

| File | Fix |
|------|-----|
| `01-item-default-putsubexpr.patch` | ITEM DEFAULT null leak + PutSubExpr no double parse |
| `02-getfromjson-scalar.patch` | Value::getFromJSON scalar clone free |
| `03-webrequest-worker-pool-apply-clone.patch` | WEBREQUEST worker pool + apply Duplicate (larger) |

Dry-run:

```bash
./install.sh --apply-patches --dry-run
```

Apply (only after dry-run looks clean):

```bash
./install.sh --apply-patches --force
```

Then **build** the site’s normal iod Release target (example):

```bash
cd $LATPROC/iod/build/Release   # or site-specific build dir
cmake --build . --target iod_sdo -j$(nproc)
# stage under a new name, e.g. iod_sdo-memory-handoff
# deploy + restart ONLY with operator approval and rollback binary preserved
```

If `git apply` / `patch` fails, the tree may already contain the fix — compare
with docs and `git log --grep=cJSON`.

---

## 6. What to read (in order)

1. `docs/IOD_WEBREQUEST_MEMORY_GROWTH_20260721.md` — **long-run findings** + offline plan  
2. `docs/IOD_WEBREQUEST_REPRODUCTION_PLAYBOOK.md` — how to reproduce without plant  
3. `docs/MEMORY_LEAK_INVESTIGATION.md` — ownership methodology + examples B/C/D  
4. `docs/OPEN_WORK_PLAN.md` — Track F status  

---

## 7. Success criteria (monitoring week)

| Observation | Meaning |
|-------------|---------|
| Overnight / idle: flat `cjson_nodes` and `malloc_in_use` | Idle ownership OK |
| Busy day: rise then **plateau** | Working set, not pure drip leak |
| Busy day: unbounded linear climb all shift | Still open (Result retention / WEBREQUEST) |
| RSS tracks `malloc_in_use`; free/releasable small | Live retention, not just glibc free pool |

---

## 8. Do not do by default

- Restart iod “to pick up docs”
- Force-push or overwrite production `iod_sdo` without staging name + rollback
- Load generators or heap profilers on live EtherCAT
- Assume curl Request count ≠ HTTP 200 is a failure (see docs: sampler clears)

---

## Package layout

```text
iod-memory-investigation/
  README.md
  GROK_INSTALL.md          ← this file
  install.sh
  docs/
  tools/
    iod_memory_monitor.sh
    iod_memory_report.py
    service/memory_monitor.run
  patches/
    01-… 02-… 03-…
  scripts/
    iod_memory_status.sh
    iod_enable_memsnapshot.sh
```
