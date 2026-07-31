#!/bin/bash
# Runtime toggle for iod verbose file logging (no iod restart).
# Requires a site boot wrapper stream filter that honors /tmp/iod-verbose
# (after one deploy restart that installs the filter, if used).
#
# Usage:
#   iod_verbose.sh on [ttl_seconds]   # default ttl 3600; touch/create /tmp/iod-verbose
#   iod_verbose.sh off                # rm switch → filter stops file logging
#   iod_verbose.sh status
#   iod_verbose.sh renew [ttl]        # refresh mtime / optional new TTL while already on
#
set -euo pipefail

SWITCH="${IOD_VERBOSE_SWITCH:-/tmp/iod-verbose}"
LOG="${IOD_LOG_FILE:-/tmp/iod.log}"
DEFAULT_TTL="${IOD_LOG_TTL_SEC:-3600}"
POLL="${IOD_VERBOSE_POLL_SEC:-2}"

usage() {
  sed -n '1,12p' "$0" | tail -n +2
  exit 0
}

status() {
  echo "switch: ${SWITCH}"
  echo "log:    ${LOG}"
  if [ ! -e "${SWITCH}" ]; then
    echo "state:  OFF (no switch file)"
    return 0
  fi
  local ttl age mtime now remain
  ttl="${DEFAULT_TTL}"
  if [ -f "${SWITCH}" ] && [ -s "${SWITCH}" ]; then
    read -r maybe <"${SWITCH}" || true
    if [[ "${maybe:-}" =~ ^[0-9]+$ ]]; then
      ttl="${maybe}"
    fi
  fi
  mtime=$(stat -c %Y "${SWITCH}" 2>/dev/null || echo 0)
  now=$(date +%s)
  age=$((now - mtime))
  remain=$((ttl - age))
  if [ "${remain}" -le 0 ]; then
    echo "state:  EXPIRED (age=${age}s ttl=${ttl}s) — filter will clear switch"
  else
    echo "state:  ON  age=${age}s ttl=${ttl}s remaining≈${remain}s"
    echo "note:   filter re-checks ~every ${POLL}s; no svc -t needed"
  fi
  if [ -f "${LOG}" ]; then
    echo "log_sz: $(stat -c %s "${LOG}") bytes  mtime=$(stat -c %y "${LOG}")"
  else
    echo "log_sz: (file not created yet — appears on first line after switch ON)"
  fi
}

cmd="${1:-status}"
case "${cmd}" in
  -h|--help) usage ;;
  status) status ;;
  on)
    ttl="${2:-${DEFAULT_TTL}}"
    if ! [[ "${ttl}" =~ ^[0-9]+$ ]]; then
      echo "TTL must be integer seconds" >&2
      exit 1
    fi
    echo "${ttl}" >"${SWITCH}"
    # Ensure mtime is "now" even if file existed
    touch "${SWITCH}"
    echo "${ttl}" >"${SWITCH}"
    echo "verbose ON (ttl=${ttl}s from now). Log → ${LOG}"
    echo "No iod restart required (if stream filter already running)."
    status
    ;;
  renew)
    ttl="${2:-}"
    if [ -n "${ttl}" ]; then
      if ! [[ "${ttl}" =~ ^[0-9]+$ ]]; then
        echo "TTL must be integer seconds" >&2
        exit 1
      fi
      echo "${ttl}" >"${SWITCH}"
    else
      touch "${SWITCH}"
    fi
    echo "verbose renew (mtime refreshed)."
    status
    ;;
  off)
    rm -f "${SWITCH}"
    echo "verbose OFF (switch removed). Filter stops writing ${LOG} within ~${POLL}s."
    ;;
  *)
    echo "Unknown command: ${cmd}" >&2
    usage
    ;;
esac
