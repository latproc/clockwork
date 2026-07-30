#!/bin/bash
# Quick memory status for iod (+ optional MEMSNAPSHOT tail).
set -u
LATPROC="${LATPROC:-/opt/latproc}"
CSV="${IOD_MEMORY_OUTPUT_DIR:-$LATPROC/sampling/iod-memory}/memory.csv"
EVENTS="${IOD_MEMORY_OUTPUT_DIR:-$LATPROC/sampling/iod-memory}/events.log"

echo "=== $(date -u +%Y-%m-%dT%H:%M:%SZ) local $(date +%H:%M:%S) ==="
PID=""
if [ -n "${IOD_MEMORY_IOD_PID:-}" ]; then
  PID="$IOD_MEMORY_IOD_PID"
else
  PID=$(ps -eo pid=,args= | awk '
    $2 ~ /(^|\/)iod$/ ||
    ($2 ~ /(^|\/)(iod_sdo|iod-elc)[^\/[:space:]]*$/ && $0 !~ /(^|[[:space:]])-t([[:space:]]|$)/) {
      print $1; exit
    }')
fi

if [ -z "$PID" ] || [ ! -r "/proc/$PID/status" ]; then
  echo "iod: not running"
else
  echo "iod PID=$PID"
  ps -o pid,etime,pcpu,rss,vsz,nlwp,cmd -p "$PID" 2>/dev/null || true
  grep -E 'VmSize|VmRSS|VmData|VmPeak|Threads' "/proc/$PID/status" 2>/dev/null || true
  ls -la "/proc/$PID/exe" 2>/dev/null || true
fi

if command -v ethercat >/dev/null 2>&1; then
  echo "=== ethercat ==="
  ethercat master 2>/dev/null | grep -E 'Phase|Active|Link|Tx errors|frame rate' | head -10 || true
fi

if [ -f "$EVENTS" ]; then
  echo "=== events (last 8) ==="
  tail -8 "$EVENTS"
fi

if [ -f "$CSV" ] && [ -n "$PID" ]; then
  echo "=== memory.csv (this PID, summary) ==="
  python3 - "$CSV" "$PID" <<'PY' 2>/dev/null || true
import csv, sys
from datetime import datetime
path, pid = sys.argv[1], sys.argv[2]
rows=[]
with open(path) as f:
    for r in csv.DictReader(f):
        if r.get("pid")==pid and r.get("rss_kb","").isdigit():
            rows.append(r)
if not rows:
    print("no samples yet for this pid")
    raise SystemExit
a,b=rows[0],rows[-1]
t0=datetime.fromisoformat(a["timestamp_utc"].replace("Z","+00:00"))
t1=datetime.fromisoformat(b["timestamp_utc"].replace("Z","+00:00"))
mins=max((t1-t0).total_seconds()/60, 0.1)
dr=(int(b["rss_kb"])-int(a["rss_kb"]))/1024
print(f"samples={len(rows)} elapsed={mins:.1f} min")
print(f"RSS {int(a['rss_kb'])/1024:.1f} -> {int(b['rss_kb'])/1024:.1f} MiB  ({dr/mins:+.3f} MiB/min)")
print(f"VSZ {int(a['vmsize_kb'])/1024:.1f} -> {int(b['vmsize_kb'])/1024:.1f} MiB")
print(f"latest {b['timestamp_utc']}")
PY
fi

MEMLOG="${IOD_MEMSNAPSHOT_LOG:-$LATPROC/sampling/iod-memory/memsnapshot.log}"
echo "=== recent MEMSNAPSHOT (file, last 6) ==="
if [ -s "$MEMLOG" ]; then
  tail -6 "$MEMLOG"
else
  echo "(none in $MEMLOG — enable DEBUG_MEMSNAPSHOT in iod.conf or scripts/iod_enable_memsnapshot.sh; needs process age ≥ ~5 min)"
fi

if command -v svstat >/dev/null 2>&1; then
  echo "=== services ==="
  svstat /etc/service/iod /etc/service/memory_monitor 2>/dev/null || true
fi
