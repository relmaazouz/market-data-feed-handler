#!/usr/bin/env bash
#
# Build and run the ITCH benchmark or feed handler, with an explicit,
# clearly-labelled allocator choice.
#
#   ./run.sh                      # itch_bench, system allocator (your real baseline)
#   ./run.sh --jemalloc           # itch_bench, jemalloc (diagnostic only)
#   ./run.sh feed                 # itch_feed, system allocator
#   ./run.sh feed --jemalloc DATA # itch_feed under jemalloc on a specific file
#
# The allocator is ALWAYS printed. jemalloc is opt-in and errors if missing,
# so a jemalloc run can never be silently mistaken for the default baseline.
set -euo pipefail

DATA="data/20190130.BX_ITCH_50"
TARGET="bench"        # bench | feed
USE_JEMALLOC=0

usage() {
    sed -n '3,12p' "$0" | sed 's/^# \{0,1\}//'
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        bench)          TARGET="bench";      shift;;
        feed)           TARGET="feed";       shift;;
        -j|--jemalloc)  USE_JEMALLOC=1;      shift;;
        -h|--help)      usage; exit 0;;
        -*)             echo "unknown option: $1" >&2; usage; exit 2;;
        *)              DATA="$1";           shift;;   # positional = data file
    esac
done

case "$TARGET" in
    bench) BIN=itch_bench;;
    feed)  BIN=itch_feed;;
esac

# Incremental build (make only recompiles if sources changed).
make "$BIN" >/dev/null

if [[ ! -f "$DATA" ]]; then
    echo "ERROR: data file not found: $DATA" >&2
    echo "       download from https://emi.nasdaq.com/ITCH/ and gunzip into data/" >&2
    exit 1
fi

PRELOAD=""
if [[ "$USE_JEMALLOC" -eq 1 ]]; then
    JEM=$(ldconfig -p 2>/dev/null | awk '/libjemalloc\.so\.2/ {print $NF; exit}')
    [[ -z "$JEM" ]] && JEM=$(find /usr/lib /usr/local/lib -name 'libjemalloc.so.2' 2>/dev/null | head -1)
    if [[ -z "$JEM" ]]; then
        echo "ERROR: --jemalloc requested but libjemalloc.so.2 was not found." >&2
        echo "       install it:  sudo apt-get install -y libjemalloc2" >&2
        exit 1
    fi
    PRELOAD="$JEM"
fi

echo "allocator : $([[ -n "$PRELOAD" ]] && echo "jemalloc  ($PRELOAD)" || echo "system (default)")"
echo "binary    : ./$BIN"
echo "data      : $DATA"
echo "---"

if [[ -n "$PRELOAD" ]]; then
    LD_PRELOAD="$PRELOAD" "./$BIN" "$DATA"
else
    "./$BIN" "$DATA"
fi
