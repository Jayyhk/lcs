#!/bin/bash

now=$(date)
echo "$now"

if [ "$EUID" -ne 0 ]; then
    echo "Please run as root (using sudo)"
    exit 1
fi

CGROUP_NAME="adversarial"
CGROUP_PATH="/sys/fs/cgroup/$CGROUP_NAME"
BASE_CASE=256
RESULTS_FILE="res/adversarial/adversarial_results.txt"

SWAP_LIMIT=67108864
INIT_MAX=67108864

PIPE_FILE="/tmp/lcs_mem_signal"
PROFILE_FILE="res/adversarial/adversarial_profile.txt"

HIRSCHBERG_LOG="res/adversarial/adversarial_hirschberg.log"
OBLIVIOUS_LOG="res/adversarial/adversarial_oblivious.log"

sudo -u "$SUDO_USER" mkdir -p res/adversarial
rm -f "$HIRSCHBERG_LOG" "$OBLIVIOUS_LOG" "$RESULTS_FILE"

cleanup() {
    echo "Cleaning up..."
    pkill -9 -f bin/mem_controller 2>/dev/null || true
    pkill -9 -f 'bin/lcs_.*_instrumented' 2>/dev/null || true
    sleep 0.5
    rmdir "$CGROUP_PATH" 2>/dev/null || true
    rm -f "$PIPE_FILE" "${PIPE_FILE}_ack" 2>/dev/null || true
}
trap cleanup EXIT

echo "Setting up cgroup: $CGROUP_NAME"
mkdir -p "$CGROUP_PATH"
echo $INIT_MAX > "$CGROUP_PATH/memory.max"
echo $SWAP_LIMIT > "$CGROUP_PATH/memory.swap.max"
echo 0 > "$CGROUP_PATH/memory.oom.group" 2>/dev/null || true

echo "64" > "$PROFILE_FILE"
echo "2" >> "$PROFILE_FILE"

echo "Cgroup: $CGROUP_NAME  (memory.max driven by mem_controller: 64 MiB <-> 2 MiB)" >> "$RESULTS_FILE"
echo "BASE_CASE: $BASE_CASE" >> "$RESULTS_FILE"
echo "" >> "$RESULTS_FILE"
echo "N, Hirschberg_IO, Oblivious_IO, Ratio" >> "$RESULTS_FILE"

run_one() {
    local exe="$1" log="$2"
    rm -f "$PIPE_FILE" "${PIPE_FILE}_ack"
    mkfifo "$PIPE_FILE" "${PIPE_FILE}_ack"
    sync; echo 3 > /proc/sys/vm/drop_caches
    echo $INIT_MAX > "$CGROUP_PATH/memory.max"

    stdbuf -o0 ./bin/mem_controller "$CGROUP_PATH" "$PROFILE_FILE" "$PIPE_FILE" \
        > "res/adversarial/controller_${exe}.log" 2>&1 &
    local controller_pid=$!
    sleep 0.5

    stdbuf -o0 ./bin/"$exe" "$N" 1 $BASE_CASE "$PIPE_FILE" \
        < "rsrc/data-$N.in" >> "$log" 2>&1 &
    local lcs_pid=$!
    echo $lcs_pid > "$CGROUP_PATH/cgroup.procs"

    wait $lcs_pid 2>/dev/null || true
    kill $controller_pid 2>/dev/null || true
    wait $controller_pid 2>/dev/null || true
}

for N in 131072; do
    echo "Running ADVERSARIAL test for N = $N"

    echo "  Running Hirschberg (non-adaptive)..."
    run_one lcs_hirschberg_instrumented "$HIRSCHBERG_LOG"

    echo "  Running Oblivious (adaptive)..."
    run_one lcs_oblivious_instrumented "$OBLIVIOUS_LOG"

    LCS_HIRSCHBERG_IO=$(grep 'I/Os' "$HIRSCHBERG_LOG" | tail -1 | awk '{print $4}')
    LCS_HIRSCHBERG_IO=${LCS_HIRSCHBERG_IO:-0}
    LCS_OBLIVIOUS_IO=$(grep 'I/Os' "$OBLIVIOUS_LOG" | tail -1 | awk '{print $4}')
    LCS_OBLIVIOUS_IO=${LCS_OBLIVIOUS_IO:-0}

    if [ "$LCS_OBLIVIOUS_IO" -gt 0 ]; then
        RESULT=$(echo "scale=6; $LCS_HIRSCHBERG_IO / $LCS_OBLIVIOUS_IO" | bc -l)
        echo "$N, $LCS_HIRSCHBERG_IO, $LCS_OBLIVIOUS_IO, $RESULT" >> "$RESULTS_FILE"
    else
        echo "$N, $LCS_HIRSCHBERG_IO, $LCS_OBLIVIOUS_IO, 0" >> "$RESULTS_FILE"
    fi
    rm -f "$PIPE_FILE" "${PIPE_FILE}_ack"
done

echo "Adversarial experiment complete. Results saved to $RESULTS_FILE."
chown -R "$SUDO_USER:$SUDO_USER" res/adversarial/
