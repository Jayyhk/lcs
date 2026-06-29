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

SWAP_LIMIT=536870912
LOW_MIB=2
HIGH_MIB=64

PIPE_FILE="/tmp/lcs_mem_signal"
PROFILE_FILE="res/adversarial/adversarial_profile.txt"
TRACE_FILE="res/adversarial/adversarial_trace.txt"
HIRSCHBERG_LOG="res/adversarial/adversarial_hirschberg.log"
OBLIVIOUS_LOG="res/adversarial/adversarial_oblivious.log"

sudo -u "$SUDO_USER" mkdir -p res/adversarial
rm -f "$HIRSCHBERG_LOG" "$OBLIVIOUS_LOG" "$RESULTS_FILE"

cleanup() {
    echo "Cleaning up..."
    pkill -9 -f bin/mem_controller 2>/dev/null || true
    pkill -9 -f bin/mem_replay 2>/dev/null || true
    pkill -9 -f 'bin/lcs_' 2>/dev/null || true
    sleep 0.3
    rmdir "$CGROUP_PATH" 2>/dev/null || true
    rm -f "$PIPE_FILE" "${PIPE_FILE}_ack" 2>/dev/null || true
}
trap cleanup EXIT

mkdir -p "$CGROUP_PATH"
echo $((HIGH_MIB * 1048576)) > "$CGROUP_PATH/memory.max"
echo $SWAP_LIMIT > "$CGROUP_PATH/memory.swap.max"
echo 0 > "$CGROUP_PATH/memory.oom.group" 2>/dev/null || true

printf '%s\n%s\n' "$LOW_MIB" "$HIGH_MIB" > "$PROFILE_FILE"

echo "Cgroup: $CGROUP_NAME (worst-case: HIGH=${HIGH_MIB}MiB during Hirschberg ALG_B scans, LOW=${LOW_MIB}MiB after; profile generated from Hirschberg and replayed for oblivious)" >> "$RESULTS_FILE"
echo "BASE_CASE: $BASE_CASE" >> "$RESULTS_FILE"
echo "" >> "$RESULTS_FILE"
echo "N, Hirschberg_IO, Oblivious_IO, Ratio" >> "$RESULTS_FILE"

for N in "${@:-131072}"; do
    echo "Running WORST-CASE ADVERSARIAL for N = $N"

    echo "  Phase A: Hirschberg (instrumented) generates + runs under the worst-case profile..."
    rm -f "$PIPE_FILE" "${PIPE_FILE}_ack"
    mkfifo "$PIPE_FILE" "${PIPE_FILE}_ack"
    sync; echo 3 > /proc/sys/vm/drop_caches
    echo $((HIGH_MIB * 1048576)) > "$CGROUP_PATH/memory.max"
    ionice -c3 nice -n19 ./bin/mem_controller "$CGROUP_PATH" "$PROFILE_FILE" "$PIPE_FILE" "$TRACE_FILE" > /dev/null 2>&1 &
    ctrl_pid=$!
    sleep 0.5
    ionice -c3 nice -n19 ./bin/lcs_hirschberg_instrumented "$N" 1 $BASE_CASE "$PIPE_FILE" \
        < "rsrc/data-$N.in" >> "$HIRSCHBERG_LOG" 2>&1 &
    lcs_pid=$!
    echo $lcs_pid > "$CGROUP_PATH/cgroup.procs"
    wait $lcs_pid 2>/dev/null || true
    kill $ctrl_pid 2>/dev/null || true
    wait $ctrl_pid 2>/dev/null || true

    echo "  Phase B: Oblivious (plain) runs under Hirschberg's replayed profile..."
    rm -f "$PIPE_FILE" "${PIPE_FILE}_ack"
    sync; echo 3 > /proc/sys/vm/drop_caches
    echo $((HIGH_MIB * 1048576)) > "$CGROUP_PATH/memory.max"
    ionice -c3 nice -n19 ./bin/mem_replay "$CGROUP_PATH" "$TRACE_FILE" > /dev/null 2>&1 &
    replay_pid=$!
    ionice -c3 nice -n19 ./bin/lcs_oblivious "$N" 1 $BASE_CASE \
        < "rsrc/data-$N.in" >> "$OBLIVIOUS_LOG" 2>&1 &
    lcs_pid=$!
    echo $lcs_pid > "$CGROUP_PATH/cgroup.procs"
    wait $lcs_pid 2>/dev/null || true
    kill $replay_pid 2>/dev/null || true
    wait $replay_pid 2>/dev/null || true

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
done

echo "Worst-case adversarial complete. Results in $RESULTS_FILE."
chown -R "$SUDO_USER:$SUDO_USER" res/adversarial/
