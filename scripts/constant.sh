#!/bin/bash

# This script must be run as root to manage cgroups
if [ "$EUID" -ne 0 ]; then
    echo "Please run as root (using sudo)"
    exit 1
fi

# Create res directory if it doesn't exist
mkdir -p res

# --- Configuration ---
BASE_CASE=2048        # Base case for recursion, larger = more memory usage
MEM_LIMIT=4194304     # 4MiB
SWAP_LIMIT=2147483648 # 2GiB

CGROUP_NAME="constant"
CGROUP_PATH="/sys/fs/cgroup/$CGROUP_NAME"
RESULTS_FILE="res/constant_results.txt"

# Temp log files
HIRSCHBERG_LOG="res/constant_hirschberg.log"
OBLIVIOUS_LOG="res/constant_oblivious.log"

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

# Set memory limits using variables
echo $MEM_LIMIT > "$CGROUP_PATH/memory.max"
echo $SWAP_LIMIT > "$CGROUP_PATH/memory.swap.max"
echo 0 > "$CGROUP_PATH/memory.oom.group"
echo "Cgroup setup complete. Memory limit: 4MiB, Swap limit: 2GiB."

# Write setup info to results file
echo "Cgroup setup: $CGROUP_NAME" >> $RESULTS_FILE
echo "Memory limit: $(($MEM_LIMIT / 1024 / 1024))MiB" >> $RESULTS_FILE
echo "Swap limit: $(($SWAP_LIMIT / 1024 / 1024 / 1024))GiB" >> $RESULTS_FILE
echo "BASE_CASE: $BASE_CASE" >> $RESULTS_FILE
echo "" >> $RESULTS_FILE
echo "N, Hirschberg_IO, Oblivious_IO, Ratio" >> $RESULTS_FILE

for N in 32768 65536 131072 262144 524288 1048576; do
    echo "Running CONSTANT test for N = $N"
  
    # Clear caches
    sync; echo 3 > /proc/sys/vm/drop_caches
  
    # Run Hirschberg (non-adaptive) inside the cgroup
    echo "  Running Hirschberg (constant) for N = $N"
    stdbuf -o0 cgexec -g memory:$CGROUP_NAME bin/lcs_hirschberg $N 1 $BASE_CASE < rsrc/data-$N.in >> $HIRSCHBERG_LOG 2>&1
    LCS_HIRSCHBERG_IO=$(cat $HIRSCHBERG_LOG | grep 'I/Os' | tail -1 | awk '{print $4}')
    LCS_HIRSCHBERG_IO=${LCS_HIRSCHBERG_IO:-0}

    # Clear caches again
    sync; echo 3 > /proc/sys/vm/drop_caches

    # Run Oblivious (adaptive) inside the cgroup
    echo "  Running Oblivious (constant) for N = $N"
    stdbuf -o0 cgexec -g memory:$CGROUP_NAME bin/lcs_oblivious $N 1 $BASE_CASE < rsrc/data-$N.in >> $OBLIVIOUS_LOG 2>&1
    LCS_OBLIVIOUS_IO=$(cat $OBLIVIOUS_LOG | grep 'I/Os' | tail -1 | awk '{print $4}')
    LCS_OBLIVIOUS_IO=${LCS_OBLIVIOUS_IO:-0}

    # Calculate and print the normalized I/O for plotting
    if [ "$LCS_OBLIVIOUS_IO" -gt 0 ]; then
        RESULT=$(echo "scale=6; $LCS_HIRSCHBERG_IO / $LCS_OBLIVIOUS_IO" | bc -l)
        echo "$N, $LCS_HIRSCHBERG_IO, $LCS_OBLIVIOUS_IO, $RESULT" >> $RESULTS_FILE
    else
        echo "$N, $LCS_HIRSCHBERG_IO, $LCS_OBLIVIOUS_IO, 0" >> $RESULTS_FILE
    fi
done

echo "Constant experiment complete. Results saved to res/constant_results.txt."
