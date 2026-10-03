#!/bin/sh
#
# Dependency graph export check.
#
#   run_cw_graph.sh --cw <cw> --file <program.cw> --out <graph.dot>
#                   [--root <instance>] [--expect <needles>]
#
# Runs the export and then checks the graph against a list of required
# substrings. Each non-empty, non-'#' line of the expectation file is a
# substring that must appear; a line starting with '!' must not appear.
#
# The run is bounded with -t (test only): cw writes the graph, exports the
# modbus mapping and exits, instead of starting a plant that never shuts down.
# -t also writes the mapping file into the current directory, so the run happens
# in a scratch directory. This keeps the test self-contained: it needs no
# runtime harness and no --parse-only, so the same script works on every line.
#
# The checks are done with grep -F rather than in CMake because the needles are
# DOT fragments containing quotes, and CMake's string handling mangles
# multi-line content (file(STRINGS) runs the file through the list lexer).
set -eu

CW=""
FILE=""
OUT=""
ROOT=""
EXPECT=""

while [ $# -gt 0 ]; do
  case "$1" in
    --cw) CW="${2:-}"; shift 2 ;;
    --file) FILE="${2:-}"; shift 2 ;;
    --out) OUT="${2:-}"; shift 2 ;;
    --root) ROOT="${2:-}"; shift 2 ;;
    --expect) EXPECT="${2:-}"; shift 2 ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done

if [ -z "$CW" ] || [ -z "$FILE" ] || [ -z "$OUT" ]; then
  echo "usage: run_cw_graph.sh --cw <cw> --file <prog.cw> --out <graph.dot> [--root <inst>] [--expect <file>]" >&2
  exit 2
fi
if [ ! -x "$CW" ]; then
  echo "cw not executable: $CW" >&2
  exit 2
fi

work=$(mktemp -d) || exit 1
trap 'rm -rf "$work"' EXIT INT TERM

rm -f "$OUT"
rc=0
if [ -n "$ROOT" ]; then
  ( cd "$work" && "$CW" -g "$OUT" -r "$ROOT" -t "$FILE" ) > "$work/log" 2>&1 || rc=$?
else
  ( cd "$work" && "$CW" -g "$OUT" -t "$FILE" ) > "$work/log" 2>&1 || rc=$?
fi
if [ "$rc" -ne 0 ]; then
  echo "graph export exited $rc" >&2
  cat "$work/log" >&2
  exit 1
fi
if [ ! -s "$OUT" ]; then
  echo "graph was not written to $OUT" >&2
  cat "$work/log" >&2
  exit 1
fi

if [ -n "$EXPECT" ]; then
  if [ ! -f "$EXPECT" ]; then
    echo "expectation file not found: $EXPECT" >&2
    exit 2
  fi
  failed=0
  while IFS= read -r line; do
    case "$line" in
      ""|\#*) continue ;;
    esac
    negate=0
    case "$line" in
      '!'*) negate=1; line="${line#!}" ;;
    esac
    if [ "$negate" -eq 1 ]; then
      if grep -Fq -- "$line" "$OUT"; then
        echo "graph unexpectedly contains: $line" >&2
        failed=1
      fi
    else
      if ! grep -Fq -- "$line" "$OUT"; then
        echo "graph is missing: $line" >&2
        failed=1
      fi
    fi
  done < "$EXPECT"
  if [ "$failed" -ne 0 ]; then
    echo "--- graph ($OUT) ---" >&2
    cat "$OUT" >&2
    exit 1
  fi
fi

exit 0
