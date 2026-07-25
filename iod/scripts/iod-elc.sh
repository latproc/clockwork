#!/bin/bash
# iod-elc boot: Phase 8 kernel transport (libelcethercat / elc_ethercat).
#
# SDO commissioning uses the kernel ordered setup path (elc_sdo recipe),
# not legacy ethercat download / sdo.sh zero-mapping writes (0x06040041).
# See /opt/etherlab-cyclic-kmod/docs/ed3l-pdo-configuration-test.md
#
# Exclusivity: elc_sdo and iod-elc each open /dev/elc_ethercat0 in turn.
# Do not run ethercat-based tools that hold master 0 while either is open.

set -euo pipefail

rm -f /tmp/iod.lock
BASEDIR='/opt/latproc'
ulimit -c unlimited

echo "performance" | /usr/bin/tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor >/dev/null

log=/dev/null
[ "${1:-}" = "-v" ] && log=/tmp/iod.log
[ -z "${IOD:-}" ] && IOD="${BASEDIR}/iod/build-elc/iod-elc"
[ -r /tmp/iod ] && : > /tmp/iod

export LD_LIBRARY_PATH="/opt/elc/lib:${BASEDIR}/code/plugins:${LD_LIBRARY_PATH:-}"
export PATH="/opt/elc/bin:/opt/etherlab-cyclic-kmod/tools:${PATH}"

ELC_DEVICE="${ELC_DEVICE:-/dev/elc_ethercat0}"
ELC_BUS="${ELC_BUS:-$(command -v elc_bus || true)}"
ELC_SDO="${ELC_SDO:-$(command -v elc_sdo || true)}"

if [ -z "${RECIPE_IN:-}" ]; then
  if [ -r "${BASEDIR}/code/config/recipes/ed3l_velocity_pdo.recipe.in" ]; then
    RECIPE_IN="${BASEDIR}/code/config/recipes/ed3l_velocity_pdo.recipe.in"
  else
    RECIPE_IN="${BASEDIR}/iod/recipes/ed3l_velocity_pdo.recipe.in"
  fi
fi

if [ -z "${ELC_BUS}" ] || [ ! -x "${ELC_BUS}" ]; then
  echo "ERROR: elc_bus not found (install libelcethercat tools or set ELC_BUS)" >&2
  exit 1
fi
if [ -z "${ELC_SDO}" ] || [ ! -x "${ELC_SDO}" ]; then
  echo "ERROR: elc_sdo not found (install libelcethercat tools or set ELC_SDO)" >&2
  exit 1
fi
if [ ! -r "${RECIPE_IN}" ]; then
  echo "ERROR: missing ED3L recipe template: ${RECIPE_IN}" >&2
  exit 1
fi
if [ ! -c "${ELC_DEVICE}" ]; then
  echo "ERROR: kernel EtherCAT device missing: ${ELC_DEVICE}" >&2
  echo "Load elc_ethercat and ensure master is free." >&2
  exit 1
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

echo "Starting ${IOD} (kernel transport Phase 8: discovery + SDO mailbox; no cyclic outputs yet)"
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
  > >(logger -p user.info) 2> >(logger -p user.warn)
