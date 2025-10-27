#!/bin/bash
set -ex
g++ -std=c++11 -Iinclude src/lcs_classic.c -o lcs_classic
g++ -std=c++11 -Iinclude src/lcs_hirschberg.c -o lcs_hirschberg
g++ -std=c++11 -Iinclude src/lcs_oblivious.c -o lcs_oblivious

# declare -a length=( 262144 524288 1048576 2097152 ) experiment2
declare -a length=( 524288 1048576 2097152 4194304 ) #experiment 2a
declare -a programs=( "lcs_classic" "lcs_hirschberg" "lcs_oblivious" )

# Set up cgroup
if [ -d "/sys/fs/cgroup/lcs_test" ]; then
    rmdir /sys/fs/cgroup/lcs_test 2>/dev/null || true
fi
mkdir -p /sys/fs/cgroup/lcs_test
echo 2147483648 > /sys/fs/cgroup/lcs_test/memory.swap.max
echo 1 > /sys/fs/cgroup/lcs_test/memory.oom.group

memory=4194304

for l in "${length[@]}" ; do
	for p in "${programs[@]}" ; do
		filename="rsrc/data-"$l".in"
		echo $memory > /sys/fs/cgroup/lcs_test/memory.max
		echo $filename $p $l $memory
		cgexec -g memory:/lcs_test ./bin/$p $l 1 < $filename #dummy run
		echo "Running 1 concurrent process"
		cgexec -g memory:/lcs_test ./bin/$p $l 1 < $filename
		cgexec -g memory:/lcs_test ./bin/$p $l 1 < $filename
		wait
		memory=$(($memory*2))
		echo $memory > /sys/fs/cgroup/lcs_test/memory.max
		echo "Running 2 concurrent processes"
		cgexec -g memory:/lcs_test ./bin/$p $l 1 < $filename &
		cgexec -g memory:/lcs_test ./bin/$p $l 1 < $filename
		wait
		cgexec -g memory:/lcs_test ./bin/$p $l 1 < $filename &
		cgexec -g memory:/lcs_test ./bin/$p $l 1 < $filename
		wait
		memory=$(($memory*2))
		echo $memory > /sys/fs/cgroup/lcs_test/memory.max
		echo "Running 4 concurrent processes"
		cgexec -g memory:/lcs_test ./bin/$p $l 1 < $filename &
		cgexec -g memory:/lcs_test ./bin/$p $l 1 < $filename &
		cgexec -g memory:/lcs_test ./bin/$p $l 1 < $filename &
		cgexec -g memory:/lcs_test ./bin/$p $l 1 < $filename
		wait
		memory=$(($memory/2))
	done
done

# Clean up
rmdir /sys/fs/cgroup/lcs_test 2>/dev/null || true