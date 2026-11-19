#!/usr/bin/env bash
set -euo pipefail
dir=$(realpath "${1:?supply scored experiment directory}")
dest=$(realpath "${2:?supply result directory}")
(cd "$dir" && sha256sum -c predictions.sha256 >/dev/null)
arms=(stock none curated 512 8192 full placebo reuse-curated reuse-full)
[[ $(wc -l < "$dir/outcomes.jsonl") == $((16*${#arms[@]})) ]]
jq -ncr --slurpfile data "$dir/outcomes.jsonl" --args '
  $ARGS.positional as $arms |
  (["task"]+$arms | @tsv),
  ($data|group_by(.task)[]| . as $rows |([$rows[0].task]+[$arms[] as $a| $rows[]|select(.arm==$a)|.status])|@tsv)
' "${arms[@]}" > "$dest/paired.tsv"
for arm in "${arms[@]}"; do
  case "$arm" in
    stock|none) stats='{}'; bytes=0;;
    *)
      level=${arm#reuse-}; [[ $level != placebo ]] || level=full
      ix="$dir/levels/$level.json"
      stats=$(jq -c '{unique_documents,contributing_documents,eligible_bytes,stack_rows_scanned,signatures:(.df|length)}' "$ix")
      bytes=$(stat -c %s "$ix");;
  esac
  jq -ns --arg arm "$arm" --argjson index "$stats" --argjson bytes "$bytes" \
    --slurpfile score "$dir/outcomes.jsonl" --slurpfile a "$dir/predictions/0-$arm.jsonl" \
    --slurpfile b "$dir/predictions/1-$arm.jsonl" --slurpfile c "$dir/predictions/2-$arm.jsonl" '
    ($score|map(select(.arm==$arm))) as $rows |
    {arm:$arm,passed:($rows|map(select(.status=="passed"))|length),incorrect:($rows|map(select(.status=="incorrect"))|length),
     exhausted:($rows|map(select(.status=="exhausted"))|length), proposals:($rows|map(.proposals)|add),
     pool:$a[0].pool,setup_ms:([$a,$b,$c]|map(.[0].setup_ms)), suite_online_ms:([$a,$b,$c]|map(map(.online_ms)|add)),index_bytes:$bytes,index:$index}'
done | jq -s . > "$dest/metrics.json"
