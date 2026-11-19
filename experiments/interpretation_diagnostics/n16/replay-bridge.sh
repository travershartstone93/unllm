#!/usr/bin/env bash
set -euo pipefail
root=$(git -C "$(dirname "$0")" rev-parse --show-toplevel)
cd "$root"
trial_dir=$(mktemp -d "$root/.state/algorithm-bridge-XXXXXX")
tasks=experiments/algorithm_ir/paired_routes/tasks.json
baseline=experiments/interpretation_diagnostics/n16/baseline.json
export UNLLM_MODEL=off UNLLM_NO_SERVER=1 UNLLM_PYTHON_TARGET=/usr/bin/python3
while read -r name; do
  job="$trial_dir/$name"
  mkdir "$job"
  export UNLLM_ARTIFACTS="$job" UNLLM_FACT_DB="$job/facts.sqlite"
  contract=$(jq -c --arg name "$name" '.[]|select(.id==$name)|.contract' "$tasks")
  reference=$(jq -c --arg name "$name" '.n11[]|select(.task==$name)|.direct_constructor' "$baseline")
  expected=$(jq -r 'if .status=="examples_validated" then "validated" else .status end' <<< "$reference")
  for lang in cpp python; do
    build/unllm --json --code "$lang" "$contract" > "$job/$lang.json" || test -s "$job/$lang.json"
    jq -e --arg status "$expected" --argjson ref "$reference" \
      '.status==$status and .unique_candidates==$ref.unique_candidates' "$job/$lang.json" >/dev/null
  done
  if [[ "$expected" == validated ]]; then
    jq -e --slurpfile py "$job/python.json" '.ir==$py[0].ir' "$job/cpp.json" >/dev/null
    jq .ir "$job/cpp.json" > "$job/program.json"
    jq --arg name "$name" '.[]|select(.id==$name)|.contract+{examples:.reserved}' "$tasks" > "$job/check.json"
    build/unllm --algorithm check "$job/program.json" "$job/check.json" /usr/bin/python3 > "$job/checked.json"
    for lang in cpp python; do
      test "$(jq -r .source_sha256 "$job/$lang.json")" = "$(jq -r --arg lang "$lang" '.[$lang]' "$job/checked.json")"
    done
  fi
  printf '%s\t%s\n' "$name" "$expected"
done < <(jq -r '.[].id' "$tasks")
printf 'Evidence in %s\n' "$trial_dir"
