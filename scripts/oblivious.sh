#!/bin/bash

now=$(date)
echo "$now"

if [ "$EUID" -ne 0 ]; then
    echo "Please run as root (using sudo)"
    exit 1
fi

sudo -u "$SUDO_USER" mkdir -p res/oblivious

BASE_CASE=32
MEM_LIMIT=8388608
SWAP_LIMIT=536870912
INSTANCE_COUNTS="1 2 3 4"

CGROUP_NAME="oblivious"
CGROUP_PATH="/sys/fs/cgroup/$CGROUP_NAME"
RESULTS_FILE="res/oblivious/oblivious_results.txt"
LOG_DIR="res/oblivious"

rm -f $LOG_DIR/oblivious_*.log $RESULTS_FILE

cleanup() {
    echo "Cleaning up cgroup: $CGROUP_NAME"
    pkill -9 -f 'bin/lcs_' 2>/dev/null || true
    sleep 0.3
    rmdir "$CGROUP_PATH" 2>/dev/null || true
}
trap cleanup EXIT

echo "Setting up cgroup: $CGROUP_NAME"
mkdir -p "$CGROUP_PATH"
if [ $? -ne 0 ]; then
    echo "Failed to create cgroup directory. Exiting."
    exit 1
fi

echo $MEM_LIMIT > "$CGROUP_PATH/memory.max"
echo $SWAP_LIMIT > "$CGROUP_PATH/memory.swap.max"
echo 0 > "$CGROUP_PATH/memory.oom.group" 2>/dev/null || true

# I/O is measured per-instance from each binary's /proc/self/io ("I/Os" line in its
# log) and summed across the batch: file-backed buffers => page-cache writeback, i.e.
# real per-process file I/O, not cgroup swap (pswpin/pswpout would be ~0 now).

echo "Cgroup: $CGROUP_NAME (oblivious: $(($MEM_LIMIT/1024))KiB shared by N concurrent instances; summed per-instance /proc/io file I/O per batch)" >> "$RESULTS_FILE"
echo "BASE_CASE: $BASE_CASE, Swap limit: $SWAP_LIMIT" >> "$RESULTS_FILE"
echo "" >> "$RESULTS_FILE"
echo "N, Instances, Hirschberg_IO, Oblivious_IO, Ratio" >> "$RESULTS_FILE"

# Run $inst concurrent copies of $exe at size $N, all sharing the cgroup; echo the
# total file I/O (bytes) summed across the instances' /proc/io.
run_batch() {
    local exe="$1" N="$2" inst="$3" i io total=0
    sync; echo 3 > /proc/sys/vm/drop_caches
    for i in $(seq 1 "$inst"); do
        ( echo $BASHPID > "$CGROUP_PATH/cgroup.procs"; \
          exec stdbuf -o0 nice -n 10 ./bin/"$exe" "$N" 1 $BASE_CASE ) \
            < "rsrc/data-$N.in" > "$LOG_DIR/oblivious_${exe}_${N}_${inst}_$i.log" 2>&1 &
    done
    wait
    for i in $(seq 1 "$inst"); do
        io=$(grep 'I/Os' "$LOG_DIR/oblivious_${exe}_${N}_${inst}_$i.log" | tail -1 | awk '{print $4}')
        total=$(( total + ${io:-0} ))
    done
    echo "$total"
}

for N in "${@:-131072}"; do
    for INST in $INSTANCE_COUNTS; do
        echo "Running OBLIVIOUS for N=$N with $INST instance(s)..."
        echo "  $INST x Hirschberg..."
        H=$(run_batch lcs_hirschberg "$N" "$INST")
        echo "  $INST x Oblivious..."
        O=$(run_batch lcs_oblivious "$N" "$INST")
        if [ "$O" -gt 0 ]; then
            R=$(echo "scale=6; $H / $O" | bc -l)
        else
            R=0
        fi
        echo "$N, $INST, $H, $O, $R" >> "$RESULTS_FILE"
    done
done

echo "Oblivious experiment complete. Results saved to $RESULTS_FILE."
chown -R $SUDO_USER:$SUDO_USER res/oblivious/
