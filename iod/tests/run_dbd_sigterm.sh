#!/bin/sh
# run_dbd_sigterm.sh <dbd>
#
# Regression test: dbd must exit promptly on SIGTERM.
#
# It used to return from main() and then hang forever inside
# ~zmq::context_t(): SubscriptionManager leaked its setup REQ and monitor
# sockets, and zmq_ctx_term() blocks until every socket open in the context is
# closed. The signal handler did run, so the process looked like it "ignored"
# SIGTERM while it sat in the context destructor.
#
# No peers are started: dbd installs its handlers and enters the main loop
# whether or not it can reach iod/dbsvr, which is all this test needs.

DBD="$1"
if [ ! -x "$DBD" ]; then
  echo "run_dbd_sigterm: not an executable: $DBD"
  exit 1
fi

# Derived from the pid so parallel CTest runs do not collide.
base=$(( 21000 + ($$ % 20000) ))
out=$(mktemp) || exit 1

"$DBD" --host 127.0.0.1 --cwout "$base" \
  --dbsvr "tcp://127.0.0.1:$((base + 1))" \
  --notify "tcp://127.0.0.1:$((base + 2))" >"$out" 2>&1 &
pid=$!

# Give dbd time to install the handler and enter the main loop.
sleep 2
kill -TERM "$pid" 2>/dev/null

# kill -0 cannot distinguish a running child from an unreaped zombie, so use a
# watchdog: if dbd has not exited in 5s the watchdog SIGKILLs it and wait
# reports the signal status below.
( sleep 5; kill -9 "$pid" 2>/dev/null ) &
watchdog=$!

wait "$pid"
status=$?
kill "$watchdog" 2>/dev/null
wait "$watchdog" 2>/dev/null

if [ "$status" -ne 0 ]; then
  echo "run_dbd_sigterm: dbd did not exit cleanly on SIGTERM (wait status $status)"
  cat "$out"
  rm -f "$out"
  exit 1
fi

rm -f "$out"
exit 0
