#!/usr/bin/env bash
set -eu
root=$(cd "$(dirname "$0")/../../.." && pwd)
out=${1:?supply a new output directory}
mkdir "$out"
out=$(realpath "$out")
data=${UNLLM_DATA:-${XDG_DATA_HOME:-$HOME/.local/share}/unllm}
packs=${UNLLM_PACKS:-$data/packs}
for pack in wikipedia-en se-cooking; do
  scope="$out/$pack"
  mkdir -p "$scope/packs"
  ln -s "$(realpath "$packs/$pack")" "$scope/packs/$pack"
  sock="$scope/resident.sock"
  # Use the ranked retrieval path for the scoped pack. Keep learned state isolated.
  export UNLLM_DATA="$scope" UNLLM_PACKS="$scope/packs" UNLLM_MODEL=off UNLLM_SOCKET="$sock"
  unset UNLLM_NO_SERVER UNLLM_MEANING
  "$root/build/unllm" --engine legacy --serve >"$scope/server.log" 2>&1 &
  pid=$!
  trap 'kill "$pid" 2>/dev/null || true' EXIT
  for ((i=0;i<200;i++)); do [[ -S "$sock" ]] && break; sleep .05; done
  [[ -S "$sock" ]]
  index=0
  while IFS=$'\t' read -r source question; do
    [[ $source == "$pack" ]] || continue
    ((index+=1))
    for mode in fresh resident_first resident_repeat; do
      flags=()
      [[ $mode != fresh ]] || flags=(UNLLM_NO_SERVER=1)
      TIMEFORMAT=%3R
      { time env "${flags[@]}" "$root/build/unllm" --engine legacy --branch --brief --passages 2 "$question" >"$scope/$index-$mode.txt" 2>"$scope/$index-$mode.stderr"; } 2>"$scope/$index-$mode.seconds"
    done
  done < "$root/experiments/corpus_storage/s0/questions.tsv"
  kill "$pid"
  wait "$pid" || true
  trap - EXIT
done
