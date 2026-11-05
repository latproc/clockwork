#!/bin/sh
set -eu

if [ "$#" -ne 5 ]; then
    echo "usage: run_persistd_sigterm.sh <persistd> <cw> <iocmd> <store-test> <source-dir>" >&2
    exit 2
fi

PERSISTD="$1"
CW="$2"
IOCMD="$3"
STORE_TEST="$4"
SOURCE_DIR="$5"
WORK=$(mktemp -d "${TMPDIR:-/tmp}/persistd-sigterm.XXXXXX")
setup_port=$((29000 + ($$ % 500)))
channel_port=$((setup_port + 500))
publish_port=$((setup_port + 1000))
store_port=$((setup_port + 1500))
modbus_port=$((setup_port + 2000))
cw_pid=
persistd_pid=

cleanup() {
    if [ -n "${persistd_pid}" ]; then
        kill -KILL "${persistd_pid}" 2>/dev/null || true
        wait "${persistd_pid}" 2>/dev/null || true
    fi
    if [ -n "${cw_pid}" ]; then
        kill -TERM "${cw_pid}" 2>/dev/null || true
        sleep 1
        kill -KILL "${cw_pid}" 2>/dev/null || true
        wait "${cw_pid}" 2>/dev/null || true
    fi
    rm -rf "${WORK}"
}
trap cleanup EXIT HUP INT TERM

sed -E "s/port[[:space:]]+27901;/port ${channel_port};/" \
    "${SOURCE_DIR}/persistence_channel.cw" > "${WORK}/channels.cw"
cp "${SOURCE_DIR}/../../tests/persistent_float.cw" "${WORK}/persistent_float.cw"

cd "${WORK}"
"${CW}" -i "${WORK}/persist.dat" -cp "${setup_port}" -p "${publish_port}" \
    -ps "${store_port}" -mp "${modbus_port}" \
    -l "${WORK}/cw.log" persistent_float.cw channels.cw >"${WORK}/cw.stdout" 2>&1 &
cw_pid=$!

sleep 1
if ! kill -0 "${cw_pid}" 2>/dev/null; then
    echo "cw exited during startup" >&2
    cat "${WORK}/cw.stdout" >&2
    exit 1
fi

"${PERSISTD}" --verbose --setup-port "${setup_port}" >"${WORK}/persistd.log" 2>&1 &
persistd_pid=$!

i=0
while [ "${i}" -lt 200 ]; do
    if [ -s "${WORK}/persist.dat" ]; then
        break
    fi
    if ! kill -0 "${persistd_pid}" 2>/dev/null; then
        echo "persistd exited before creating persist.dat" >&2
        cat "${WORK}/persistd.log" >&2
        exit 1
    fi
    sleep 0.1
    i=$((i + 1))
done

if [ ! -s "${WORK}/persist.dat" ]; then
    echo "persistd did not create persist.dat" >&2
    cat "${WORK}/persistd.log" >&2
    exit 1
fi

grep -q '^sample a ' "${WORK}/persist.dat"
grep -q '^sample e ' "${WORK}/persist.dat"
test "$(tail -c 1 "${WORK}/persist.dat" | wc -l | tr -d ' ')" -eq 1

(
    i=0
    while [ "${i}" -lt 500 ]; do
        printf 'PROPERTY sample b %s;\n' "${i}"
        i=$((i + 1))
    done
) | "${IOCMD}" -p "${setup_port}" >"${WORK}/iocmd.log" 2>&1 &
writer_pid=$!
sleep 0.1

kill -TERM "${persistd_pid}"
i=0
while kill -0 "${persistd_pid}" 2>/dev/null; do
    if [ "${i}" -ge 100 ]; then
        echo "persistd did not exit within 10 seconds of SIGTERM" >&2
        exit 1
    fi
    sleep 0.1
    i=$((i + 1))
done
wait "${persistd_pid}"
persistd_pid=
wait "${writer_pid}" 2>/dev/null || true

grep -q '^sample a ' "${WORK}/persist.dat"
grep -q '^sample e ' "${WORK}/persist.dat"
test "$(tail -c 1 "${WORK}/persist.dat" | wc -l | tr -d ' ')" -eq 1
PERSIST_STORE_EXTERNAL_FIXTURE="${WORK}/persist.dat" "${STORE_TEST}" \
    --gtest_color=no --gtest_brief=1 \
    --gtest_filter='PersistentStore.OptionalExternalProductionFixtureRoundTripsExactly' \
    >"${WORK}/store-term-check.log" 2>&1 ||
    { cat "${WORK}/store-term-check.log" >&2; exit 1; }

"${PERSISTD}" --verbose --setup-port "${setup_port}" >>"${WORK}/persistd.log" 2>&1 &
persistd_pid=$!
sleep 4
(
    i=0
    while [ "${i}" -lt 500 ]; do
        printf 'PROPERTY sample c %s;\n' "${i}"
        i=$((i + 1))
    done
) | "${IOCMD}" -p "${setup_port}" >"${WORK}/iocmd-kill.log" 2>&1 &
writer_pid=$!
sleep 0.1
kill -KILL "${persistd_pid}"
wait "${persistd_pid}" 2>/dev/null || true
persistd_pid=
wait "${writer_pid}" 2>/dev/null || true

PERSIST_STORE_EXTERNAL_FIXTURE="${WORK}/persist.dat" "${STORE_TEST}" \
    --gtest_color=no --gtest_brief=1 \
    --gtest_filter='PersistentStore.OptionalExternalProductionFixtureRoundTripsExactly' \
    >"${WORK}/store-kill-check.log" 2>&1 ||
    { cat "${WORK}/store-kill-check.log" >&2; exit 1; }
