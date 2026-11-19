#!/usr/bin/env bash
# Score only after freezing the binary. Oracle rows enter the check command
# in a different process after construction has finished.
set -euo pipefail
cli=$(realpath "${1:?provide unllm_algorithm}")
out=$(realpath -m "${2:?provide fresh output directory}")
python=$(realpath "${3:?provide generated Python interpreter}")
fixtures=$(realpath "$(dirname "$0")")
mkdir "$out"
(cd "$fixtures" && sha256sum -c SHA256SUMS)
sha256sum "$cli" >"$out/binary.sha256"
while read -r task; do
    dir="$out/$task"
    mkdir "$dir"
    jq --arg k "$task" '.[$k]' "$fixtures/requests.json" >"$dir/request.json"
    "$cli" construct "$dir/session" "$dir/request.json" "$python" >"$dir/result.json"
    status=$(jq -r .status "$dir/result.json")
    if [[ $status == examples_validated ]]; then
        jq .ir "$dir/result.json" >"$dir/ir.json"
        jq --arg k "$task" --slurpfile request "$dir/request.json" '$request[0] + {examples:.[$k].cases}' "$fixtures/oracles.json" >"$dir/check.json"
        if "$cli" check "$dir/ir.json" "$dir/check.json" "$python" >"$dir/validation.json" 2>"$dir/validation.stderr"; then status=passed; else status=failed_unseen; fi
    fi
    jq -nc --arg task "$task" --arg status "$status" --slurpfile r "$dir/result.json" '{task:$task,status:$status,candidates:$r[0].unique_candidates,search_ms:$r[0].search_ms,refinements:($r[0].counterexamples|length)}' >>"$out/tasks.jsonl"
done < <(jq -r 'keys[]' "$fixtures/requests.json")
while read -r test; do
    jq '.[0]' <<<"$test" >"$out/stop-input.json"
    r=$("$cli" synthesize "$out/stop-input.json")
    jq -nc --argjson r "$r" --argjson t "$test" '{expected:$t[1],observed:$r.status,pass:($r.status==$t[1])}' >>"$out/stops.jsonl"
done < <(jq -c '.[]' "$fixtures/stops.json")
for i in 0 1 2 3 4; do
    mode=revise
    jq --argjson i "$i" '{examples:.steps[$i]}' "$fixtures/revisions.json" >"$out/change.json"
    if [[ $i == 0 ]]; then mode=construct; jq '.+{input:"ints",output:"ints"}' "$out/change.json" >"$out/first.json"; cp "$out/first.json" "$out/change.json"; fi
    "$cli" "$mode" "$out/history" "$out/change.json" "$python" >"$out/revision-$i.json"
    jq -c '{status,method,revision,edit_path,reason}' "$out/revision-$i.json" >>"$out/history.jsonl"
done
jq -s -e 'map(.status)==["examples_validated","examples_validated","examples_validated","examples_validated","inconsistent"]' "$out/history.jsonl" >/dev/null
jq -e '.revision==4' "$out/history/current.json" >/dev/null
sha256sum -c "$out/binary.sha256"
