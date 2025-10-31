#!/bin/bash

now=$(date)
echo "$now"

# This script must be run as root to manage cgroups
if [ "$EUID" -ne 0 ]; then
    echo "Please run as root (using sudo)"
    exit 1
fi
set -e # Exit on error

# --- Configuration ---
CGROUP_NAME="benevolent"
CGROUP_PATH="/sys/fs/cgroup/$CGROUP_NAME"
BASE_CASE=2048
RESULTS_FILE="res/benevolent/benevolent_results.txt"

# Set ONE static, HIGH memory limit for the cgroup
MEM_LIMIT_BYTES=2147483648 # 2GiB
MEM_LIMIT_MB=2048          # 2GiB
SWAP_LIMIT=2147483648      # 2GiB

# Temp log files
HIRSCHBERG_LOG="res/benevolent/benevolent_hirschberg.log"
OBLIVIOUS_LOG="res/benevolent/benevolent_oblivious.log"

# Clear files
rm -f res/benevolent/balloon/*
rm -f $HIRSCHBERG_LOG $OBLIVIOUS_LOG
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
echo $MEM_LIMIT_BYTES > "$CGROUP_PATH/memory.max"
echo $SWAP_LIMIT > "$CGROUP_PATH/memory.swap.max"
echo 0 > "$CGROUP_PATH/memory.oom.group"
mkdir -p res/benevolent/balloon # Ensure directory exists for balloon

# Write headers to results file
echo "Cgroup setup: $CGROUP_NAME" >> $RESULTS_FILE
echo "Memory limit: ${MEM_LIMIT_MB}MiB" >> $RESULTS_FILE
echo "Swap limit: $(($SWAP_LIMIT / 1024 / 1024))GiB" >> $RESULTS_FILE
echo "BASE_CASE: $BASE_CASE" >> $RESULTS_FILE
echo "" >> $RESULTS_FILE
echo "N, Hirschberg_IO, Oblivious_IO, Ratio" >> $RESULTS_FILE

# --- Run Experiment Loop ---
for N in 32768 65536 131072; do
    echo "Running BENEVOLENT test for N = $N"
    
    # Phase 1: Generate scan log
    echo "  Generating scan log..."
    ./bin/lcs_hirschberg_instrumented $N 1 $BASE_CASE < "rsrc/data-$N.in" > /dev/null 2> "res/benevolent/balloon/scan_$N.log"
    
    # Phase 1: Generate benevolent profile from log
    echo "  Generating benevolent profile..."
    awk 'BEGIN { FS=","; print "0.0 4" } /START_SCAN/ { print $1 " 2048" } /END_SCAN/ { print $1 " 4" }' \
        "res/benevolent/balloon/scan_$N.log" > "res/benevolent/balloon/benevolent_$N.txt"
    
    # Phase 2: Run the experiment
    # Point to the profile file created above
    PROFILE_FILE="res/benevolent/balloon/benevolent_$N.txt"

    # --- 1. Run Hirschberg (non-adaptive) ---
    echo "  Running Hirschberg (benevolent)..."
    sync; echo 3 > /proc/sys/vm/drop_caches

    # Start the Balloon in the background (Mode 1)
    # Args: 1=Mode, CgroupMem(MB), StartMem(MB), NumBalloons, BalloonID, ProfileFile
    # We give a "StartMem" of 4MB, which matches our profile's 0.0s time.
    stdbuf -o0 cgexec -g memory:$CGROUP_NAME ./bin/balloon 1 \
        $MEM_LIMIT_MB 4 1 1 $PROFILE_FILE > res/benevolent/balloon/balloon_$N.log 2>&1 &
    BALLOON_PID=$!

    # Start the CLEAN lcs_hirschberg
    stdbuf -o0 cgexec -g memory:$CGROUP_NAME ./bin/lcs_hirschberg $N 1 $BASE_CASE \
        < "rsrc/data-$N.in" >> $HIRSCHBERG_LOG 2>&1 &
    LCS_PID=$!

    wait $LCS_PID
    kill $BALLOON_PID 2>/dev/null # Balloon is in an infinite loop
    wait $BALLOON_PID 2>/dev/null || true # Ignore "Terminated" error
    
    # --- 2. Run Oblivious (adaptive) ---
    echo "  Running Oblivious (benevolent)..."
    sync; echo 3 > /proc/sys/vm/drop_caches
    
    # Run the SAME experiment with lcs_oblivious
    stdbuf -o0 cgexec -g memory:$CGROUP_NAME ./bin/balloon 1 \
        $MEM_LIMIT_MB 4 1 1 $PROFILE_FILE > res/benevolent/balloon/balloon_oblivious_$N.log 2>&1 &
    BALLOON_PID=$!
    
    stdbuf -o0 cgexec -g memory:$CGROUP_NAME ./bin/lcs_oblivious $N 1 $BASE_CASE \
        < "rsrc/data-$N.in" >> $OBLIVIOUS_LOG 2>&1 &
    LCS_PID=$!
    
    wait $LCS_PID
    kill $BALLOON_PID 2>/dev/null
    wait $BALLOON_PID 2>/dev/null || true
    
    # --- 3. Record Results ---
    LCS_HIRSCHBERG_IO=$(cat $HIRSCHBERG_LOG | grep 'I/Os' | tail -1 | awk '{print $4}')
    LCS_HIRSCHBERG_IO=${LCS_HIRSCHBERG_IO:-0} # Default to 0 if not found
    
    LCS_OBLIVIOUS_IO=$(cat $OBLIVIOUS_LOG | grep 'I/Os' | tail -1 | awk '{print $4}')
    LCS_OBLIVIOUS_IO=${LCS_OBLIVIOUS_IO:-0} # Default to 0 if not found

    if [ "$LCS_OBLIVIOUS_IO" -gt 0 ]; then
        RESULT=$(echo "scale=6; $LCS_HIRSCHBERG_IO / $LCS_OBLIVIOUS_IO" | bc -l)
        echo "$N, $LCS_HIRSCHBERG_IO, $LCS_OBLIVIOUS_IO, $RESULT" >> $RESULTS_FILE
    else
        echo "$N, $LCS_HIRSCHBERG_IO, $LCS_OBLIVIOUS_IO, 0" >> $RESULTS_FILE
    fi
done

echo "Benevolent experiment complete. Results saved to $RESULTS_FILE."

# Fix permissions
chown -R $SUDO_USER:$SUDO_USER res/benevolent/
