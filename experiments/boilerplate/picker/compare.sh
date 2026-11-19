#!/usr/bin/env bash
# Compare engines on the same roles. Gold values go only to the scorer.
set -euo pipefail
if (( $# < 4 )); then echo "Usage: compare.sh BIN CATALOG CASES NEW_DIR [bm25-capped|reviewed|reviewed-guard|guarded-bge|evidence-bge|fill-gaps|review-all|OLLAMA_MODEL ...]" >&2; exit 2; fi
bin=$(realpath "$1")
catalog=$(realpath "$2")
cases=$(realpath "$3")
out=$(realpath -m "$4")
shift 4
mkdir "$out"
cp "$catalog" "$out/original.json"
jq 'reverse' "$catalog" > "$out/reversed.json"
n=$(jq length "$cases")
for method in bm25 "$@"; do
    for (( i=0; i<n; i++ )); do
        base="$out/input-$i.json"
        jq --argjson i "$i" '.[$i].roles' "$cases" > "$base"
        for order in original reversed; do
            args=(bm25 "$out/$order.json" "$base")
            case "$method" in
                bm25-capped) args[0]=bm25-capped ;;
                reviewed|reviewed-guard) args=("$method" "$out/$order.json" "$base" "${PICKER_REVIEWS:?Set PICKER_REVIEWS to the reviewed example file}") ;;
                guarded-bge|evidence-bge) args=("$method" "$out/$order.json" "$base" "${PICKER_REVIEWS:?Set PICKER_REVIEWS to the reviewed example file}" "${PICKER_BGE_DIR:?Set PICKER_BGE_DIR to the BGE model directory}") ;;
                fill-gaps|review-all) args=("$method" "$out/$order.json" "$base" "${PICKER_REVIEWS:?Set PICKER_REVIEWS to the reviewed example file}" "${PICKER_MODEL:?Set PICKER_MODEL to the Ollama model name}") ;;
                bm25) ;;
                *) args=(ollama "$out/$order.json" "$base" "$method") ;;
            esac
            start=$(date +%s%N)
            status=0
            "$bin" "${args[@]}" > "$out/answer" 2> "$out/error" || status=$?
            end=$(date +%s%N)
            jq -cn --arg engine "$method" --arg order "$order" --argjson i "$i" \
                --argjson rc "$status" --argjson ms "$(( (end - start) / 1000000 ))" \
                --slurpfile cases "$cases" --slurpfile answer "$out/answer" --rawfile err "$out/error" \
                '{engine:$engine,order:$order,case:$cases[0][$i].case,gold:$cases[0][$i].gold,
                  rc:$rc,ms:$ms,result:$answer[0],error:$err}' >> "$out/rows.jsonl"
        done
    done
done
rm "$out/answer" "$out/error"
