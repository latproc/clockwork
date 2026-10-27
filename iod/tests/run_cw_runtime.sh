#!/bin/sh
# run_cw_runtime.sh <cw> <file> --must-contain <marker>
#
# Run a Clockwork program for a bounded time, then terminate it so LOG output
# is flushed, and check the combined output for the requested marker.

CW="$1"
FILE="$2"
shift 2

MUST_CONTAIN=""
MUST_NOT_CONTAIN=""
while [ $# -gt 0 ]; do
  case "$1" in
    --must-contain) MUST_CONTAIN="$2"; shift 2 ;;
    --must-not-contain) MUST_NOT_CONTAIN="$2"; shift 2 ;;
    *) shift ;;
  esac
done

out=$(mktemp) || exit 1
"$CW" -l - "$FILE" >"$out" 2>&1 &
pid=$!
sleep 6
kill -TERM "$pid" 2>/dev/null
wait "$pid" 2>/dev/null
combined=$(cat "$out")
rm -f "$out"

if [ -n "$MUST_CONTAIN" ]; then
  case "$combined" in
    *"$MUST_CONTAIN"*) ;;
    *)
      echo "run_cw_runtime: output missing '$MUST_CONTAIN' for $FILE"
      echo "$combined"
      exit 1
      ;;
  esac
fi
if [ -n "$MUST_NOT_CONTAIN" ]; then
  case "$combined" in
    *"$MUST_NOT_CONTAIN"*)
      echo "run_cw_runtime: output unexpectedly contains '$MUST_NOT_CONTAIN' for $FILE"
      echo "$combined"
      exit 1
      ;;
  esac
fi
exit 0
