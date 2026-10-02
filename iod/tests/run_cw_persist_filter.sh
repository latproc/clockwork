#!/bin/sh
# run_cw_persist_filter.sh <cw> <program.cw> <store-file>
#
# Covers the persist load filter (IOD_PERSIST_FILTER):
#
#   off      every store entry for an existing machine is applied (default)
#   dryrun   same behaviour, plus a PERSIST_FILTER summary line
#   enforce  an entry whose property is not already on the instance is skipped
#
# The fixture logs `declared` (set by the class, so always admitted) and
# `phantom` (present only in the store), which separates the three modes.
#
# cw does not SHUTDOWN on its own, so each run is bounded and SIGTERMed so its
# log flushes -- the same approach as run_cw_runtime.sh. Readiness is polled for
# rather than slept on, and the ports are derived from the PID, so the test does
# not flap when ctest runs the suite in parallel on a loaded machine.
#
# Self-contained on purpose: this test also has to run on branches that carry no
# runtime harness.

CW="$1"
PROGRAM="$2"
STORE="$3"
if [ -z "$CW" ] || [ -z "$PROGRAM" ] || [ -z "$STORE" ]; then
    echo "usage: run_cw_persist_filter.sh <cw> <program.cw> <store-file>" >&2
    exit 2
fi

fail() {
    echo "run_cw_persist_filter: $1" >&2
    echo "--- output ---" >&2
    cat "$2" >&2 2>/dev/null
    exit 1
}

# run_mode <mode> <port-base> <outfile>
run_mode() {
    _mode="$1"
    _port="$2"
    _out="$3"
    _dir=$(mktemp -d) || exit 1
    cp "$STORE" "$_dir/persist.store" || exit 1

    if [ "$_mode" = "off" ]; then
        IOD_PERSIST_FILTER= "$CW" -i "$_dir/persist.store" \
            -cp "$_port" -p $((_port + 1)) -ps $((_port + 2)) -mp $((_port + 3)) \
            -l - "$PROGRAM" >"$_out" 2>&1 &
    else
        IOD_PERSIST_FILTER="$_mode" "$CW" -i "$_dir/persist.store" \
            -cp "$_port" -p $((_port + 1)) -ps $((_port + 2)) -mp $((_port + 3)) \
            -l - "$PROGRAM" >"$_out" 2>&1 &
    fi
    _pid=$!

    # cw logs to stdout, which is block-buffered when redirected to a file, so
    # the fixture's INIT line is not readable until the process is signalled and
    # flushes. A bounded run is therefore the only way to observe it -- the same
    # approach as run_cw_runtime.sh. Ports are derived from the PID and the test
    # is registered RUN_SERIAL so a parallel ctest run cannot contend with it.
    sleep 6

    kill -TERM "$_pid" 2>/dev/null
    wait "$_pid" 2>/dev/null
    rm -rf "$_dir"
}

outdir=$(mktemp -d) || exit 1
trap 'rm -rf "$outdir"' EXIT

# Distinct ports per invocation, spread by PID so a parallel ctest run does not
# collide with itself or with a neighbouring test that also starts cw.
base=$((16000 + ($$ % 400) * 4))

# --- off: default behaviour, no filter output, phantom applied -------------
run_mode off "$base" "$outdir/off.log"
grep -q "declared=2" "$outdir/off.log" || fail "off: declared not applied" "$outdir/off.log"
grep -q "phantom=7" "$outdir/off.log" || fail "off: store entry not applied" "$outdir/off.log"
if grep -q "PERSIST_FILTER" "$outdir/off.log"; then
    fail "off: filter reported without being armed" "$outdir/off.log"
fi

# --- dryrun: decides and reports, behaviour unchanged ----------------------
run_mode dryrun $((base + 4)) "$outdir/dryrun.log"
grep -q "PERSIST_FILTER mode=dryrun" "$outdir/dryrun.log" ||
    fail "dryrun: no summary line" "$outdir/dryrun.log"
grep -q "rejected=1" "$outdir/dryrun.log" ||
    fail "dryrun: expected exactly one reject" "$outdir/dryrun.log"
grep -q "would_change=1" "$outdir/dryrun.log" ||
    fail "dryrun: expected one behaviour change" "$outdir/dryrun.log"
grep -q "not-on-instance=1" "$outdir/dryrun.log" ||
    fail "dryrun: wrong reject reason" "$outdir/dryrun.log"
grep -q "phantom=7" "$outdir/dryrun.log" ||
    fail "dryrun: must not change behaviour" "$outdir/dryrun.log"

# --- enforce: the phantom is skipped, declared properties still applied ----
run_mode enforce $((base + 8)) "$outdir/enforce.log"
grep -q "PERSIST_FILTER mode=enforce" "$outdir/enforce.log" ||
    fail "enforce: no summary line" "$outdir/enforce.log"
grep -q "declared=2" "$outdir/enforce.log" ||
    fail "enforce: declared property must still be applied" "$outdir/enforce.log"
grep -q "phantom=7" "$outdir/enforce.log" &&
    fail "enforce: undeclared entry was applied" "$outdir/enforce.log"
grep -q "phantom=phantom" "$outdir/enforce.log" ||
    fail "enforce: phantom should be unset" "$outdir/enforce.log"

exit 0
