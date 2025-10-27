#!/bin/bash
set -ex
g++ -std=c++11 -Iinclude src/lcs_classic.c -o lcs_classic
g++ -std=c++11 -Iinclude src/lcs_hirschberg.c -o lcs_hirschberg
g++ -std=c++11 -Iinclude src/lcs_oblivious.c -o lcs_oblivious

declare -a length=( 524288 ) #( 65536 131072 262144 524288 )
declare -a programs=( "lcs_classic" "lcs_hirschberg" "lcs_oblivious" )

# Set up cgroup
if [ -d "/sys/fs/cgroup/lcs_test" ]; then
    rmdir /sys/fs/cgroup/lcs_test 2>/dev/null || true
fi
mkdir -p /sys/fs/cgroup/lcs_test
echo 4194304 > /sys/fs/cgroup/lcs_test/memory.max
echo 2147483648 > /sys/fs/cgroup/lcs_test/memory.swap.max
echo 1 > /sys/fs/cgroup/lcs_test/memory.oom.group

for l in "${length[@]}" ; do
	for p in "${programs[@]}" ; do
		filename="rsrc/data-"$l".in"
		echo $filename $p $l
		cgexec -g memory:/lcs_test ./bin/$p $l 1 < $filename #dummy run
		echo "Running 1 concurrent process"
		cgexec -g memory:/lcs_test ./bin/$p $l 1 < $filename
		cgexec -g memory:/lcs_test ./bin/$p $l 1 < $filename
		wait
		cgexec -g memory:/lcs_test ./bin/$p $l 1 < $filename
		cgexec -g memory:/lcs_test ./bin/$p $l 1 < $filename
		echo "Running 2 concurrent processes"
		cgexec -g memory:/lcs_test ./bin/$p $l 1 < $filename &
		cgexec -g memory:/lcs_test ./bin/$p $l 1 < $filename
		wait
		cgexec -g memory:/lcs_test ./bin/$p $l 1 < $filename &
		cgexec -g memory:/lcs_test ./bin/$p $l 1 < $filename
		wait
		echo "Running 4 concurrent processes"
		cgexec -g memory:/lcs_test ./bin/$p $l 1 < $filename &
		cgexec -g memory:/lcs_test ./bin/$p $l 1 < $filename &
		cgexec -g memory:/lcs_test ./bin/$p $l 1 < $filename &
		cgexec -g memory:/lcs_test ./bin/$p $l 1 < $filename
		wait
	done
done

# Clean up
rmdir /sys/fs/cgroup/lcs_test 2>/dev/null || true