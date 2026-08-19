EtherCAT transport source: `/opt/etherlab-cyclic-kmod`

| Install | Path |
|---------|------|
| Kernel module (DKMS) | `elc-ethercat` → `/lib/modules/$(uname -r)/updates/dkms/elc_ethercat.ko` |
| Userland lib + headers | `make install-lib` → **`/usr/local`** (`libelcethercat`, `elc_ethercat.h`) |
| pkg-config | `/usr/local/lib/pkgconfig/elcethercat.pc` |
| Tools (`elc_bus`, `elc_sdo`, …) | `make tools` → tree `tools/`; **copy to `/usr/local/bin`** for boot (DKMS install does **not** install tools) |
| Legacy prefix (optional) | `/opt/elc` still works as fallback |

Docs (in source tree):

  /opt/etherlab-cyclic-kmod/docs/libelcethercat.md
  /opt/etherlab-cyclic-kmod/docs/uapi.md
  /opt/etherlab-cyclic-kmod/docs/developer-guide.md
  /opt/etherlab-cyclic-kmod/docs/process-image-exchange.md
  /opt/etherlab-cyclic-kmod/docs/iod-session-handoff.md
  /opt/etherlab-cyclic-kmod/docs/client-slave-recovery.md
  /opt/etherlab-cyclic-kmod/docs/recommended-master-lifecycle.md

**PDO map / power-return setup (plant + elc):** Mapping CoE (`0x1600` /
`0x1C12` / …) must run in **PREOP or SAFEOP**, not OP.

- **Cold start:** `svc -d /etc/service/iod`, wait until slaves are PREOP
  (`elc_bus`), then `svc -u`. If iod starts onto already-OP drives, recipes
  skip (`already OP; CoE unchanged`) — ED3L then stays mode 1 / accel 0
  (A.76, shaft does not turn, no 0x603F).
- **Device / domain power-down while iod is up:** slave INIT or not-visible
  sets `needs_commission`. Reapply must reach PREOP (setup-hold from OP is
  allowed **only** on that path). A slave that never left OP still skips.
- **elc API 0.19** setup-hold (`ELC_CAP_SETUP_HOLD`) holds servos in PREOP
  while cyclic stays up — `docs/client-slave-recovery.md` §9, `docs/uapi.md`.
  Do not apply `0x1C12` in OP (abort 0x08000022 / 1G2C-122 2026-08-11 wedge).

Git lines (plant iod vs clients — not bus load params):

  iod/docs/BRANCHES.md
  iod/docs/LEGACY_ECRT_REMOVAL_PLAN.md

  A  prod-experimental-mqtt-fix          → iod_sdo (legacy IgH ecrt; only place ecrt lives)
  B  feature/iod-elc-kernel-transport     → iod-elc **only** (kernel elc; no iod/iod_sdo build)
  C  prod-client-zmq-fix                  → humid/modbusd/dbd/persistd cw_client

This checkout (B) is the plant path below. For IgH userland `iod_sdo`, use line A.

module=elc_ethercat  
device=/dev/elc_ethercat0  
library=libelcethercat  

## Module load (required for arm)

The cyclic kthread must be real-time or domain working-counter flaps
`ELC_IO_FAULT_DOMAIN_INCOMPLETE` (0x20) under load → `bus_healthy=0` → outputs
publish but never arm (even with 34/34 OP and link up).

**Boot order (1G2C-122):** `daemontools-run.service` must start **after**
`ethercat.service`. `iod-elc.sh` `modprobe elc_ethercat` depends on `ec_master`;
if that happens first, IgH loads with empty `main_devices` and `/dev/EtherCAT0`
never appears (`ethercatctl start` is a no-op). Host drop-in:

`/etc/systemd/system/daemontools-run.service.d/10-after-ethercat.conf`  
(source: `code/config/scripts/systemd/daemontools-run.service.d/`).

`iod-elc.sh` also refuses to load `elc_ethercat` until `/dev/EtherCAT0` exists
and `main_devices` is non-empty.

**Plant path: DKMS + modprobe** (RT defaults in `/etc/modprobe.d/elc_ethercat.conf`):

```sh
# stop iod first, then:
rmmod elc_ethercat 2>/dev/null || true
modprobe elc_ethercat   # uses cycle_cpu=1 cycle_fifo_priority=90 from modprobe.d
# or explicit:
# modprobe elc_ethercat cycle_cpu=1 cycle_fifo_priority=90

# verify:
cat /sys/module/elc_ethercat/parameters/cycle_fifo_priority   # 90
cat /sys/module/elc_ethercat/parameters/cycle_cpu              # 1
ls -l /dev/elc_ethercat0
dkms status | grep elc
```

**Fallback (no DKMS):** out-of-tree `.ko` via `ELC_KO=…` / insmod:

```sh
KO=${ELC_KO:-/opt/etherlab-cyclic-kmod/kernel/elc_ethercat.ko}
insmod "$KO" cycle_cpu=1 cycle_fifo_priority=90
```

**Generic product starter:** `scripts/iod-elc.sh` loads `elc_ethercat` (modprobe,
then optional `ELC_KO` insmod), optionally reloads if soft-RT, waits for link,
and execs `iod-elc`. Site LPC dirs / process name / persist path are **env or
argv**, not hardcoded plant trees.

```sh
# example (site supplies Clockwork source dirs):
/opt/latproc/scripts/iod-elc.sh --name MYCELL \
  /path/to/plant/lib /path/to/plant/config
# optional: IOD_PERSIST=… IOD_MODBUS_MAP=… IOD_STREAM_FILTER=1 …
```

**Important:** restarting iod alone does **not** reload the module. If
`cycle_fifo_priority` is still `0`, the kthread is created soft-RT at every
`cycle_activate`.

Mitigations (in order):

1. `/etc/modprobe.d/elc_ethercat.conf` + modprobe with RT params (site boot may
   reload if soft-RT while free).
2. After `cycle_activate`, **`iod-elc`** promotes `elc_cycle` to SCHED_FIFO 90 / CPU 1
   (`KernelEthercatBus::ensureCycleThreadRealtime`).
3. Manual: `chrt -f -p 90 $(pgrep -x elc_cycle); taskset -cp 1 $(pgrep -x elc_cycle)`.

Verify: `ps -eLo class,rtprio,psr,comm | grep elc_cycle` → `FF  90  1 elc_cycle`.
Log on activate may also show: `elc_cycle tid=… promoted SCHED_FIFO prio=90 cpu=1`.

### One-command plant helper (in transport tree)

```sh
cd /opt/etherlab-cyclic-kmod
./scripts/elc-plant.sh status
./scripts/elc-plant.sh setup          # install-userland + reload-module + verify
./scripts/elc-plant.sh install-userland
./scripts/elc-plant.sh reload-module  # stops iod, modprobe, starts iod
./scripts/elc-plant.sh verify
```

Or step-by-step:

```sh
cd /opt/etherlab-cyclic-kmod
make dkms-install          # kernel only
make install-lib           # headers + lib → /usr/local + ldconfig
make install-tools         # elc_bus elc_sdo … → /usr/local/bin
```

### Userland rebuild note

```sh
# after make install-lib to /usr/local:
export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}
# rebuild iod-elc (CMake prefers /usr/local then /opt/elc)
```

## Rates (bus vs Clockwork)

| Knob | Meaning | Default |
|------|---------|---------|
| `SYSTEM.CYCLE_DELAY` | EtherCAT bus period (µs), **frozen at activate** (no live retune) | **500** (2 kHz) |
| `SYSTEM.POLLING_DELAY` | CW process-data pull (µs); adjustable anytime | **2000** (500 Hz) |

Kernel RT cycles at `CYCLE_DELAY`. Userspace takes a full input snapshot only
when a CW pull is due (`POLLING_DELAY`); intermediate bus frames are dropped
for Clockwork (latest sample wins). Outputs publish only when the commanded
shadow changes.

## Data currency (iod-elc)

Kernel contract (API 0.16):

- RT cyclic task runs at `SYSTEM.CYCLE_DELAY` (bus period).
- After each successful domain receive, the kernel publishes a **coherent**
  full process image into a double buffer and advances `input_sequence`.
  `ELC_IOC_GET_INPUT_SNAPSHOT` always returns the **current active** buffer
  (latest published image), never a half-written one. If a reader holds the
  buffer, the RT task skips that cycle’s publish rather than overwrite.
- Outputs use a separate double buffer. `ELC_IOC_PUBLISH_OUTPUT` updates the
  inactive shadow; the RT task applies only the **active published** image at
  the cycle boundary. Arm requires exact `config_generation` + latest
  nonzero `output_sequence` while `bus_healthy`.

iod-elc userspace:

| Rate | What | Currency |
|------|------|----------|
| `SYSTEM.CYCLE_DELAY` | `receiveState` → snapshot into `domain1_pd`; late re-snapshot before publish; `sendUpdates` publish+arm | Always latest kernel image / latest commanded shadow |
| `SYSTEM.POLLING_DELAY` | `collectState` + ZMQ to Clockwork | Latest sample at pull time; intermediate bus frames **dropped** for CW (no history yet) |

Failed snapshot ioctl keeps the previous `domain1_pd` (last known current), never zeros it. Commanded outputs live in `g_kernel_output_*` and are merged into the snapshot for collect/processAll.

Future: optional `CAP_INPUT_HISTORY` for short pulses between CW pulls.

## Getting going (minimal)

```sh
# 1) transport (once)
cd /opt/etherlab-cyclic-kmod && make dkms-install && make install-lib && make install-tools
# RT defaults: /etc/modprobe.d/elc_ethercat.conf  (see Module load above)

# 2) bus map: product sample etc/elc_topology.conf, or plant path via
#    ELC_TOPOLOGY_CONFIG=/path/to/plant/elc_topology.conf

# 3) build iod-elc
cd /opt/latproc/iod && mkdir -p build-elc && cd build-elc
cmake .. -DBUILD_IOD_ELC=ON -DCMAKE_BUILD_TYPE=Release && make -j"$(nproc)" iod-elc

# 4) start (pass YOUR Clockwork source dirs)
/opt/latproc/scripts/iod-elc.sh --name MYCELL /path/to/cw/lib /path/to/cw/config
```

| Path | Purpose |
|------|---------|
| `etc/elc_topology.conf` | **Product sample** topology (`ELC_TOPOLOGY_CONFIG` for plant maps) |
| `etc/recipes/*.recipe.in` | **Product sample** servo CoE listings (copy/adapt in plant tree) |
| `etc/iod.conf` | Debug flags (`-c`) |
| `scripts/iod-elc.sh` | Load module + run `iod-elc` |
| `etc/README.md` | Short onboarding for this directory |

**Service wrapper:** site-owned (daemontools/systemd) may call `scripts/iod-elc.sh`
with plant paths. Plant LPC/plugins are **not** part of generic Clockwork.

Stream filter (MEMSNAPSHOT / runtime verbose): set `IOD_STREAM_FILTER=1` or
pass `-v` / `IOD_LOG=…` to `scripts/iod-elc.sh`. Full ECDOMAIN / PROCSNAP file
logging is opt-in via `/tmp/iod-verbose` when the filter is running.

### Opt-in verbose log (runtime, auto-off by TTL)

**No `svc -t` / restart required** to enable or disable after a site stream
filter is running (one deploy restart installs the filter if the site uses it).

| Action | How |
|--------|-----|
| Enable (≤1 h default TTL) | `touch /tmp/iod-verbose` **or** `scripts/iod_verbose.sh on` |
| Custom TTL (seconds) | `echo 1800 > /tmp/iod-verbose` **or** `scripts/iod_verbose.sh on 1800` |
| Renew mtime / TTL | `touch /tmp/iod-verbose` **or** `scripts/iod_verbose.sh renew [ttl]` |
| Disable | `rm -f /tmp/iod-verbose` **or** `scripts/iod_verbose.sh off` |
| Status | `scripts/iod_verbose.sh status` |
| One-shot foreground | run `iod-elc` with site logging as needed (not under svc) |

Log file (when on): `/tmp/iod.log` (override with `IOD_LOG_FILE` or `IOD_LOG=`).
Helper: `/opt/latproc/scripts/iod_verbose.sh`.

Guards against filling the system:

1. **Default off** — no switch → no verbose file (MEMSNAPSHOT still captured if filter supports it).
2. **TTL** — filter re-checks switch every ~2s; if mtime age ≥ TTL (file content
   = seconds, or `IOD_LOG_TTL_SEC`, default **3600**), switch is **removed** and
   verbose file logging stops (iod keeps running).
3. **Size cap** — if the log is ≥ **50 MiB** (`IOD_LOG_MAX_BYTES`), it is
   rotated to `iod.log.1`.

Env overrides: `IOD_VERBOSE_SWITCH`, `IOD_LOG_FILE`, `IOD_LOG`,
`IOD_LOG_MAX_BYTES`, `IOD_LOG_TTL_SEC`, `IOD_VERBOSE_POLL_SEC`,
`IOD_STREAM_FIFO`, `IOD_MEMSNAPSHOT_LOG`.

## Open work (task list pointer)

Backlog for elc plant work is tracked in site notes / issues (not under a
generic `code/` path in this product tree). See also `iod/OPEN_WORK_PLAN.md`
on this branch.

**Stage 4 (2026-07-27):** dual-domain servo control power-off is
**proven at elc and under CW/iod-elc**. elc: domain 2 incomplete, domain 1
valid (`/opt/etherlab-cyclic-kmod/docs/testing.md` *Live domain bus firewall*).
CW: after `ECDomain_*` status fix, plant re-prove logged
`ECDomain_2 COMPLETE -> INCOMPLETE` (`faults=0x20`) on power-off and both
COMPLETE / 34/34 after restore.

Do not start `iod-elc` / reload the module while kernel-module work is
in progress without coordinating with the operator.
