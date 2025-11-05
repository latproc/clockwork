#!/bin/bash
# Memory leak testing script for Clockwork channel code
# Usage: ./test_channel_memory_leaks.sh [duration_seconds]

DURATION=${1:-60}
LOG_DIR="./memory_test_logs"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)

mkdir -p "$LOG_DIR"

echo "Starting channel memory leak test (duration: ${DURATION}s)"
echo "Logs will be saved to: $LOG_DIR"

# Function to cleanup on exit
cleanup() {
    echo "Cleaning up processes..."
    pkill -f "cw.*7910"
    pkill -f "cw.*7920" 
    pkill -f "valgrind.*cw"
    sleep 3
    pkill -f --signal 8 "cw.*7910"
    pkill -f --signal 8 "cw.*7920" 
    pkill -f --signal 8 "valgrind.*cw"
    pkill -8 "$0"
    wait 2>/dev/null
    echo "Cleanup complete"
}

trap cleanup TERM INT

# Start primary daemon with Valgrind
echo "Starting primary daemon with Valgrind..."
cd "$(dirname "$0")/.."
valgrind \
    --tool=memcheck \
    --leak-check=full \
    --show-leak-kinds=all \
    --track-origins=yes \
    --log-file="$LOG_DIR/valgrind_primary_${TIMESTAMP}.log" \
    ./iod/cw -cp 7910 tests/channel_stress_test.cw tests/cycling_counter.cw &

PRIMARY_PID=$!

# Wait for primary daemon to start
sleep 3

# Start secondary daemon with Valgrind  
echo "Starting secondary daemon with Valgrind..."
valgrind \
    --tool=memcheck \
    --leak-check=full \
    --show-leak-kinds=all \
    --track-origins=yes \
    --log-file="$LOG_DIR/valgrind_secondary_${TIMESTAMP}.log" \
    ./iod/cw -cp 7920 tests/channel_stress_secondary.cw tests/cycling_counter.cw &

SECONDARY_PID=$!

# Wait for both daemons to start
echo "GET SYSTEM STATE" | ./iod/iocmd -p 7910 >/dev/null
echo "GET SYSTEM STATE" | ./iod/iocmd -p 7920 >/dev/null

# Monitor memory usage
echo "Monitoring memory usage..."
{
    echo "Timestamp,Primary_RSS_MB,Primary_VSZ_MB,Secondary_RSS_MB,Secondary_VSZ_MB"
    while true; do
        TIMESTAMP_NOW=$(date '+%Y-%m-%d %H:%M:%S')
        
        # Get memory usage for both processes
        PRIMARY_MEM=$(ps -o pid,rss,vsz --no-headers -p $PRIMARY_PID 2>/dev/null || echo "0 0 0")
        SECONDARY_MEM=$(ps -o pid,rss,vsz --no-headers -p $SECONDARY_PID 2>/dev/null || echo "0 0 0")
        
        PRIMARY_RSS=$(echo $PRIMARY_MEM | awk '{print $2/1024}') # Convert to MB
        PRIMARY_VSZ=$(echo $PRIMARY_MEM | awk '{print $3/1024}')
        SECONDARY_RSS=$(echo $SECONDARY_MEM | awk '{print $2/1024}')
        SECONDARY_VSZ=$(echo $SECONDARY_MEM | awk '{print $3/1024}')
        
        echo "$TIMESTAMP_NOW,$PRIMARY_RSS,$PRIMARY_VSZ,$SECONDARY_RSS,$SECONDARY_VSZ"
        
        sleep 1
    done
} > "$LOG_DIR/memory_usage_${TIMESTAMP}.csv" &

MONITOR_PID=$!

# Start the stress test via iosh
echo "Starting stress test..."
sleep 2
echo "SEND stress_test_driver.start_test" | ./iod/iocmd -p 7910

# Run for specified duration
echo "Running test for $DURATION seconds..."
sleep $DURATION

# Stop monitoring
kill $MONITOR_PID 2>/dev/null

echo "Test completed. Check logs in $LOG_DIR/"
echo "Memory usage: $LOG_DIR/memory_usage_${TIMESTAMP}.csv"
echo "Valgrind primary: $LOG_DIR/valgrind_primary_${TIMESTAMP}.log"
echo "Valgrind secondary: $LOG_DIR/valgrind_secondary_${TIMESTAMP}.log"

# Generate summary
echo "=== Memory Usage Summary ===" 
echo "Final memory usage:"
ps -o pid,rss,vsz,cmd --no-headers -p $PRIMARY_PID $SECONDARY_PID 2>/dev/null || echo "Processes may have ended"

echo ""
echo "To analyze Valgrind output:"
echo "  grep 'definitely lost' $LOG_DIR/valgrind_*_${TIMESTAMP}.log"
echo "  grep 'possibly lost' $LOG_DIR/valgrind_*_${TIMESTAMP}.log"
echo "  grep 'RemoteClockworkCommandFilter' $LOG_DIR/valgrind_*_${TIMESTAMP}.log"