#!/bin/bash

now=$(date)
echo "$now"

# Clean up any existing cgroup
if [ -d "/sys/fs/cgroup/constant" ]; then
    rmdir /sys/fs/cgroup/constant 2>/dev/null || true
fi

# Create new cgroup
mkdir -p /sys/fs/cgroup/constant

# Set memory limit to 4MB
echo 4194304 > /sys/fs/cgroup/constant/memory.max

# Allow swapping (set large swap limit)
echo 2147483648 > /sys/fs/cgroup/constant/memory.swap.max

# Disable OOM killer
echo 0 > /sys/fs/cgroup/constant/memory.oom.group

# Cleanup function
cleanup() {
    echo "Cleaning up cgroup"
    rmdir /sys/fs/cgroup/constant 2>/dev/null || true
}

# Trap to ensure cleanup on exit
trap cleanup EXIT

declare -a ip_size=( "8192" "16384" "32768" "65536" "131072" )
numruns=1

for (( j=0; j<=${#ip_size[@]}-1; j++ ));
do
    cgexec -g memory:constant ./bin/lcs_classic ${ip_size[$j]} $numruns < rsrc/data-${ip_size[$j]}.in
    cgexec -g memory:constant ./bin/lcs_hirschberg ${ip_size[$j]} $numruns 2048 < rsrc/data-${ip_size[$j]}.in
    cgexec -g memory:constant ./bin/lcs_oblivious ${ip_size[$j]} $numruns 2048 < rsrc/data-${ip_size[$j]}.in
done

echo "All runs complete."
