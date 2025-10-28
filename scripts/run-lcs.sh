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
echo "4194304" > /sys/fs/cgroup/constant/memory.max

# Allow swapping (set large swap limit)
echo "2147483648" > /sys/fs/cgroup/constant/memory.swap.max

# Enable OOM killer
echo "1" > /sys/fs/cgroup/constant/memory.oom.group

declare -a ip_size=( "8192" "16384" "32768" "65536" "131072" "262144" "524288" "1048576" "2097152" "4194304" "8388608" )
numruns=1

for (( j=0; j<=${#ip_size[@]}-1; j++ ));
do
    cgexec -g memory:/constant ./bin/lcs_classic ${ip_size[$j]} $numruns < rsrc/data-${ip_size[$j]}.in
    cgexec -g memory:/constant ./bin/lcs_hirschberg ${ip_size[$j]} $numruns < rsrc/data-${ip_size[$j]}.in
    cgexec -g memory:/constant ./bin/lcs_oblivious ${ip_size[$j]} $numruns < rsrc/data-${ip_size[$j]}.in
done

# Clean up
rmdir /sys/fs/cgroup/constant 2>/dev/null || true
