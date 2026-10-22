#!/usr/bin/env bash
# The big local test: run the coverage gauge over every mined real-world
# question (thousands of lines, local only, never published) and save a dated
# report. The published data/corpus.txt gauge stays the public number; this
# one keeps the hardening honest.
#
#   bigtest.sh [mined-dir] [report-dir]
export PATH=/usr/local/bin:/usr/bin:/bin
set -eu
HERE="$(cd "$(dirname "$0")/.." && pwd)"
MINED="${1:-$HOME/Downloads/unllm-corpus-sources/mined}"
REPORTS="${2:-$HOME/Downloads/unllm-corpus-sources/reports}"
mkdir -p "$REPORTS"
OUT="$REPORTS/$(date +%F-%H%M)-bigtest.txt"
cd "$HERE"
files=()
for f in wildchat-machine.txt lmsys-machine.txt se-titles.txt wildchat-top-smalltalk.txt lmsys-top-smalltalk.txt; do
    [ -r "$MINED/$f" ] && files+=("$MINED/$f")
done
{
    echo "== published gauge"
    ./build/unllm_coverage
    echo "== big local test: ${#files[@]} mined files"
    ./build/unllm_coverage "${files[@]}" --report 50
    if [ -r "$MINED/followups-ranked.tsv" ]; then
        echo "== follow-up phrasings (top 500)"
        head -500 "$MINED/followups-ranked.tsv" >"$REPORTS/.followups-top.tmp"
        ./build/unllm_coverage "$REPORTS/.followups-top.tmp"
        rm -f "$REPORTS/.followups-top.tmp"
    fi
} | tee "$OUT"
echo "saved: $OUT"
