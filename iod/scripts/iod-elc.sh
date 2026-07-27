#!/bin/bash
# iod-elc boot: libelcethercat / elc_ethercat master path.
#
# Servo commissioning uses elc_sdo ordered setup recipes (not legacy ethercat
# download / sdo.sh zero-mapping writes). elc_sdo and iod-elc open
# /dev/elc_ethercat0 in turn — do not hold master 0 with ethercat tools while
# either is running.

set -euo pipefail

rm -f /tmp/iod.lock
BASEDIR='/opt/latproc'
ulimit -c unlimited

echo "performance" | /usr/bin/tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor >/dev/null

log=/dev/null
[ "${1:-}" = "-v" ] && log=/tmp/iod.log
[ -z "${IOD:-}" ] && IOD="${BASEDIR}/iod/build-elc/iod-elc"
[ -r /tmp/iod ] && : > /tmp/iod

# Userland: make install-lib → /usr/local; tools from source tree or PATH.
# Legacy /opt/elc remains a fallback. ldconfig/RUNPATH often enough for the lib.
export LD_LIBRARY_PATH="/usr/local/lib:/opt/elc/lib:${BASEDIR}/code/plugins:${LD_LIBRARY_PATH:-}"
export PATH="/usr/local/bin:/opt/etherlab-cyclic-kmod/tools:/opt/elc/bin:${PATH}"

ELC_DEVICE="${ELC_DEVICE:-/dev/elc_ethercat0}"
ELC_TOOLS_DIR="${ELC_TOOLS_DIR:-/opt/etherlab-cyclic-kmod/tools}"
# Prefer PATH (/usr/local/bin after install), then source-tree tools.
if [ -z "${ELC_BUS:-}" ]; then
  ELC_BUS="$(command -v elc_bus 2>/dev/null || true)"
fi
if [ -z "${ELC_BUS}" ] && [ -x "${ELC_TOOLS_DIR}/elc_bus" ]; then
  ELC_BUS="${ELC_TOOLS_DIR}/elc_bus"
fi
if [ -z "${ELC_SDO:-}" ]; then
  ELC_SDO="$(command -v elc_sdo 2>/dev/null || true)"
fi
if [ -z "${ELC_SDO}" ] && [ -x "${ELC_TOOLS_DIR}/elc_sdo" ]; then
  ELC_SDO="${ELC_TOOLS_DIR}/elc_sdo"
fi

if [ -z "${RECIPE_IN:-}" ]; then
  if [ -r "${BASEDIR}/code/config/recipes/ed3l_velocity_pdo.recipe.in" ]; then
    RECIPE_IN="${BASEDIR}/code/config/recipes/ed3l_velocity_pdo.recipe.in"
  else
    RECIPE_IN="${BASEDIR}/iod/recipes/ed3l_velocity_pdo.recipe.in"
  fi
fi

if [ -z "${ELC_BUS}" ] || [ ! -x "${ELC_BUS}" ]; then
  echo "ERROR: elc_bus not found (cd /opt/etherlab-cyclic-kmod && make tools; install to /usr/local/bin or set ELC_BUS)" >&2
  exit 1
fi
if [ -z "${ELC_SDO}" ] || [ ! -x "${ELC_SDO}" ]; then
  echo "ERROR: elc_sdo not found (cd /opt/etherlab-cyclic-kmod && make tools; install to /usr/local/bin or set ELC_SDO)" >&2
  exit 1
fi
if [ ! -r "${RECIPE_IN}" ]; then
  echo "ERROR: missing ED3L recipe template: ${RECIPE_IN}" >&2
  exit 1
fi
# Kernel cyclic task must be SCHED_FIFO or domain WC flaps incomplete under
# load (fault 0x20 DOMAIN_INCOMPLETE → bus_healthy=0 → outputs never arm).
# Prefer DKMS module via modprobe (see /etc/modprobe.d/elc_ethercat.conf).
# Optional tree .ko: ELC_KO=/path/to/elc_ethercat.ko (insmod fallback only).
ELC_CYCLE_CPU="${ELC_CYCLE_CPU:-1}"
ELC_CYCLE_FIFO_PRIORITY="${ELC_CYCLE_FIFO_PRIORITY:-90}"
ELC_KO="${ELC_KO:-/opt/etherlab-cyclic-kmod/kernel/elc_ethercat.ko}"

load_elc_module() {
  # Prefer modprobe (DKMS /lib/modules + modprobe.d RT options). Fall back to
  # insmod of a tree/out-of-tree .ko when DKMS is not installed.
  echo "Loading elc_ethercat (cycle_cpu=${ELC_CYCLE_CPU} cycle_fifo_priority=${ELC_CYCLE_FIFO_PRIORITY})"
  if modprobe elc_ethercat "cycle_cpu=${ELC_CYCLE_CPU}" \
      "cycle_fifo_priority=${ELC_CYCLE_FIFO_PRIORITY}" 2>/dev/null; then
    return 0
  fi
  echo "WARNING: modprobe elc_ethercat failed; trying insmod ${ELC_KO}" >&2
  if [ -f "${ELC_KO}" ]; then
    if insmod "${ELC_KO}" "cycle_cpu=${ELC_CYCLE_CPU}" \
        "cycle_fifo_priority=${ELC_CYCLE_FIFO_PRIORITY}"; then
      return 0
    fi
    echo "WARNING: insmod ${ELC_KO} failed" >&2
  fi
  return 1
}

ensure_elc_module() {
  if [ -c "${ELC_DEVICE}" ] && [ -d /sys/module/elc_ethercat ]; then
    return 0
  fi
  load_elc_module || true
  sleep 0.2
}

# If the module was loaded without RT params (cycle_fifo_priority=0), promote
# any live elc_cycle kthread so this boot can still arm outputs.
promote_elc_cycle_rt() {
  local tid class prio
  while read -r tid class prio; do
    [ -n "${tid}" ] || continue
    if [ "${class}" != "FF" ] || [ "${prio:-0}" -lt "${ELC_CYCLE_FIFO_PRIORITY}" ]; then
      echo "Promoting elc_cycle tid=${tid} to SCHED_FIFO ${ELC_CYCLE_FIFO_PRIORITY} CPU ${ELC_CYCLE_CPU}"
      chrt -f -p "${ELC_CYCLE_FIFO_PRIORITY}" "${tid}" 2>/dev/null || true
      taskset -cp "${ELC_CYCLE_CPU}" "${tid}" 2>/dev/null || true
    fi
  done < <(ps -eLo tid,class,rtprio,comm 2>/dev/null | awk '$4=="elc_cycle"{print $1,$2,$3}')
}

ensure_elc_module
if [ ! -c "${ELC_DEVICE}" ]; then
  echo "ERROR: kernel EtherCAT device missing: ${ELC_DEVICE}" >&2
  echo "Load (DKMS preferred):" >&2
  echo "  modprobe elc_ethercat cycle_cpu=${ELC_CYCLE_CPU} cycle_fifo_priority=${ELC_CYCLE_FIFO_PRIORITY}" >&2
  echo "  # or: insmod ${ELC_KO} cycle_cpu=… cycle_fifo_priority=…" >&2
  echo "  # RT defaults: /etc/modprobe.d/elc_ethercat.conf" >&2
  exit 1
fi
# Params are immutable after load. Restarting iod alone does not reload the
# module — if it was loaded soft-RT, try rmmod+modprobe while master is free
# (before iod/elc_sdo claim it). elc_cycle only exists after cycle activate;
# iod-elc also promotes post-activate as a safety net.
if [ -r /sys/module/elc_ethercat/parameters/cycle_fifo_priority ]; then
  cur_prio=$(cat /sys/module/elc_ethercat/parameters/cycle_fifo_priority 2>/dev/null || echo 0)
  if [ "${cur_prio}" = "0" ]; then
    echo "WARNING: elc_ethercat loaded with cycle_fifo_priority=0 (normal scheduling)." >&2
    echo "  Attempting reload with cycle_cpu=${ELC_CYCLE_CPU} cycle_fifo_priority=${ELC_CYCLE_FIFO_PRIORITY}..." >&2
    if rmmod elc_ethercat 2>/dev/null; then
      if load_elc_module; then
        echo "  elc_ethercat reloaded with RT cyclic task params."
      else
        echo "ERROR: failed to reload elc_ethercat after rmmod" >&2
        exit 1
      fi
    else
      echo "  rmmod failed (device in use). iod-elc will promote elc_cycle after activate." >&2
      promote_elc_cycle_rt
    fi
  else
    echo "elc_ethercat cycle_fifo_priority=${cur_prio} cycle_cpu=$(cat /sys/module/elc_ethercat/parameters/cycle_cpu 2>/dev/null || echo '?')"
  fi
fi
if [ ! -x "${IOD}" ]; then
  echo "ERROR: iod-elc binary not found: ${IOD}" >&2
  exit 1
fi

wait_for_link() {
  local link=""
  while true; do
    link="$("${ELC_BUS}" "${ELC_DEVICE}" 2>/dev/null | awk -F': ' '/^link:/{print $2; exit}')"
    if [ "${link}" = "up" ]; then
      echo "ELC link UP on ${ELC_DEVICE}"
      break
    fi
    echo "No Link (${ELC_DEVICE}: ${link:-unknown})"
    sleep 10
  done
}

discover_servo_positions() {
  mapfile -t SERVO_POSITIONS < <(
    "${ELC_BUS}" "${ELC_DEVICE}" 2>/dev/null |
      awk '/Summa ED3L ServoDrive/ {print $1}'
  )
}

apply_ed3l_velocity_pdo() {
  local pos recipe_tmp seq_base=0
  local -a positions=("$@")

  if [ "${#positions[@]}" -eq 0 ]; then
    echo "WARNING: no Summa ED3L ServoDrive slaves detected; skipping servo SDO setup" >&2
    return 0
  fi

  echo "Detected ${#positions[@]} Summa ED3L ServoDrive slave(s) at: ${positions[*]}"
  echo "Applying kernel ordered setup SDO recipes via ${ELC_SDO} (device ${ELC_DEVICE})"

  recipe_tmp="$(mktemp /tmp/ed3l_velocity_pdo.XXXXXX.recipe)"
  {
    echo "# Generated multi-slave ED3L velocity PDO setup"
    for pos in "${positions[@]}"; do
      awk -v pos="${pos}" -v base="${seq_base}" '
        /^#/ || NF == 0 { print; next }
        {
          seq = $1 + base
          $1 = seq
          $2 = pos
          print
        }
      ' "${RECIPE_IN}"
      seq_base=$((seq_base + 100))
    done
  } > "${recipe_tmp}"

  if ! "${ELC_SDO}" recipe "${recipe_tmp}" "${ELC_DEVICE}"; then
    echo "ERROR: elc_sdo recipe failed (see above). Leaving ${recipe_tmp} for debug." >&2
    return 1
  fi
  rm -f "${recipe_tmp}"
  echo "ED3L velocity PDO setup applied successfully"
}

wait_for_link
discover_servo_positions
apply_ed3l_velocity_pdo "${SERVO_POSITIONS[@]:-}"

MANAGEMENT_INTERFACE="${MANAGEMENT_INTERFACE:-enp2s0}"
MANAGEMENT_IRQ_CPU="${MANAGEMENT_IRQ_CPU:-2}"
if [ -r /proc/interrupts ]; then
  mapfile -t MANAGEMENT_IRQS < <(
    awk -F: -v iface="${MANAGEMENT_INTERFACE}" \
      '$0 ~ iface {gsub(/[[:space:]]/, "", $1); print $1}' \
      /proc/interrupts
  )
  for irq in "${MANAGEMENT_IRQS[@]:-}"; do
    affinity_file="/proc/irq/${irq}/smp_affinity_list"
    if [ -w "${affinity_file}" ]; then
      echo "${MANAGEMENT_IRQ_CPU}" > "${affinity_file}" || true
      echo "Set ${MANAGEMENT_INTERFACE} IRQ ${irq} affinity to CPU ${MANAGEMENT_IRQ_CPU}"
    fi
  done
fi

touch /tmp/iod.lock
[ -r core ] && mv core "core.$(date +%y%m%d.%H%M%S)"
ls -1t core.* 2>/dev/null | tail -n +5 | xargs -r -d '\n' rm -- || true

# Do not use process-substitution logger with exec: it hides aborts and can
# tear down condition variables while worker threads are still running.
echo "Starting ${IOD} (kernel EtherCAT transport)"
if [ "${log}" = "/dev/null" ]; then
  exec "${IOD}" \
    --name 1G2C \
    -i "${BASEDIR}/code/config/persist.dat" \
    -m "${BASEDIR}/code/config/modbus_mappings.txt" \
    -c "${BASEDIR}/etc/iod.conf" \
    --config "${BASEDIR}/code/config/cpu_affinity.conf" \
    --stats \
    "${BASEDIR}/code/lib" \
    "${BASEDIR}/code/config" \
    "${BASEDIR}/code/machine"
else
  exec "${IOD}" \
    --name 1G2C \
    -i "${BASEDIR}/code/config/persist.dat" \
    -m "${BASEDIR}/code/config/modbus_mappings.txt" \
    -c "${BASEDIR}/etc/iod.conf" \
    --config "${BASEDIR}/code/config/cpu_affinity.conf" \
    --stats \
    "${BASEDIR}/code/lib" \
    "${BASEDIR}/code/config" \
    "${BASEDIR}/code/machine" \
    >>"${log}" 2>&1
fi
