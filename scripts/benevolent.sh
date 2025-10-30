#!/bin/bash

# This script must be run as root to manage cgroups
if [ "$EUID" -ne 0 ]; then
  echo "Please run as root (using sudo)"
  exit 1
fi

# Create res directory if it doesn't exist
mkdir -p res

# --- Configuration ---
BASE_CASE=2048
MEM_LOW=4194304       # 4MiB
MEM_HIGH=2147483648   # 2GiB (High memory)
SLEEP_TIME=2.0        # Fluctuate every 2.0 seconds (slower = more benevolent)

CGROUP_NAME="benevolent"
CGROUP_PATH="/sys/fs/cgroup/$CGROUP_NAME"
RESULTS_FILE="res/benevolent_results.txt"

# Temp log files
HIRSCHBERG_LOG="res/benevolent_hirschberg.log"
OBLIVIOUS_LOG="res/benevolent_oblivious.log"

# Clear log files
rm -f $HIRSCHBERG_LOG $OBLIVIOUS_LOG

# Clear results file
rm -f $RESULTS_FILE

# Cleanup function
cleanup() {
    echo "Cleaning up cgroup: $CGROUP_NAME"
    rmdir "$CGROUP_PATH" 2>/dev/null || true
}

# Trap to ensure cleanup on exit
trap cleanup EXIT

# --- Cgroup Setup ---
echo "Setting up cgroup: $CGROUP_NAME"
mkdir -p "$CGROUP_PATH"
if [ $? -ne 0 ]; then
    echo "Failed to create cgroup directory. Exiting."
    exit 1
fi

# Set initial (high) memory limits & swap
echo $MEM_HIGH > "$CGROUP_PATH/memory.max"
echo 2147483648 > "$CGROUP_PATH/memory.swap.max"
echo 0 > "$CGROUP_PATH/memory.oom.group"
echo "Cgroup setup complete. Swap limit: 2GiB."

# Write setup info to results file
echo "Cgroup setup: $CGROUP_NAME (Benevolent)" >> $RESULTS_FILE
echo "Memory fluctuating (slowly) between 4MiB and 2GiB." >> $RESULTS_FILE
echo "BASE_CASE: $BASE_CASE" >> $RESULTS_FILE
echo "" >> $RESULTS_FILE
echo "N, Hirschberg_IO, Oblivious_IO, Ratio" >> $RESULTS_FILE

for N in 32768 65536 131072 262144 524288 1048576; do
    echo "Running BENEVOLENT test for N = $N"

    # --- Run Hirschberg (Benevolent) ---
    echo "  Running Hirschberg (benevolent) for N = $N"
    
    # Set memory to HIGH to start
    echo $MEM_HIGH > "$CGROUP_PATH/memory.max"
    sync; echo 3 > /proc/sys/vm/drop_caches

    # Run in the background
    stdbuf -o0 cgexec -g memory:$CGROUP_NAME bin/lcs_hirschberg $N 1 $BASE_CASE < rsrc/data-$N.in >> $HIRSCHBERG_LOG 2>&1 &
    LCS_PID=$!

    # ** This is the benevolent (slow) fluctuation loop **
    while kill -0 $LCS_PID 2>/dev/null; do
        echo $MEM_LOW > "$CGROUP_PATH/memory.max"
        sleep $SLEEP_TIME
        echo $MEM_HIGH > "$CGROUP_PATH/memory.max"
        sleep $SLEEP_TIME
    done

    # Wait and get the result
    wait $LCS_PID
    LCS_HIRSCHBERG_IO=$(cat $HIRSCHBERG_LOG | grep 'I/Os' | tail -1 | awk '{print $4}')
    LCS_HIRSCHBERG_IO=${LCS_HIRSCHBERG_IO:-0}

    # --- Run Oblivious (Benevolent) ---
    echo "  Running Oblivious (benevolent) for N = $N"

    # Set memory to HIGH to start
    echo $MEM_HIGH > "$CGROUP_PATH/memory.max"
    sync; echo 3 > /proc/sys/vm/drop_caches

    # Run in the background
    stdbuf -o0 cgexec -g memory:$CGROUP_NAME bin/lcs_oblivious $N 1 $BASE_CASE < rsrc/data-$N.in >> $OBLIVIOUS_LOG 2>&1 &
    LCS_PID=$!

    # ** Run the same benevolent (slow) loop **
    while kill -0 $LCS_PID 2>/dev/null; do
        echo $MEM_LOW > "$CGROUP_PATH/memory.max"
        sleep $SLEEP_TIME
        echo $MEM_HIGH > "$CGROUP_PATH/memory.max"
        sleep $SLEEP_TIME
    done

    # Wait and get the result
    wait $LCS_PID
    LCS_OBLIVIOUS_IO=$(cat $OBLIVIOUS_LOG | grep 'I/Os' | tail -1 | awk '{print $4}')
    LCS_OBLIVIOUS_IO=${LCS_OBLIVIOUS_IO:-0}

    # --- Record Results ---
    if [ "$LCS_OBLIVIOUS_IO" -gt 0 ]; then
        RESULT=$(echo "scale=6; $LCS_HIRSCHBERG_IO / $LCS_OBLIVIOUS_IO" | bc -l)
        echo "$N, $LCS_HIRSCHBERG_IO, $LCS_OBLIVIOUS_IO, $RESULT" >> $RESULTS_FILE
    else
        echo "$N, $LCS_HIRSCHBERG_IO, $LCS_OBLIVIOUS_IO, 0" >> $RESULTS_FILE
    fi
done

echo "Benevolent experiment complete. Results saved to $RESULTS_FILE."
