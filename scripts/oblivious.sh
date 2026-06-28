#!/bin/bash

now=$(date)
echo "$now"

if [ "$EUID" -ne 0 ]; then
    echo "Please run as root (using sudo)"
    exit 1
fi

mkdir -p res/oblivious

BASE_CASE=256
MEM_LIMIT=8388608
SWAP_LIMIT=max
NUM_INSTANCES=4

CGROUP_NAME="oblivious"
CGROUP_PATH="/sys/fs/cgroup/$CGROUP_NAME"
RESULTS_FILE="res/oblivious/oblivious_results.txt"

LOG_DIR="res/oblivious"

rm -f $LOG_DIR/oblivious_*.log
rm -f $RESULTS_FILE

cleanup() {
    echo "Cleaning up cgroup: $CGROUP_NAME"
    rmdir "$CGROUP_PATH" 2>/dev/null || true
}

trap cleanup EXIT

echo "Setting up cgroup: $CGROUP_NAME"
mkdir -p "$CGROUP_PATH"
if [ $? -ne 0 ]; then
    echo "Failed to create cgroup directory. Exiting."
    exit 1
fi

echo $MEM_LIMIT > "$CGROUP_PATH/memory.max"
echo $SWAP_LIMIT > "$CGROUP_PATH/memory.swap.max"
echo "Cgroup setup complete. Memory limit: $(($MEM_LIMIT / 1024 / 1024))MiB, Swap limit: $SWAP_LIMIT."

echo "Cgroup setup: $CGROUP_NAME" >> $RESULTS_FILE
echo "Memory limit: $(($MEM_LIMIT / 1024 / 1024))MiB (Shared by $NUM_INSTANCES instances)" >> $RESULTS_FILE
echo "Swap limit: $SWAP_LIMIT" >> $RESULTS_FILE
echo "BASE_CASE: $BASE_CASE" >> $RESULTS_FILE
echo "" >> $RESULTS_FILE
echo "N, Hirschberg_IO_Avg, Oblivious_IO_Avg, Ratio" >> $RESULTS_FILE
echo $$ > "$CGROUP_PATH/cgroup.procs"

for N in 131072; do
    echo "Running OBLIVIOUS test for N = $N"
    
    echo "  Running $NUM_INSTANCES Hirschberg..."
    sync; echo 3 > /proc/sys/vm/drop_caches
    
    for i in $(seq 1 $NUM_INSTANCES); do
        stdbuf -o0 nice -n 10 ./bin/lcs_hirschberg $N 1 $BASE_CASE < rsrc/data-$N.in > "$LOG_DIR/oblivious_hirschberg_${N}_$i.log" 2>&1 &
    done
    
    wait
    
    HIRSCHBERG_TOTAL_IO=0
    for i in $(seq 1 $NUM_INSTANCES); do
        IO=$(cat "$LOG_DIR/oblivious_hirschberg_${N}_$i.log" | grep 'I/Os' | tail -1 | awk '{print $4}')
        HIRSCHBERG_TOTAL_IO=$(($HIRSCHBERG_TOTAL_IO + ${IO:-0}))
    done
    LCS_HIRSCHBERG_IO_AVG=$(echo "scale=2; $HIRSCHBERG_TOTAL_IO / $NUM_INSTANCES" | bc)

    echo "  Running $NUM_INSTANCES Oblivious..."
    sync; echo 3 > /proc/sys/vm/drop_caches

    for i in $(seq 1 $NUM_INSTANCES); do
        stdbuf -o0 nice -n 10 ./bin/lcs_oblivious $N 1 $BASE_CASE < rsrc/data-$N.in > "$LOG_DIR/oblivious_oblivious_${N}_$i.log" 2>&1 &
    done
    
    wait

    OBLIVIOUS_TOTAL_IO=0
    for i in $(seq 1 $NUM_INSTANCES); do
        IO=$(cat "$LOG_DIR/oblivious_oblivious_${N}_$i.log" | grep 'I/Os' | tail -1 | awk '{print $4}')
        OBLIVIOUS_TOTAL_IO=$(($OBLIVIOUS_TOTAL_IO + ${IO:-0}))
    done
    LCS_OBLIVIOUS_IO_AVG=$(echo "scale=2; $OBLIVIOUS_TOTAL_IO / $NUM_INSTANCES" | bc)
    
    if (( $(echo "$LCS_OBLIVIOUS_IO_AVG > 0" | bc -l) )); then
        RESULT=$(echo "scale=6; $LCS_HIRSCHBERG_IO_AVG / $LCS_OBLIVIOUS_IO_AVG" | bc -l)
        echo "$N, $LCS_HIRSCHBERG_IO_AVG, $LCS_OBLIVIOUS_IO_AVG, $RESULT" >> $RESULTS_FILE
    else
        echo "$N, $LCS_HIRSCHBERG_IO_AVG, $LCS_OBLIVIOUS_IO_AVG, 0" >> $RESULTS_FILE
    fi
done

echo "Oblivious experiment complete. Results saved to $RESULTS_FILE."

chown -R $SUDO_USER:$SUDO_USER res/oblivious/
