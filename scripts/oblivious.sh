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
MEM_LIMIT=4194304     # 4MiB (This is the TOTAL pool they all share)
SWAP_LIMIT=2147483648 # 2GiB
NUM_INSTANCES=3       # Number of concurrent programs to run

CGROUP_NAME="oblivious"
CGROUP_PATH="/sys/fs/cgroup/$CGROUP_NAME"
RESULTS_FILE="res/oblivious_results.txt"

# Temp log directory
LOG_DIR="res/oblivious_logs"
mkdir -p $LOG_DIR

# Clear log files
rm -f $LOG_DIR/* # Clear old logs

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

# Set memory limits using variables
echo $MEM_LIMIT > "$CGROUP_PATH/memory.max"
echo $SWAP_LIMIT > "$CGROUP_PATH/memory.swap.max"
echo 0 > "$CGROUP_PATH/memory.oom.group"
echo "Cgroup setup complete. Memory limit: 4MiB, Swap limit: 2GiB."

# Write setup info to results file
echo "Cgroup setup: $CGROUP_NAME (Oblivious)" >> $RESULTS_FILE
echo "Memory limit: $(($MEM_LIMIT / 1024 / 1024))MiB (Shared by $NUM_INSTANCES instances)" >> $RESULTS_FILE
echo "BASE_CASE: $BASE_CASE" >> $RESULTS_FILE
echo "" >> $RESULTS_FILE
echo "N, Hirschberg_IO_Avg, Oblivious_IO_Avg, Ratio" >> $RESULTS_FILE

for N in 32768 65536 131072 262144 524288 1048576; do
    echo "Running OBLIVIOUS test for N = $N"
    
    # --- Run Hirschberg (Oblivious) ---
    echo "  Running $NUM_INSTANCES concurrent Hirschberg..."
    sync; echo 3 > /proc/sys/vm/drop_caches
    
    # Run all instances in the background
    for i in $(seq 1 $NUM_INSTANCES); do
        stdbuf -o0 cgexec -g memory:$CGROUP_NAME bin/lcs_hirschberg $N 1 $BASE_CASE < rsrc/data-$N.in > "$LOG_DIR/oblivious_hirschberg_${N}_$i.log" 2>&1 &
    done
    
    # Wait for all background jobs to finish
    wait
    
    # Calculate average I/Os
    HIRSCHBERG_TOTAL_IO=0
    for i in $(seq 1 $NUM_INSTANCES); do
        IO=$(cat "$LOG_DIR/oblivious_hirschberg_${N}_$i.log" | grep 'I/Os' | tail -1 | awk '{print $4}')
        HIRSCHBERG_TOTAL_IO=$(($HIRSCHBERG_TOTAL_IO + ${IO:-0}))
    done
    # Use bc for floating point division
    LCS_HIRSCHBERG_IO_AVG=$(echo "scale=2; $HIRSCHBERG_TOTAL_IO / $NUM_INSTANCES" | bc)

    # --- Run Oblivious (Oblivious) ---
    echo "  Running $NUM_INSTANCES concurrent Oblivious..."
    sync; echo 3 > /proc/sys/vm/drop_caches

    for i in $(seq 1 $NUM_INSTANCES); do
        stdbuf -o0 cgexec -g memory:$CGROUP_NAME bin/lcs_oblivious $N 1 $BASE_CASE < rsrc/data-$N.in > "$LOG_DIR/oblivious_oblivious_${N}_$i.log" 2>&1 &
    done
    
    wait

    OBLIVIOUS_TOTAL_IO=0
    for i in $(seq 1 $NUM_INSTANCES); do
        IO=$(cat "$LOG_DIR/oblivious_oblivious_${N}_$i.log" | grep 'I/Os' | tail -1 | awk '{print $4}')
        OBLIVIOUS_TOTAL_IO=$(($OBLIVIOUS_TOTAL_IO + ${IO:-0}))
    done
    LCS_OBLIVIOUS_IO_AVG=$(echo "scale=2; $OBLIVIOUS_TOTAL_IO / $NUM_INSTANCES" | bc)
    
    # --- Record Results ---
    # We must use bc for the comparison since the numbers might be floats (e.g., "150.33")
    if (( $(echo "$LCS_OBLIVIOUS_IO_AVG > 0" | bc -l) )); then
        RESULT=$(echo "scale=6; $LCS_HIRSCHBERG_IO_AVG / $LCS_OBLIVIOUS_IO_AVG" | bc -l)
        echo "$N, $LCS_HIRSCHBERG_IO_AVG, $LCS_OBLIVIOUS_IO_AVG, $RESULT" >> $RESULTS_FILE
    else
        echo "$N, $LCS_HIRSCHBERG_IO_AVG, $LCS_OBLIVIOUS_IO_AVG, 0" >> $RESULTS_FILE
    fi
done

echo "Oblivious experiment complete. Results saved to $RESULTS_FILE."
