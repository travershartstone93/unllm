#!/usr/bin/env bash
set -euo pipefail
fixture=$(cd "$(dirname "$0")" && pwd)
root=$(git -C "$fixture" rev-parse --show-toplevel)
work=$(mkdir -p "${1:?Specify output directory}" && cd "$1" && pwd)
export UNLLM_NO_SERVER=1 UNLLM_MODEL=off UNLLM_PYTHON_TARGET=/usr/bin/python3
cd "$root"
printf 'id\tarm\tcontract\tmeaning\tcpp\tpython\n'
row=0
while IFS=$'\t' read -r expected raw adjusted original_pick adjusted_pick present prompt || test -n "$prompt"; do
  row=$((row+1))
  for arm in original adjusted supplied; do
    case "$arm" in original) picked=$original_pick;; adjusted) picked=$adjusted_pick;; supplied) picked=$expected;; esac
    [[ "$picked" != "-" ]] || continue
    job="$work/$row-$arm"
    mkdir -p "$job"
    export UNLLM_FACT_DB="$job/facts.sqlite" UNLLM_ARTIFACTS="$job"
    jq -n --arg selected "$picked" '$selected | split(" ") | {
      input: .[0], output: .[-1], steps: (.[1:-1]|map(
        if .=="double" then {op:"map",expr:"x * 2"}
        elif .=="negative" then {op:"filter",expr:"x < 0"}
        elif .=="lower" then {op:"lowercase"}
        else {op:.} end))}' > "$job/contract.json"
    verdict=wrong; [[ "$picked" != "$expected" ]] || verdict=exact
    printf '%s\t%s\t%s\t%s' "$row" "$arm" "$picked" "$verdict"
    for lang in cpp python; do
      report="$job/$lang.json"
      build/unllm --json --code "$lang" "$(cat "$job/contract.json")" > "$report" || test -s "$report"
      status=$(jq -r .status "$report")
      if [[ "$status" != validated || "$expected" == "-" ]]; then printf '\t%s' "$status"; continue; fi
      if [[ "$lang" == cpp ]]; then cmd=("$(jq -r .job "$report")/program"); else cmd=(/usr/bin/python3 -I "$(jq -r .source "$report")"); fi
      pass=pass
      for kind in empty output; do
        if [[ "$kind" == empty ]]; then : > "$job/input"; else
          jq -r --argjson id "$row" '.input[.cases[$id-1].input][]' "$fixture/execution.json" > "$job/input"
        fi
        jq -r --argjson id "$row" --arg k "$kind" '.cases[$id-1][$k][]' "$fixture/execution.json" > "$job/expected"
        if ! "${cmd[@]}" < "$job/input" > "$job/actual" || ! cmp -s "$job/expected" "$job/actual"; then pass=fail; fi
      done
      printf '\t%s' "$pass"
    done
    printf '\n'
  done
done < <(tail -n +3 "$fixture/results.tsv")
