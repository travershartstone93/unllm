#!/usr/bin/env bash
set -euo pipefail
dir=$(realpath "${1:?supply frozen prediction directory}")
cli=$(realpath "${2:?supply native algorithm CLI}")
py=$(realpath "${3:?supply Python target}")
here=$(cd "$(dirname "$0")" && pwd)
(cd "$dir" && sha256sum -c predictions.sha256 >/dev/null)
(cd "$here" && sha256sum -c FROZEN.sha256)
mkdir "$dir/checks"
for prediction in "$dir"/predictions/0-*.jsonl; do
  arm=$(basename "$prediction" .jsonl); arm=${arm#0-}
  jq -s 'map([.task,.status,.ir_sha256,.candidates])' "$prediction" > "$dir/checks/stability.json"
  for round in 1 2; do
    jq -s 'map([.task,.status,.ir_sha256,.candidates])' "$dir/predictions/$round-$arm.jsonl" | cmp - "$dir/checks/stability.json"
  done
  while IFS= read -r row; do
    task=$(jq -r .task <<<"$row"); result=$(jq -r .status <<<"$row")
    case "$result" in
      candidate)
        digest=$(jq -r .ir_sha256 <<<"$row"); check="$dir/checks/$task-$digest"
        if [[ ! -d "$check" ]]; then
          mkdir "$check"
          jq .ir <<<"$row" > "$check/ir.json"
          # Native check executes the IR first, then compiled C++ and Python.
          # Include construction examples as well as the reserved cases.
          jq -n --slurpfile a "$here/requests.json" --slurpfile b "$here/scoring.json" --arg id "$task" \
            '$a[0][$id] + {examples:($a[0][$id].examples+$b[0][$id].examples)}' > "$check/cases.json"
          if "$cli" check "$check/ir.json" "$check/cases.json" "$py" > "$check/report.json" 2> "$check/error"; then
            echo passed > "$check/status"
          elif rg -q '^Interpreter disagrees with example' "$check/error"; then
            echo incorrect > "$check/status"
          else
            cat "$check/error" >&2; exit 1
          fi
        fi
        result=$(cat "$check/status")
        ;;
      exhausted) ;;
      *) echo "unexpected search status $result" >&2; exit 1 ;;
    esac
    jq -nc --arg task "$task" --arg arm "$arm" --arg status "$result" --argjson row "$row" \
      '{task:$task,arm:$arm,status:$status,digest:$row.ir_sha256,proposals:$row.candidates,method:$row.method}' >> "$dir/outcomes.jsonl"
  done < "$prediction"
done
