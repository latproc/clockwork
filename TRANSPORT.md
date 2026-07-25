EtherCAT transport source: /opt/etherlab-cyclic-kmod
Installed prefix (headers + libelcethercat): /opt/elc

Docs:
  /opt/etherlab-cyclic-kmod/docs/libelcethercat.md
  /opt/etherlab-cyclic-kmod/docs/uapi.md
  /opt/etherlab-cyclic-kmod/docs/developer-guide.md
  /opt/etherlab-cyclic-kmod/docs/process-image-exchange.md
  /opt/etherlab-cyclic-kmod/docs/iod-session-handoff.md

module=elc_ethercat
device=/dev/elc_ethercat0
library=libelcethercat

## Module load (required for arm)

The cyclic kthread must be real-time or domain working-counter flaps
`ELC_IO_FAULT_DOMAIN_INCOMPLETE` (0x20) under load → `bus_healthy=0` → outputs
publish but never arm (even with 34/34 OP and link up).

**This host does not install the module under `/lib/modules`.** Use the
out-of-tree `.ko` (operator-guide style):

```sh
# path (override with ELC_KO=…):
KO=/opt/etherlab-cyclic-kmod/kernel/elc_ethercat.ko

# stop iod first, then:
rmmod elc_ethercat 2>/dev/null || true
insmod "$KO" cycle_cpu=1 cycle_fifo_priority=90

# verify:
cat /sys/module/elc_ethercat/parameters/cycle_fifo_priority   # 90
cat /sys/module/elc_ethercat/parameters/cycle_cpu              # 1
ls -l /dev/elc_ethercat0
```

`iod-elc.sh` loads via `insmod` with those params when the device is missing,
and reloads (rmmod+insmod) if priority is still 0 and the module is free.

**Important:** restarting iod alone does **not** reload the module. If
`cycle_fifo_priority` is still `0`, the kthread is created soft-RT at every
`cycle_activate`.

Mitigations (in order):

1. `iod-elc.sh` `insmod` with RT params; reload if soft-RT while free.
2. After `cycle_activate`, `iod-elc` promotes `elc_cycle` to SCHED_FIFO 90 / CPU 1
   (`KernelEthercatBus::ensureCycleThreadRealtime`).
3. Manual: `chrt -f -p 90 $(pgrep -x elc_cycle); taskset -cp 1 $(pgrep -x elc_cycle)`.

Verify: `ps -eLo class,rtprio,psr,comm | grep elc_cycle` → `FF  90  1 elc_cycle`.
Log on activate may also show: `elc_cycle tid=… promoted SCHED_FIFO prio=90 cpu=1`.

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
