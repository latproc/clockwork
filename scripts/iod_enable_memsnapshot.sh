#!/bin/bash
# Enable DEBUG_MEMSNAPSHOT on a running iod (runtime flag; cleared on restart).
set -euo pipefail
IOSH="${IOSH:-/opt/latproc/iod/iosh}"
if [ ! -x "$IOSH" ]; then
  IOSH="$(command -v iosh 2>/dev/null || true)"
fi
[ -n "$IOSH" ] && [ -x "$IOSH" ] || { echo "iosh not found" >&2; exit 1; }
printf 'DEBUG DEBUG_MEMSNAPSHOT on;\n' | "$IOSH"
MEMLOG="${IOD_MEMSNAPSHOT_LOG:-/opt/latproc/sampling/iod-memory/memsnapshot.log}"
echo "MEMSNAPSHOT requested (runtime)."
echo "  After iod restart with updated iod-elc.sh:  tail -f ${MEMLOG}"
echo "  (std::cerr; not journalctl unless full verbose is on)"
echo "Note: once per ~60s after process age >= 5 minutes."
echo "Also enable persistently: uncomment/enable DEBUG_MEMSNAPSHOT in /opt/latproc/etc/iod.conf"
