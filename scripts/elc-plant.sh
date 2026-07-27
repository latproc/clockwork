#!/bin/bash
# One-stop plant helper for elc EtherCAT (DKMS module + /usr/local userland).
#
# Usage:
#   elc-plant.sh status              # show module/lib/tools/iod (no changes)
#   elc-plant.sh verify              # same checks; exit 1 if anything critical fails
#   elc-plant.sh install-userland    # make tools+lib in tree, install to /usr/local
#   elc-plant.sh reload-module       # stop iod, modprobe elc with RT params, start iod
#   elc-plant.sh setup               # install-userland + ldconfig + reload-module + verify
#
# Source tree default: /opt/etherlab-cyclic-kmod
# Override: ELC_SRC=…  PREFIX=…  IOD_SVC=…

set -euo pipefail

ELC_SRC="${ELC_SRC:-/opt/etherlab-cyclic-kmod}"
PREFIX="${PREFIX:-/usr/local}"
IOD_SVC="${IOD_SVC:-/etc/service/iod}"
ELC_CYCLE_CPU="${ELC_CYCLE_CPU:-1}"
ELC_CYCLE_FIFO_PRIORITY="${ELC_CYCLE_FIFO_PRIORITY:-90}"
MODPROBE_CONF="${MODPROBE_CONF:-/etc/modprobe.d/elc_ethercat.conf}"

red() { printf '\033[31m%s\033[0m\n' "$*"; }
grn() { printf '\033[32m%s\033[0m\n' "$*"; }
ylw() { printf '\033[33m%s\033[0m\n' "$*"; }

need_root() {
  if [ "$(id -u)" -ne 0 ]; then
    red "ERROR: run as root (module load / install)"
    exit 1
  fi
}

iod_stop() {
  if [ -d "${IOD_SVC}" ]; then
    echo "Stopping iod (${IOD_SVC})..."
    svc -d "${IOD_SVC}" 2>/dev/null || true
  fi
  local i
  for i in 1 2 3 4 5 6 7 8 9 10 12 15; do
    if ! pidof iod-elc >/dev/null 2>&1 && ! pidof iod_main >/dev/null 2>&1; then
      echo "iod stopped"
      return 0
    fi
    pkill -x iod-elc 2>/dev/null || true
    pkill -x iod_main 2>/dev/null || true
    sleep 1
  done
  if pidof iod-elc >/dev/null 2>&1; then
    red "ERROR: iod still running; free /dev/elc_ethercat0 and retry"
    exit 1
  fi
}

iod_start() {
  if [ -d "${IOD_SVC}" ]; then
    echo "Starting iod (${IOD_SVC})..."
    svc -u "${IOD_SVC}" 2>/dev/null || true
    sleep 3
  fi
}

ensure_modprobe_conf() {
  if [ -f "${MODPROBE_CONF}" ]; then
    return 0
  fi
  ylw "Creating ${MODPROBE_CONF}"
  cat >"${MODPROBE_CONF}" <<EOF
# elc_ethercat cyclic task must be real-time or domain WC goes incomplete
# under load (ELC_IO_FAULT_DOMAIN_INCOMPLETE / bus_healthy=0 / outputs disarmed).
options elc_ethercat cycle_cpu=${ELC_CYCLE_CPU} cycle_fifo_priority=${ELC_CYCLE_FIFO_PRIORITY}
EOF
}

cmd_status() {
  local fail=0
  echo "=== elc plant status ($(date -Iseconds)) ==="
  echo
  echo "-- DKMS --"
  if command -v dkms >/dev/null 2>&1; then
    dkms status 2>/dev/null | grep -iE 'elc|ethercat' || ylw "(no elc/ethercat dkms lines)"
  else
    ylw "dkms not installed"
  fi
  echo
  echo "-- kernel module --"
  if modinfo elc_ethercat >/dev/null 2>&1; then
    modinfo elc_ethercat | grep -E 'filename|description|depends|vermagic' || true
  else
    red "elc_ethercat: modinfo failed (not installed for this kernel?)"
    fail=1
  fi
  if [ -d /sys/module/elc_ethercat ]; then
    local cpu prio
    cpu=$(cat /sys/module/elc_ethercat/parameters/cycle_cpu 2>/dev/null || echo '?')
    prio=$(cat /sys/module/elc_ethercat/parameters/cycle_fifo_priority 2>/dev/null || echo '?')
    echo "loaded: cycle_cpu=${cpu} cycle_fifo_priority=${prio}"
    if [ "${prio}" = "0" ] || [ "${prio}" = "?" ]; then
      ylw "  WARN: soft-RT or unknown priority (want ${ELC_CYCLE_FIFO_PRIORITY})"
      fail=1
    elif [ "${prio}" != "${ELC_CYCLE_FIFO_PRIORITY}" ]; then
      ylw "  WARN: priority ${prio} != configured ${ELC_CYCLE_FIFO_PRIORITY} (reload-module)"
    else
      grn "  RT params OK"
    fi
  else
    ylw "module not loaded"
  fi
  if [ -c /dev/elc_ethercat0 ]; then
    grn "device: /dev/elc_ethercat0 present"
  else
    ylw "device: /dev/elc_ethercat0 missing"
  fi
  if [ -f "${MODPROBE_CONF}" ]; then
    echo "modprobe.d: ${MODPROBE_CONF}"
    grep -v '^#' "${MODPROBE_CONF}" | grep -v '^$' || true
  else
    ylw "modprobe.d: missing ${MODPROBE_CONF}"
  fi
  echo
  echo "-- userland lib (${PREFIX}) --"
  if [ -e "${PREFIX}/lib/libelcethercat.so.0" ]; then
    ls -la "${PREFIX}/lib/libelcethercat.so"* 2>/dev/null | head -5
    if command -v pkg-config >/dev/null 2>&1; then
      PKG_CONFIG_PATH="${PREFIX}/lib/pkgconfig:${PKG_CONFIG_PATH:-}" \
        pkg-config --modversion elcethercat 2>/dev/null && grn "pkg-config elcethercat OK" \
        || ylw "pkg-config elcethercat not found (PKG_CONFIG_PATH?)"
    fi
    if ldconfig -p 2>/dev/null | grep -q libelcethercat; then
      grn "ldconfig sees libelcethercat"
    else
      ylw "ldconfig does not list libelcethercat (run: ldconfig)"
      fail=1
    fi
  else
    red "missing ${PREFIX}/lib/libelcethercat.so.0 (make install-lib)"
    fail=1
  fi
  echo
  echo "-- tools --"
  local t
  for t in elc_bus elc_sdo elc_config; do
    if command -v "${t}" >/dev/null 2>&1; then
      grn "${t}: $(command -v "${t}")"
    elif [ -x "${ELC_SRC}/tools/${t}" ]; then
      ylw "${t}: only in tree ${ELC_SRC}/tools/${t} (install-tools)"
      fail=1
    else
      red "${t}: not found"
      fail=1
    fi
  done
  echo
  echo "-- iod --"
  if pidof iod-elc >/dev/null 2>&1; then
    grn "iod-elc running pid=$(pidof iod-elc)"
  else
    ylw "iod-elc not running"
  fi
  if [ -x /opt/latproc/iod/iod-elc ]; then
    if env -u LD_LIBRARY_PATH ldd /opt/latproc/iod/iod-elc 2>/dev/null | grep -q 'libelcethercat.so.0 => /'; then
      grn "iod-elc links libelcethercat (no LD_LIBRARY_PATH needed)"
    elif ldd /opt/latproc/iod/iod-elc 2>/dev/null | grep -q 'libelcethercat.so.0 => not found'; then
      ylw "iod-elc needs LD_LIBRARY_PATH or ldconfig for libelcethercat"
    fi
  fi
  if [ -d "${IOD_SVC}" ]; then
    svstat "${IOD_SVC}" 2>/dev/null || true
  fi
  return "${fail}"
}

cmd_verify() {
  if cmd_status; then
    grn "VERIFY OK"
    return 0
  fi
  red "VERIFY FAILED (see warnings above)"
  return 1
}

cmd_install_userland() {
  need_root
  if [ ! -d "${ELC_SRC}" ]; then
    red "ERROR: ELC_SRC not found: ${ELC_SRC}"
    exit 1
  fi
  echo "Building tools+lib in ${ELC_SRC}..."
  make -C "${ELC_SRC}" -j"$(nproc)" lib tools
  echo "Installing lib+headers to ${PREFIX}..."
  make -C "${ELC_SRC}" PREFIX="${PREFIX}" install-lib
  echo "Installing tools to ${PREFIX}/bin..."
  make -C "${ELC_SRC}" PREFIX="${PREFIX}" install-tools
  ldconfig || true
  grn "userland installed"
}

cmd_reload_module() {
  need_root
  ensure_modprobe_conf
  iod_stop
  echo "Unloading elc_ethercat (if loaded)..."
  rmmod elc_ethercat 2>/dev/null || true
  sleep 0.3
  echo "modprobe elc_ethercat cycle_cpu=${ELC_CYCLE_CPU} cycle_fifo_priority=${ELC_CYCLE_FIFO_PRIORITY}"
  if ! modprobe elc_ethercat \
      "cycle_cpu=${ELC_CYCLE_CPU}" \
      "cycle_fifo_priority=${ELC_CYCLE_FIFO_PRIORITY}"; then
    red "modprobe failed"
    if [ -f "${ELC_SRC}/kernel/elc_ethercat.ko" ]; then
      ylw "Trying insmod ${ELC_SRC}/kernel/elc_ethercat.ko"
      insmod "${ELC_SRC}/kernel/elc_ethercat.ko" \
        "cycle_cpu=${ELC_CYCLE_CPU}" \
        "cycle_fifo_priority=${ELC_CYCLE_FIFO_PRIORITY}"
    else
      exit 1
    fi
  fi
  sleep 0.3
  local prio
  prio=$(cat /sys/module/elc_ethercat/parameters/cycle_fifo_priority 2>/dev/null || echo 0)
  echo "cycle_cpu=$(cat /sys/module/elc_ethercat/parameters/cycle_cpu 2>/dev/null || echo '?')"
  echo "cycle_fifo_priority=${prio}"
  ls -l /dev/elc_ethercat0 2>/dev/null || ylw "no /dev/elc_ethercat0 yet"
  if [ "${prio}" != "${ELC_CYCLE_FIFO_PRIORITY}" ]; then
    ylw "WARN: wanted priority ${ELC_CYCLE_FIFO_PRIORITY}, got ${prio}"
  else
    grn "module RT params OK"
  fi
  iod_start
}

cmd_setup() {
  need_root
  cmd_install_userland
  ensure_modprobe_conf
  cmd_reload_module
  echo
  cmd_verify
}

usage() {
  sed -n '2,14p' "$0" | sed 's/^# \{0,1\}//'
  echo "Commands: status | verify | install-userland | reload-module | setup"
}

main() {
  local cmd="${1:-status}"
  case "${cmd}" in
    status) cmd_status; exit $? ;;
    verify) cmd_verify ;;
    install-userland|userland) cmd_install_userland ;;
    reload-module|reload|module) cmd_reload_module ;;
    setup|all) cmd_setup ;;
    -h|--help|help) usage ;;
    *)
      red "Unknown command: ${cmd}"
      usage
      exit 2
      ;;
  esac
}

main "$@"
