#!/bin/bash

now=$(date)
echo "$now"

# This script must be run as root
if [ "$EUID" -ne 0 ]; then
    echo "Please run as root (using sudo)"
    exit 1
fi
set -e # Exit on error

# --- Configuration ---
CGROUP_NAME="adversarial"
CGROUP_PATH="/sys/fs/cgroup/$CGROUP_NAME"
BASE_CASE=256
RESULTS_FILE="res/adversarial/adversarial_results.txt"

# Set ONE static, HIGH memory limit for the cgroup
MEM_LIMIT_BYTES=2147483648 # 2GiB
MEM_LIMIT_MB=2048          # 2GiB
SWAP_LIMIT=2147483648      # 2GiB

# Temporary file for the signaling pipe
PIPE_FILE="/tmp/balloon_signal"

# This just lists the memory values the balloon will step through.
PROFILE_FILE="res/adversarial/adversarial_profile.txt"

# Temp log files
HIRSCHBERG_LOG="res/adversarial/adversarial_hirschberg.log"
OBLIVIOUS_LOG="res/adversarial/adversarial_oblivious.log"

# Clear files
rm -f res/adversarial/balloon/*
rm -f $HIRSCHBERG_LOG $OBLIVIOUS_LOG
rm -f $RESULTS_FILE

# Cleanup function
cleanup() {
    echo "Cleaning up..."
    rmdir "$CGROUP_PATH" 2>/dev/null || true
    rm -f $PIPE_FILE 2>/dev/null || true
}

# Trap to ensure cleanup on exit
trap cleanup EXIT

# --- Cgroup Setup ---
echo "Setting up cgroup: $CGROUP_NAME"
mkdir -p "$CGROUP_PATH"
echo $MEM_LIMIT_BYTES > "$CGROUP_PATH/memory.max"
echo $SWAP_LIMIT > "$CGROUP_PATH/memory.swap.max"
echo 0 > "$CGROUP_PATH/memory.oom.group"
mkdir -p res/adversarial/balloon # For logs

echo "Creating static adversarial profile..."
echo "2048" > $PROFILE_FILE
echo "4"    >> $PROFILE_FILE
echo "2048" >> $PROFILE_FILE

# Write headers to results file
echo "Cgroup setup: $CGROUP_NAME" >> $RESULTS_FILE
echo "Memory limit: ${MEM_LIMIT_MB}MiB" >> $RESULTS_FILE
echo "BASE_CASE: $BASE_CASE" >> $RESULTS_FILE
echo "" >> $RESULTS_FILE
echo "N, Hirschberg_IO, Oblivious_IO, Ratio" >> $RESULTS_FILE

# --- Run Experiment Loop ---
for N in 32768 65536 131072; do
    echo "Running ADVERSARIAL test for N = $N"
    
    # Create the pipe
    rm -f $PIPE_FILE
    mkfifo $PIPE_FILE

    # --- 1. Run Hirschberg (non-adaptive) ---
    echo "  Running Hirschberg (adversarial)..."
    sync; echo 3 > /proc/sys/vm/drop_caches

    # Start the Balloon in the background. It will mmap() its
    # first memory value (2048) and then block, waiting on the pipe.
    # Args: CgroupMem(MB), NumBalloons, BalloonID, ProfileFile, PipeFile
    stdbuf -o0 cgexec -g memory:$CGROUP_NAME ./bin/balloon \
        $MEM_LIMIT_MB 1 1 $PROFILE_FILE $PIPE_FILE \
        > res/adversarial/balloon/balloon_h_$N.log 2>&1 &
    BALLOON_PID=$!
    
    # Give the balloon a moment to start and block on the pipe
    sleep 0.5 

    # Start the lcs_hirschberg program.
    # It will run, send a signal (waking the balloon), scan,
    # send another signal, and finish.
    # Args: N, 1, BaseCase, PipeFile
    stdbuf -o0 cgexec -g memory:$CGROUP_NAME ./bin/lcs_hirschberg_instrumented \
        $N 1 $BASE_CASE $PIPE_FILE \
        < "rsrc/data-$N.in" >> $HIRSCHBERG_LOG 2>&1 &
    LCS_PID=$!

    wait $LCS_PID
    kill $BALLOON_PID 2>/dev/null
    wait $BALLOON_PID 2>/dev/null || true
    
    # --- 2. Run Oblivious (adaptive) ---
    echo "  Running Oblivious (adversarial)..."
    
    rm -f $PIPE_FILE
    mkfifo $PIPE_FILE
    sync; echo 3 > /proc/sys/vm/drop_caches

    # Start the Balloon again for the next run
    stdbuf -o0 cgexec -g memory:$CGROUP_NAME ./bin/balloon \
        $MEM_LIMIT_MB 1 1 $PROFILE_FILE $PIPE_FILE \
        > res/adversarial/balloon/balloon_o_$N.log 2>&1 &
    BALLOON_PID=$!
    sleep 0.5

    stdbuf -o0 cgexec -g memory:$CGROUP_NAME ./bin/lcs_oblivious_instrumented \
        $N 1 $BASE_CASE $PIPE_FILE \
        < "rsrc/data-$N.in" >> $OBLIVIOUS_LOG 2>&1 &
    LCS_PID=$!
    
    wait $LCS_PID
    kill $BALLOON_PID 2>/dev/null
    wait $BALLOON_PID 2>/dev/null || true

    # --- 3. Record Results ---
    LCS_HIRSCHBERG_IO=$(cat $HIRSCHBERG_LOG | grep 'I/Os' | tail -1 | awk '{print $4}')
    LCS_HIRSCHBERG_IO=${LCS_HIRSCHBERG_IO:-0} 
    
    LCS_OBLIVIOUS_IO=$(cat $OBLIVIOUS_LOG | grep 'I/Os' | tail -1 | awk '{print $4}')
    LCS_OBLIVIOUS_IO=${LCS_OBLIVIOUS_IO:-0}

    if [ "$LCS_OBLIVIOUS_IO" -gt 0 ]; then
        RESULT=$(echo "scale=6; $LCS_HIRSCHBERG_IO / $LCS_OBLIVIOUS_IO" | bc -l)
        echo "$N, $LCS_HIRSCHBERG_IO, $LCS_OBLIVIOUS_IO, $RESULT" >> $RESULTS_FILE
    else
        echo "$N, $LCS_HIRSCHBERG_IO, $LCS_OBLIVIOUS_IO, 0" >> $RESULTS_FILE
    fi
    
    rm -f $PIPE_FILE
done

echo "Adversarial experiment complete. Results saved to $RESULTS_FILE."
chown -R $SUDO_USER:$SUDO_USER res/adversarial/
