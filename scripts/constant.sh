#!/bin/bash

now=$(date)
echo "$now"

if [ "$EUID" -ne 0 ]; then
    echo "Please run as root (using sudo)"
    exit 1
fi

sudo -u "$SUDO_USER" mkdir -p res/constant

BASE_CASE=32
MEM_LIMIT=2097152
SWAP_LIMIT=536870912

CGROUP_NAME="constant"
CGROUP_PATH="/sys/fs/cgroup/$CGROUP_NAME"
RESULTS_FILE="res/constant/constant_results.txt"

LOG_DIR="res/constant"

HIRSCHBERG_LOG="$LOG_DIR/constant_hirschberg.log"
OBLIVIOUS_LOG="$LOG_DIR/constant_oblivious.log"

rm -f $HIRSCHBERG_LOG $OBLIVIOUS_LOG $RESULTS_FILE

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
echo "Memory limit: $(($MEM_LIMIT / 1024 / 1024))MiB" >> $RESULTS_FILE
echo "Swap limit: $SWAP_LIMIT" >> $RESULTS_FILE
echo "BASE_CASE: $BASE_CASE" >> $RESULTS_FILE
echo "" >> $RESULTS_FILE
echo "N, Hirschberg_IO, Oblivious_IO, Ratio" >> $RESULTS_FILE

for N in "${@:-131072}"; do
    echo "Running CONSTANT test for N = $N"
    
    echo "  Running Hirschberg (constant)..."
    sync; echo 3 > /proc/sys/vm/drop_caches
    ( echo $BASHPID > "$CGROUP_PATH/cgroup.procs"; exec stdbuf -o0 nice -n 10 ./bin/lcs_hirschberg $N 1 $BASE_CASE ) < rsrc/data-$N.in >> $HIRSCHBERG_LOG 2>&1
    
    LCS_HIRSCHBERG_IO=$(cat $HIRSCHBERG_LOG | grep 'I/Os' | tail -1 | awk '{print $4}')
    LCS_HIRSCHBERG_IO=${LCS_HIRSCHBERG_IO:-0}

    echo "  Running Oblivious (constant)..."
    sync; echo 3 > /proc/sys/vm/drop_caches

    ( echo $BASHPID > "$CGROUP_PATH/cgroup.procs"; exec stdbuf -o0 nice -n 10 ./bin/lcs_oblivious $N 1 $BASE_CASE ) < rsrc/data-$N.in >> $OBLIVIOUS_LOG 2>&1

    LCS_OBLIVIOUS_IO=$(cat $OBLIVIOUS_LOG | grep 'I/Os' | tail -1 | awk '{print $4}')
    LCS_OBLIVIOUS_IO=${LCS_OBLIVIOUS_IO:-0}

    if [ "$LCS_OBLIVIOUS_IO" -gt 0 ]; then
        RESULT=$(echo "scale=6; $LCS_HIRSCHBERG_IO / $LCS_OBLIVIOUS_IO" | bc -l)
        echo "$N, $LCS_HIRSCHBERG_IO, $LCS_OBLIVIOUS_IO, $RESULT" >> $RESULTS_FILE
    else
        echo "$N, $LCS_HIRSCHBERG_IO, $LCS_OBLIVIOUS_IO, 0" >> $RESULTS_FILE
    fi
done

echo "Constant experiment complete. Results saved to $RESULTS_FILE."

chown -R $SUDO_USER:$SUDO_USER res/constant/
