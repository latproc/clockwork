#!/bin/bash
# iod (iod_sdo) boot script for 2GRAB / legacy ethercat master path.
#
# Optional stderr/stdout capture for PROCSNAP / MEMSNAPSHOT / other DEBUG noise.
# Default is quiet-ish (syslog via logger) so the disk is not filled.
#
# Enable file log for one boot (or until TTL expires):
#   touch /tmp/iod-verbose                 # or: echo 3600 > /tmp/iod-verbose
#   svc -t /etc/service/iod
# Then (once iod is up) enable snapshots over the command channel:
#   printf 'DEBUG DEBUG_PROCSNAP on;\nDEBUG DEBUG_MEMSNAPSHOT on;\n' | /opt/latproc/iod/iosh
# Watch:
#   tail -f /tmp/iod.log | grep -E 'PROCSNAP|MEMSNAPSHOT'
# Disable:
#   rm -f /tmp/iod-verbose
#   printf 'DEBUG DEBUG_PROCSNAP off;\nDEBUG DEBUG_MEMSNAPSHOT off;\n' | /opt/latproc/iod/iosh
#   svc -t /etc/service/iod                # optional: stop file logging
#
# Also: argv -v, or env IOD_LOG=/path/to/file (still size-capped below).
# Auto-off: if the switch file mtime is older than its TTL (default 3600s),
# the file is removed and file logging stays off.

rm -f /tmp/iod.lock
BASEDIR='/opt/latproc'
ulimit -c unlimited

echo "performance" | /usr/bin/tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor >/dev/null

# Keep the ordinary network receive queue off CPU 1, which is reserved for
# the EtherCAT timer and cyclic threads. Resolve the IRQ dynamically because
# Linux can assign a different IRQ number after a reboot.
ENP1S0_IRQ=$(
  awk -F: '/enp1s0-TxRx-0/ {
    gsub(/[[:space:]]/, "", $1)
    print $1
    exit
  }' /proc/interrupts
)

if [ -n "${ENP1S0_IRQ}" ] &&
   [ -w "/proc/irq/${ENP1S0_IRQ}/smp_affinity_list" ]; then
  echo 2 > "/proc/irq/${ENP1S0_IRQ}/smp_affinity_list"
  ACTUAL_AFFINITY=$(cat "/proc/irq/${ENP1S0_IRQ}/smp_affinity_list")
  if [ "${ACTUAL_AFFINITY}" = "2" ]; then
    echo "Moved enp1s0-TxRx-0 IRQ ${ENP1S0_IRQ} to CPU 2"
  else
    echo "WARNING: enp1s0-TxRx-0 IRQ affinity is ${ACTUAL_AFFINITY}, expected CPU 2" >&2
  fi
else
  echo "WARNING: enp1s0-TxRx-0 IRQ was not found or cannot be configured" >&2
fi

IOD_VERBOSE_SWITCH="${IOD_VERBOSE_SWITCH:-/tmp/iod-verbose}"
IOD_LOG_FILE="${IOD_LOG_FILE:-/tmp/iod.log}"
IOD_LOG_MAX_BYTES="${IOD_LOG_MAX_BYTES:-52428800}"   # 50 MiB hard cap per boot
IOD_LOG_TTL_SEC="${IOD_LOG_TTL_SEC:-3600}"            # default 1 hour if switch has no number

log=/dev/null
if [ "${1:-}" = "-v" ]; then
  log="${IOD_LOG_FILE}"
elif [ -n "${IOD_LOG:-}" ]; then
  log="${IOD_LOG}"
elif [ -e "${IOD_VERBOSE_SWITCH}" ]; then
  ttl="${IOD_LOG_TTL_SEC}"
  if [ -f "${IOD_VERBOSE_SWITCH}" ] && [ -s "${IOD_VERBOSE_SWITCH}" ]; then
    # Optional content: TTL seconds (integer). Empty file → IOD_LOG_TTL_SEC.
    read -r maybe_ttl <"${IOD_VERBOSE_SWITCH}" || true
    if [[ "${maybe_ttl:-}" =~ ^[0-9]+$ ]]; then
      ttl="${maybe_ttl}"
    fi
  fi
  # Age of switch file (seconds). Missing/unreadable → treat as expired.
  age=999999
  if [ -e "${IOD_VERBOSE_SWITCH}" ]; then
    age=$(($(date +%s) - $(stat -c %Y "${IOD_VERBOSE_SWITCH}" 2>/dev/null || echo 0)))
  fi
  if [ "${age}" -ge "${ttl}" ]; then
    echo "iod-verbose switch expired (age=${age}s ttl=${ttl}s); removing ${IOD_VERBOSE_SWITCH}" >&2
    rm -f "${IOD_VERBOSE_SWITCH}"
    log=/dev/null
  else
    log="${IOD_LOG_FILE}"
    echo "iod verbose log ON → ${log} (switch age=${age}s ttl=${ttl}s; rm switch + svc -t to stop)" >&2
  fi
fi
if [ "${log}" != "/dev/null" ]; then
  # Cap size: rotate oversized log so one verbose session cannot fill the disk.
  if [ -f "${log}" ]; then
    sz=$(stat -c %s "${log}" 2>/dev/null || echo 0)
    if [ "${sz}" -ge "${IOD_LOG_MAX_BYTES}" ]; then
      mv -f "${log}" "${log}.1" 2>/dev/null || rm -f "${log}"
      echo "rotated oversized ${log} (>= ${IOD_LOG_MAX_BYTES} bytes)" >&2
    fi
  fi
  : >>"${log}"
fi

[ -z "${IOD:-}" ] && IOD="${BASEDIR}/iod/iod_sdo"
[ -r /tmp/iod ] && : > /tmp/iod

ETHERCAT=$(command -v ethercat || true)
export LD_LIBRARY_PATH="${BASEDIR}/code/plugins:${LD_LIBRARY_PATH:-}"

# save the core file
[ -r core ] && mv core "core.$(date +%y%m%d.%H%M%S)"
ls -1t core.* 2>/dev/null | tail -n +5 | xargs -r -d '\n' rm -- || true

if [ -z "${ETHERCAT}" ] || [ ! -x "${ETHERCAT}" ]; then
  echo "ERROR: ethercat tool not found in PATH" >&2
  exit 1
fi

while true; do
  LINK=$("${ETHERCAT}" master | grep Link || true)
  if [[ ${LINK} =~ "UP" ]]; then
    break
  else
    echo "No Link"
  fi
  sleep 10
done

# cwd for service is typically /opt/latproc/code (see service layout)
if [ -x ./config/scripts/vd3e_remap_pdo_error_voltage.sh ]; then
  bash ./config/scripts/vd3e_remap_pdo_error_voltage.sh -p 23
elif [ -x "${BASEDIR}/code/config/scripts/vd3e_remap_pdo_error_voltage.sh" ]; then
  bash "${BASEDIR}/code/config/scripts/vd3e_remap_pdo_error_voltage.sh" -p 23
fi

touch /tmp/iod.lock

IOD_ARGS=(
  --name 2GRAB
  -i "${BASEDIR}/code/config/persist.dat"
  -m "${BASEDIR}/code/config/modbus_mappings.txt"
  -c "${BASEDIR}/etc/iod.conf"
  --config "${BASEDIR}/code/config/cpu_affinity.conf"
  --stats
  "${BASEDIR}/code/lib"
  "${BASEDIR}/code/shared"
  "${BASEDIR}/code/config"
  "${BASEDIR}/code/machine"
  "${BASEDIR}/code/warehouse"
)

echo "Starting ${IOD} log=${log}"
if [ "${log}" = "/dev/null" ]; then
  # Default: operational messages to syslog (info/warn). PROCSNAP off by default
  # so this path stays manageable; enable file log for snapshot work.
  exec nice -n-1 "${IOD}" "${IOD_ARGS[@]}" \
    > >(logger -p user.info) 2> >(logger -p user.warn)
else
  # File capture for DEBUG_PROCSNAP / DEBUG_MEMSNAPSHOT (written to stderr).
  # Avoid process-substitution logger here so abort output is not hidden.
  # Line-buffer stdout/stderr: default full buffering to a file makes
  # /tmp/iod.log grow in ~4 KiB bursts and often end mid-line until flush
  # (looks like "logging stopped"). stdbuf is from coreutils.
  if command -v stdbuf >/dev/null 2>&1; then
    exec nice -n-1 stdbuf -oL -eL "${IOD}" "${IOD_ARGS[@]}" >>"${log}" 2>&1
  else
    exec nice -n-1 "${IOD}" "${IOD_ARGS[@]}" >>"${log}" 2>&1
  fi
fi
