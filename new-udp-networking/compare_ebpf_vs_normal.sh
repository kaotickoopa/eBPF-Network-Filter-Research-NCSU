#!/usr/bin/env bash
set -euo pipefail

# compare_ebpf_vs_normal.sh
# Build and run non-eBPF and (if present inside new-udp-networking/) eBPF tests,
# parse per-packet latencies and print averages and difference.
# By design this script only searches inside the new-udp-networking folder.

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
COUNT=${1:-100}
PAYLOAD=${2:-64}
PORT=${3:-12345}
DST=${4:-127.0.0.1}

echo "compare_ebpf_vs_normal: count=$COUNT payload=$PAYLOAD port=$PORT dst=$DST"

build_target() {
    local d=$1
    if [ -f "$ROOT_DIR/$d/Makefile" ]; then
        echo "Building $d"
        (cd "$ROOT_DIR/$d" && make -j2) >/dev/null
    fi
}

run_test() {
    local name=$1; shift
    local receiver_cmd=("$@")

    local outdir=$(mktemp -d)
    local log_recv="$outdir/${name}_recv.log"
    local log_send="$outdir/${name}_send.log"

    echo "Running $name test..."
    # start receiver
    "${receiver_cmd[@]}" > "$log_recv" 2>&1 &
    local rpid=$!
    sleep 0.2

    # start sender (blocking)
    # sender is expected to print send lines but receiver prints latency lines
    # sender binary name and args will be passed as additional args after receiver
    # (we expect next args to be sender path and its args)
    echo "  sender: ${sender_cmd[*]}"
    ${sender_cmd[@]} > "$log_send" 2>&1 || true

    sleep 0.2
    kill $rpid 2>/dev/null || true
    wait $rpid 2>/dev/null || true

    # parse latencies from receiver log: look for "latency=NNN us"
    if grep -q "latency=" "$log_recv"; then
        awk -F"latency=" '/latency=/ {sub(/ us.*/,"",$2); sum+=($2+0); cnt++} END {if(cnt){printf "%f\n", sum/cnt} else {print "nan"}}' "$log_recv"
    else
        echo "nan"
    fi
}

# Build normal test
build_target "normal_networking"
build_target "streamlined_udp"

# Prepare commands
RECV_NORMAL=("$ROOT_DIR/normal_networking/udp_receiver" "$PORT")
SENDER_NORMAL=("$ROOT_DIR/normal_networking/udp_sender" "$DST" "$PORT" "$COUNT" "$PAYLOAD")

# Look for eBPF binaries inside new-udp-networking (only this directory)
EBPF_RECEIVER=""
EBPF_SENDER=""
while IFS= read -r -d '' f; do
    bn=$(basename "$f")
    case "$bn" in
        *ebpf*|*xdp*|*af_xdp*|*bpf*)
            if [[ "$bn" == *recv* || "$bn" == *receiver* || "$bn" == *xdp* ]]; then EBPF_RECEIVER="$f"; fi
            if [[ "$bn" == *send* || "$bn" == *sender* ]]; then EBPF_SENDER="$f"; fi
            ;;
    esac
done < <(find "$ROOT_DIR" -maxdepth 3 -type f -executable -print0)

if [ -n "$EBPF_RECEIVER" ] && [ -n "$EBPF_SENDER" ]; then
    echo "Found eBPF test binaries inside new-udp-networking:"
    echo "  receiver: $EBPF_RECEIVER"
    echo "  sender:   $EBPF_SENDER"
    RECV_EBPF=("$EBPF_RECEIVER" "$PORT")
    SENDER_EBPF=("$EBPF_SENDER" "$DST" "$PORT" "$COUNT" "$PAYLOAD")
else
    echo "No eBPF sender/receiver binaries found inside new-udp-networking; skipping eBPF test."
fi

# Run normal test
echo "\nRunning NORMAL (non-eBPF) test..."
out_normal=$(mktemp)
(
    "$ROOT_DIR/normal_networking/udp_receiver" "$PORT" > "$out_normal" 2>&1 &
    rpid=$!
    sleep 0.2
    "$ROOT_DIR/normal_networking/udp_sender" "$DST" "$PORT" "$COUNT" "$PAYLOAD" > /dev/null 2>&1 || true
    sleep 0.2
    kill $rpid 2>/dev/null || true
    wait $rpid 2>/dev/null || true
)
avg_normal=$(awk -F"latency=" '/latency=/ {sub(/ us.*/,"",$2); sum+=($2+0); cnt++} END {if(cnt){printf "%f", sum/cnt} else {print "nan"}}' "$out_normal")
echo "Normal avg latency (us): $avg_normal"

if [ -n "$EBPF_RECEIVER" ] && [ -n "$EBPF_SENDER" ]; then
    echo "\nRunning eBPF test..."
    out_ebpf=$(mktemp)
    (
        "${RECV_EBPF[@]}" "$PORT" > "$out_ebpf" 2>&1 &
        rpid=$!
        sleep 0.2
        "${SENDER_EBPF[@]}" "$DST" "$PORT" "$COUNT" "$PAYLOAD" > /dev/null 2>&1 || true
        sleep 0.2
        kill $rpid 2>/dev/null || true
        wait $rpid 2>/dev/null || true
    )
    avg_ebpf=$(awk -F"latency=" '/latency=/ {sub(/ us.*/,"",$2); sum+=($2+0); cnt++} END {if(cnt){printf "%f", sum/cnt} else {print "nan"}}' "$out_ebpf")
    echo "eBPF avg latency (us): $avg_ebpf"

    # compute difference
    if [[ "$avg_normal" != "nan" && "$avg_ebpf" != "nan" ]]; then
        diff=$(awk -v a="$avg_normal" -v b="$avg_ebpf" 'BEGIN{printf "%f", a - b}')
        printf "\nAverage difference (normal - eBPF) in us: %s\n" "$diff"
    fi
else
    echo "\nNo eBPF test run. To compare, place executable eBPF sender/receiver binaries inside new-udp-networking or pass them manually."
fi

echo "Done."
