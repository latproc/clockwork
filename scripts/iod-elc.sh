#!/bin/bash
# Generic iod-elc starter (Clockwork product helper).
#
# Loads elc_ethercat if needed, then execs iod-elc. Site LPC paths, process
# name, persist file, and service wiring are configured by environment or
# trailing arguments — not hardcoded plant trees.
#
# Usage:
#   scripts/iod-elc.sh [options] [--] [cw_source_dir ...]
#   LATPROC=/opt/latproc scripts/iod-elc.sh --name MYCELL /path/to/cw/lib /path/to/cw/config
#
# Common env (all optional unless noted):
#   LATPROC              install root (default: parent of scripts/)
#   IOD                  path to iod-elc binary
#   IOD_NAME             --name for iod (default: iod)
#   IOD_CONF             -c config (default: $LATPROC/etc/iod.conf if present)
#   IOD_PERSIST          -i persist.dat path
#   IOD_MODBUS_MAP       -m modbus mappings path
#   IOD_CPU_AFFINITY     --config cpu affinity conf
#   IOD_STATS=1          pass --stats
#   IOD_EXTRA_ARGS       extra argv (word-split; quote carefully)
#   ELC_DEVICE           /dev/elc_ethercat0
#   ELC_KO               fallback .ko for insmod
#   ELC_CYCLE_CPU        default 1
#   ELC_CYCLE_FIFO_PRIORITY  default 90
#   ELC_TOPOLOGY_CONFIG  topology conf (default: $LATPROC/etc/elc_topology.conf)
#   LD_LIBRARY_PATH extras: IOD_PLUGIN_DIR (plugins .so search)
#   WAIT_ELC_LINK        auto|0|ethercat|elc_bus (default auto)
#   IOD_STREAM_FILTER=1  enable named-fifo log filter (MEMSNAPSHOT / verbose)
#   -v / IOD_LOG         force verbose file for this boot (with filter)
#
# See TRANSPORT.md for module RT params and rates.
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
LATPROC="${LATPROC:-$(cd "${SCRIPT_DIR}/.." && pwd)}"

usage() {
  sed -n '2,40p' "$0" | sed 's/^# \{0,1\}//'
  exit 0
}

FORCE_VERBOSE=0
PASS_ARGS=()
while [ $# -gt 0 ]; do
  case "$1" in
    -h|--help) usage ;;
    -v) FORCE_VERBOSE=1; shift ;;
    --) shift; PASS_ARGS+=("$@"); break ;;
    --name)
      IOD_NAME="${2:-}"; shift 2 || { echo "missing value for --name" >&2; exit 1; }
      ;;
    --name=*)
      IOD_NAME="${1#*=}"; shift
      ;;
    -*)
      # Unknown option: pass through to iod after our env setup
      PASS_ARGS+=("$1"); shift
      ;;
    *)
      PASS_ARGS+=("$1"); shift
      ;;
  esac
done

rm -f /tmp/iod.lock
ulimit -c unlimited 2>/dev/null || true

# Optional: raise performance governor (ignore if sysfs missing / no rights)
if [ -w /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor ] 2>/dev/null; then
  echo performance | tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor >/dev/null 2>&1 || true
fi

IOD_VERBOSE_SWITCH="${IOD_VERBOSE_SWITCH:-/tmp/iod-verbose}"
IOD_LOG_FILE="${IOD_LOG_FILE:-/tmp/iod.log}"
IOD_LOG_MAX_BYTES="${IOD_LOG_MAX_BYTES:-52428800}"
IOD_LOG_TTL_SEC="${IOD_LOG_TTL_SEC:-3600}"
IOD_MEMSNAPSHOT_LOG="${IOD_MEMSNAPSHOT_LOG:-${LATPROC}/sampling/iod-memory/memsnapshot.log}"
IOD_STREAM_FIFO="${IOD_STREAM_FIFO:-/tmp/iod.stream.fifo}"
IOD_MEMSNAPSHOT_MAX_BYTES="${IOD_MEMSNAPSHOT_MAX_BYTES:-10485760}"
IOD_VERBOSE_POLL_SEC="${IOD_VERBOSE_POLL_SEC:-2}"
IOD_STREAM_FILTER="${IOD_STREAM_FILTER:-0}"

if [ "${FORCE_VERBOSE}" -eq 1 ]; then
  : "${IOD_LOG:=${IOD_LOG_FILE}}"
fi
if [ -n "${IOD_LOG:-}" ]; then
  IOD_LOG_FILE="${IOD_LOG}"
  IOD_STREAM_FILTER=1
  if [ ! -e "${IOD_VERBOSE_SWITCH}" ]; then
    echo "${IOD_LOG_TTL_SEC}" >"${IOD_VERBOSE_SWITCH}"
  fi
fi

start_iod_stream_filter() {
  mkdir -p "$(dirname "${IOD_MEMSNAPSHOT_LOG}")"
  if [ -f "${IOD_MEMSNAPSHOT_LOG}" ]; then
    local sz
    sz=$(stat -c %s "${IOD_MEMSNAPSHOT_LOG}" 2>/dev/null || echo 0)
    if [ "${sz}" -ge "${IOD_MEMSNAPSHOT_MAX_BYTES}" ]; then
      mv -f "${IOD_MEMSNAPSHOT_LOG}" "${IOD_MEMSNAPSHOT_LOG}.1" 2>/dev/null || rm -f "${IOD_MEMSNAPSHOT_LOG}"
    fi
  fi
  if [ -f "${IOD_LOG_FILE}" ]; then
    local sz
    sz=$(stat -c %s "${IOD_LOG_FILE}" 2>/dev/null || echo 0)
    if [ "${sz}" -ge "${IOD_LOG_MAX_BYTES}" ]; then
      mv -f "${IOD_LOG_FILE}" "${IOD_LOG_FILE}.1" 2>/dev/null || rm -f "${IOD_LOG_FILE}"
    fi
  fi
  rm -f "${IOD_STREAM_FIFO}"
  mkfifo -m 600 "${IOD_STREAM_FIFO}"
  exec 3>&2
  setsid bash -c '
    mem_log="$1"; verbose_log="$2"; switch="$3"; default_ttl="$4"
    max_bytes="$5"; poll_sec="$6"
    verbose_active=0; last_check=0; last_rotate_check=0
    check_verbose() {
      if [ ! -e "${switch}" ]; then verbose_active=0; return; fi
      local ttl age mtime now
      ttl="${default_ttl}"
      if [ -f "${switch}" ] && [ -s "${switch}" ]; then
        read -r maybe_ttl <"${switch}" || true
        if [[ "${maybe_ttl:-}" =~ ^[0-9]+$ ]]; then ttl="${maybe_ttl}"; fi
      fi
      mtime=$(stat -c %Y "${switch}" 2>/dev/null || echo 0)
      now=$(date +%s); age=$((now - mtime))
      if [ "${age}" -ge "${ttl}" ]; then
        rm -f "${switch}"; verbose_active=0; return
      fi
      verbose_active=1
    }
    rotate_if_needed() {
      local sz; [ -f "${verbose_log}" ] || return 0
      sz=$(stat -c %s "${verbose_log}" 2>/dev/null || echo 0)
      if [ "${sz}" -ge "${max_bytes}" ]; then
        mv -f "${verbose_log}" "${verbose_log}.1" 2>/dev/null || rm -f "${verbose_log}"
        : >>"${verbose_log}"
      fi
    }
    check_verbose
    while IFS= read -r line || [ -n "${line}" ]; do
      printf "%s\n" "${line}" >&3
      case "${line}" in MEMSNAPSHOT*) printf "%s\n" "${line}" >>"${mem_log}" ;; esac
      now=$(date +%s)
      if [ $((now - last_check)) -ge "${poll_sec}" ]; then last_check=${now}; check_verbose; fi
      if [ "${verbose_active}" -eq 1 ]; then
        if [ $((now - last_rotate_check)) -ge 30 ]; then last_rotate_check=${now}; rotate_if_needed; fi
        printf "%s\n" "${line}" >>"${verbose_log}"
      fi
    done
  ' bash \
    "${IOD_MEMSNAPSHOT_LOG}" \
    "${IOD_LOG_FILE}" \
    "${IOD_VERBOSE_SWITCH}" \
    "${IOD_LOG_TTL_SEC}" \
    "${IOD_LOG_MAX_BYTES}" \
    "${IOD_VERBOSE_POLL_SEC}" \
    3>&3 <"${IOD_STREAM_FIFO}" &
  echo "iod stream filter: MEMSNAPSHOT→${IOD_MEMSNAPSHOT_LOG}; verbose ${IOD_VERBOSE_SWITCH}→${IOD_LOG_FILE}"
}

if [ -z "${IOD:-}" ]; then
  if [ -x "${LATPROC}/iod/iod-elc" ]; then
    IOD="${LATPROC}/iod/iod-elc"
  elif [ -x "${LATPROC}/iod/build-elc/iod-elc" ]; then
    IOD="${LATPROC}/iod/build-elc/iod-elc"
  else
    IOD="$(command -v iod-elc 2>/dev/null || true)"
  fi
fi

export LD_LIBRARY_PATH="/usr/local/lib:/opt/elc/lib${IOD_PLUGIN_DIR:+:${IOD_PLUGIN_DIR}}${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
export PATH="/usr/local/bin:/opt/etherlab-cyclic-kmod/tools:/opt/elc/bin:${PATH}"
if [ -n "${ELC_TOPOLOGY_CONFIG:-}" ]; then
  export ELC_TOPOLOGY_CONFIG
elif [ -f "${LATPROC}/etc/elc_topology.conf" ]; then
  export ELC_TOPOLOGY_CONFIG="${LATPROC}/etc/elc_topology.conf"
fi

ELC_DEVICE="${ELC_DEVICE:-/dev/elc_ethercat0}"
ELC_TOOLS_DIR="${ELC_TOOLS_DIR:-/opt/etherlab-cyclic-kmod/tools}"
ELC_CYCLE_CPU="${ELC_CYCLE_CPU:-1}"
ELC_CYCLE_FIFO_PRIORITY="${ELC_CYCLE_FIFO_PRIORITY:-90}"
ELC_KO="${ELC_KO:-/opt/etherlab-cyclic-kmod/kernel/elc_ethercat.ko}"

if [ -z "${ELC_BUS:-}" ]; then
  ELC_BUS="$(command -v elc_bus 2>/dev/null || true)"
fi
if [ -z "${ELC_BUS}" ] && [ -x "${ELC_TOOLS_DIR}/elc_bus" ]; then
  ELC_BUS="${ELC_TOOLS_DIR}/elc_bus"
fi

load_elc_module() {
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
  echo "  modprobe elc_ethercat cycle_cpu=${ELC_CYCLE_CPU} cycle_fifo_priority=${ELC_CYCLE_FIFO_PRIORITY}" >&2
  echo "  # or: insmod ${ELC_KO} …" >&2
  echo "  See TRANSPORT.md" >&2
  exit 1
fi

if [ -r /sys/module/elc_ethercat/parameters/cycle_fifo_priority ]; then
  cur_prio=$(cat /sys/module/elc_ethercat/parameters/cycle_fifo_priority 2>/dev/null || echo 0)
  if [ "${cur_prio}" = "0" ]; then
    echo "WARNING: elc_ethercat loaded with cycle_fifo_priority=0." >&2
    if rmmod elc_ethercat 2>/dev/null; then
      load_elc_module || { echo "ERROR: reload elc_ethercat failed" >&2; exit 1; }
    else
      echo "  rmmod failed (device in use); iod-elc may promote elc_cycle after activate." >&2
      promote_elc_cycle_rt
    fi
  else
    echo "elc_ethercat cycle_fifo_priority=${cur_prio} cycle_cpu=$(cat /sys/module/elc_ethercat/parameters/cycle_cpu 2>/dev/null || echo '?')"
  fi
fi

if [ -z "${IOD:-}" ] || [ ! -x "${IOD}" ]; then
  echo "ERROR: iod-elc binary not found (set IOD= or install under ${LATPROC}/iod/)" >&2
  exit 1
fi

wait_for_ec_link() {
  local mode="${WAIT_ELC_LINK:-auto}"
  local link="" ethercat_bin=""
  case "${mode}" in
    0|no|off|skip|false) return 0 ;;
  esac

  ethercat_bin="$(command -v ethercat 2>/dev/null || true)"
  if [ -z "${ethercat_bin}" ] && [ -x /usr/bin/ethercat ]; then
    ethercat_bin=/usr/bin/ethercat
  fi

  if [ "${mode}" = "elc_bus" ]; then
    if [ -z "${ELC_BUS:-}" ] || [ ! -x "${ELC_BUS}" ]; then
      echo "ERROR: WAIT_ELC_LINK=elc_bus but elc_bus not found" >&2
      exit 1
    fi
    while true; do
      link="$("${ELC_BUS}" "${ELC_DEVICE}" 2>/dev/null | awk -F': ' '/^link:/{print $2; exit}')"
      if [ "${link}" = "up" ]; then
        echo "ELC link UP (${ELC_DEVICE})"
        return 0
      fi
      echo "No Link (${ELC_DEVICE}: ${link:-unknown})"
      sleep 10
    done
  fi

  if [ -z "${ethercat_bin}" ]; then
    if [ "${mode}" = "ethercat" ]; then
      echo "ERROR: WAIT_ELC_LINK=ethercat but ethercat tool not in PATH" >&2
      exit 1
    fi
    echo "WARNING: ethercat tool not found; skipping link wait"
    return 0
  fi

  echo "Waiting for EtherCAT main link via ${ethercat_bin} master"
  while true; do
    link="$("${ethercat_bin}" master 2>/dev/null | awk '
      /Main:/{m=1}
      m && /Link:/{print toupper($2); exit}
    ')"
    if [ "${link}" = "UP" ]; then
      echo "EtherCAT main link UP"
      return 0
    fi
    echo "No EtherCAT link (${link:-unknown}); retry in 10s"
    sleep 10
  done
}

wait_for_ec_link

# Optional management NIC IRQ pin (site may set MANAGEMENT_INTERFACE)
if [ -n "${MANAGEMENT_INTERFACE:-}" ] && [ -r /proc/interrupts ]; then
  MANAGEMENT_IRQ_CPU="${MANAGEMENT_IRQ_CPU:-2}"
  mapfile -t MANAGEMENT_IRQS < <(
    awk -F: -v iface="${MANAGEMENT_INTERFACE}" \
      '$0 ~ iface {gsub(/[[:space:]]/, "", $1); print $1}' \
      /proc/interrupts
  )
  for irq in "${MANAGEMENT_IRQS[@]:-}"; do
    affinity_file="/proc/irq/${irq}/smp_affinity_list"
    if [ -w "${affinity_file}" ]; then
      echo "${MANAGEMENT_IRQ_CPU}" > "${affinity_file}" || true
    fi
  done
fi

touch /tmp/iod.lock
[ -r core ] && mv core "core.$(date +%y%m%d.%H%M%S)" 2>/dev/null || true

# Build iod argv
CMD=("${IOD}")
IOD_NAME="${IOD_NAME:-iod}"
CMD+=(--name "${IOD_NAME}")

if [ -n "${IOD_PERSIST:-}" ]; then
  CMD+=(-i "${IOD_PERSIST}")
fi
if [ -n "${IOD_MODBUS_MAP:-}" ]; then
  CMD+=(-m "${IOD_MODBUS_MAP}")
fi
if [ -n "${IOD_CONF:-}" ]; then
  CMD+=(-c "${IOD_CONF}")
elif [ -f "${LATPROC}/etc/iod.conf" ]; then
  CMD+=(-c "${LATPROC}/etc/iod.conf")
fi
if [ -n "${IOD_CPU_AFFINITY:-}" ]; then
  CMD+=(--config "${IOD_CPU_AFFINITY}")
fi
if [ "${IOD_STATS:-0}" = "1" ]; then
  CMD+=(--stats)
fi

# shellcheck disable=SC2206
if [ -n "${IOD_EXTRA_ARGS:-}" ]; then
  # intentional word-split for extra flags
  EXTRA=( ${IOD_EXTRA_ARGS} )
  CMD+=("${EXTRA[@]}")
fi

CMD+=("${PASS_ARGS[@]}")

if [ "${#PASS_ARGS[@]}" -eq 0 ] && [ -z "${IOD_EXTRA_ARGS:-}" ]; then
  echo "NOTE: no Clockwork source directories on argv." >&2
  echo "  Pass LPC/lib dirs as arguments, e.g.:" >&2
  echo "  $0 --name MYCELL /path/to/lib /path/to/config" >&2
  echo "  Config defaults: ${LATPROC}/etc/elc_topology.conf  ${LATPROC}/etc/iod.conf" >&2
  echo "  See ${LATPROC}/etc/README.md" >&2
fi

if [ -n "${ELC_TOPOLOGY_CONFIG:-}" ]; then
  echo "Topology: ${ELC_TOPOLOGY_CONFIG}"
fi
echo "Starting ${CMD[*]}"
if [ "${IOD_STREAM_FILTER}" = "1" ]; then
  start_iod_stream_filter
  exec "${CMD[@]}" >"${IOD_STREAM_FIFO}" 2>&1
else
  exec "${CMD[@]}"
fi
