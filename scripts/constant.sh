#!/bin/bash

# This script must be run as root to manage cgroups
if [ "$EUID" -ne 0 ]; then
  echo "Please run as root (using sudo)"
  exit 1
fi

# --- Configuration ---
BASE_CASE=2048  # Adjust this value to control memory pressure (larger = more memory usage)

CGROUP_NAME="constant"
CGROUP_PATH="/sys/fs/cgroup/$CGROUP_NAME"

# --- Cgroup Setup ---
echo "Setting up cgroup: $CGROUP_NAME"
mkdir -p "$CGROUP_PATH"
if [ $? -ne 0 ]; then
    echo "Failed to create cgroup directory. Exiting."
    exit 1
fi

# Set memory limits
echo 4194304 > "$CGROUP_PATH/memory.max"
echo 2147483648 > "$CGROUP_PATH/memory.swap.max"
echo 0 > "$CGROUP_PATH/memory.oom.group"
echo "Cgroup setup complete. Memory limit: 4MiB, Swap limit: 2GiB."

# --- Experiment Loop ---
for N in 32768 65536 131072 262144 524288 1048576; do
  echo "Running for N = $N"
  
  # Clear caches
  sync; echo 3 > /proc/sys/vm/drop_caches
  
  # Run Hirschberg (non-adaptive) inside the cgroup
  # Base case is set to $BASE_CASE to force memory pressure
  LCS_HIRSCHBERG_IO=$(cgexec -g memory:$CGROUP_NAME bin/lcs_hirschberg $N 1 $BASE_CASE < rsrc/data-$N.in 2>&1 | grep 'I/Os' | awk '{print $5}')
  LCS_HIRSCHBERG_IO=${LCS_HIRSCHBERG_IO:-0}

  # Clear caches again
  sync; echo 3 > /proc/sys/vm/drop_caches

  # Run Oblivious (adaptive) inside the cgroup
  # Base case is set to $BASE_CASE to force memory pressure
  LCS_OBLIVIOUS_IO=$(cgexec -g memory:$CGROUP_NAME bin/lcs_oblivious $N 1 $BASE_CASE < rsrc/data-$N.in 2>&1 | grep 'I/Os' | awk '{print $5}')
  LCS_OBLIVIOUS_IO=${LCS_OBLIVIOUS_IO:-0}

  # Calculate and print the normalized I/O for plotting
  if [ "$LCS_OBLIVIOUS_IO" -gt 0 ]; then
    RESULT=$(echo "scale=6; $LCS_HIRSCHBERG_IO / $LCS_OBLIVIOUS_IO" | bc -l)
    echo "$N, $RESULT"
  else
    echo "$N, 0"
  fi
done

# --- Cgroup Cleanup ---
echo "Cleaning up cgroup: $CGROUP_NAME"
rmdir "$CGROUP_PATH"
echo "Experiment complete."
