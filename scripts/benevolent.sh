#!/bin/bash

now=$(date)
echo "$now"

if [ "$EUID" -ne 0 ]; then
    echo "Please run as root (using sudo)"
    exit 1
fi

CGROUP_NAME="benevolent"
CGROUP_PATH="/sys/fs/cgroup/$CGROUP_NAME"
BASE_CASE=256
RESULTS_FILE="res/benevolent/benevolent_results.txt"

SWAP_LIMIT=67108864
INIT_MAX=67108864
REPLAY_INTERVAL=0.5

PROFILE_FILE="res/benevolent/benevolent_profile.txt"
HIRSCHBERG_LOG="res/benevolent/benevolent_hirschberg.log"
OBLIVIOUS_LOG="res/benevolent/benevolent_oblivious.log"

sudo -u "$SUDO_USER" mkdir -p res/benevolent
rm -f "$HIRSCHBERG_LOG" "$OBLIVIOUS_LOG" "$RESULTS_FILE"

REPLAY_PID=""
cleanup() {
    echo "Cleaning up..."
    [ -n "$REPLAY_PID" ] && kill -9 "$REPLAY_PID" 2>/dev/null
    pkill -9 -f 'bin/lcs_hirschberg' 2>/dev/null || true
    pkill -9 -f 'bin/lcs_oblivious' 2>/dev/null || true
    sleep 0.3
    rmdir "$CGROUP_PATH" 2>/dev/null || true
}
trap cleanup EXIT

mkdir -p "$CGROUP_PATH"
echo $INIT_MAX > "$CGROUP_PATH/memory.max"
echo $SWAP_LIMIT > "$CGROUP_PATH/memory.swap.max"
echo 0 > "$CGROUP_PATH/memory.oom.group" 2>/dev/null || true

printf '2\n4\n8\n16\n32\n64\n64\n32\n16\n8\n4\n' > "$PROFILE_FILE"

echo "Cgroup: $CGROUP_NAME (time-based replay of $PROFILE_FILE every ${REPLAY_INTERVAL}s, MiB)" >> "$RESULTS_FILE"
echo "BASE_CASE: $BASE_CASE" >> "$RESULTS_FILE"
echo "" >> "$RESULTS_FILE"
echo "N, Hirschberg_IO, Oblivious_IO, Ratio" >> "$RESULTS_FILE"

replay_profile() {
    local -a prof
    mapfile -t prof < "$PROFILE_FILE"
    local n=${#prof[@]} i=0
    while :; do
        echo $(( ${prof[$i]} * 1048576 )) > "$CGROUP_PATH/memory.max" 2>/dev/null
        i=$(( (i + 1) % n ))
        sleep "$REPLAY_INTERVAL"
    done
}

run_one() {
    local exe="$1" log="$2"
    sync; echo 3 > /proc/sys/vm/drop_caches
    echo $INIT_MAX > "$CGROUP_PATH/memory.max"
    replay_profile &
    REPLAY_PID=$!
    ionice -c3 nice -n19 ./bin/"$exe" "$N" 1 $BASE_CASE < "rsrc/data-$N.in" >> "$log" 2>&1 &
    local lcs_pid=$!
    echo $lcs_pid > "$CGROUP_PATH/cgroup.procs"
    wait $lcs_pid 2>/dev/null || true
    kill -9 "$REPLAY_PID" 2>/dev/null || true
    wait "$REPLAY_PID" 2>/dev/null || true
    REPLAY_PID=""
}

for N in 131072; do
    echo "Running BENEVOLENT (time-based) for N = $N"
    echo "  Hirschberg (non-adaptive)..."
    run_one lcs_hirschberg "$HIRSCHBERG_LOG"
    echo "  Oblivious (adaptive)..."
    run_one lcs_oblivious "$OBLIVIOUS_LOG"

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

echo "Benevolent (time-based) complete. Results in $RESULTS_FILE."
chown -R "$SUDO_USER:$SUDO_USER" res/benevolent/
