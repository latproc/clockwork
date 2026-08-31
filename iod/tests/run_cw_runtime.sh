#!/bin/sh
# run_cw_runtime.sh <cw> <file> [--must-contain <marker>] [--must-not-contain <marker>]
#
# Runs `cw` on a Clockwork program for a bounded time, then SIGTERMs it so its
# LOG output is flushed, and checks the combined output for the given markers.
# Used for runtime tests that do not SHUTDOWN on their own (tests/exceptions.cw,
# tests/abort.cw), where pass/fail is expressed by which LOG lines are produced.

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
