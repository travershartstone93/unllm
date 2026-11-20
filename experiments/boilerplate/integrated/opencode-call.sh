#!/bin/bash
# Invoke a hosted model for one pipeline stage. Keep every provider attempt in order.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
call=$(realpath -e "${1:?Supply the stage call directory}")
command -v opencode >/dev/null
mkdir -p "$call/empty" "$call/attempts"
cd "$call/empty"
prompt=$(<"$call/prompt.txt")
models=(opencode/big-pickle opencode/iceman-free)
quota_limited=false
for ((index=0; index<${#models[@]}; index++)); do
    model=${models[index]}
    attempt="attempts/$((index + 1))"
    mkdir "$call/$attempt"
    printf '%s\n' "$model" > "$call/$attempt/model.txt"
    for file in model.txt events.jsonl stderr.txt; do
        ln -sfn "$attempt/$file" "$call/$file"
    done
    rc=0
    if [[ $model == opencode/* ]]; then
        OPENCODE_CONFIG_CONTENT='{"permission":{"*":"deny"}}' timeout 480 opencode run \
            --model "$model" --format json "$prompt" > "$call/events.jsonl" 2> "$call/stderr.txt" || rc=$?
        if jq -es 'any(.[]; .type=="error" and
            ((.error.status // .error.data.statusCode | tostring)=="429" or .error.type=="provider.quota"))' \
            "$call/events.jsonl" >/dev/null 2>&1; then quota_limited=true; fi
    else
        bash "$here/anonymous-call.sh" "$model" "$call" "$call/$attempt" \
            > "$call/events.jsonl" 2> "$call/stderr.txt" || rc=$?
    fi
    printf '%s\n' "$rc" > "$call/$attempt/exit-code.txt"

    if jq -es 'any(.[]; (.type // "") | startswith("tool"))' "$call/events.jsonl" >/dev/null 2>&1; then
        echo "Unexpected OpenCode tool action from $model" >&2
        exit 1
    fi
    if (( rc == 0 )) && jq -es -L "$here" --arg model "$model" --arg stage "${call##*/}" '
        include "interpret-shape";
        select(all(.[]; .type != "error")) | [.[] | select(.type=="text") | .part.text] | join("") |
        sub("^\\s*```(json)?\\s*";"") | sub("\\s*```\\s*$";"") | fromjson | select(type=="object") |
        select(if ($model|startswith("opencode/")) then true
          elif $stage=="interpret" then
            (.clarify|type=="string" and length>0) or valid_cli_roles or
            (.artifact.kind=="header" and (.artifact.filename|type=="string") and
             (.artifact.methods|type=="object") and (.responsibilities|type=="array" and length>0))
          elif $stage=="edit" then (.edits|type=="array") and (.add_packages|type=="array")
          elif $stage=="responsibilities" then (.responsibilities|type=="array")
          else true end)
    ' "$call/events.jsonl" > "$call/$attempt/response.json" 2>> "$call/stderr.txt"; then
        ln -s "$attempt/response.json" "$call/response.json"
        exit 0
    fi

    if (( index == 1 )); then
        # Ask for the list only after both preferred models have failed.
        if timeout 30 opencode models > "$call/available-models.txt" 2> "$call/model-list.stderr.txt"; then
            mapfile -t others < <(awk '/^opencode\/[A-Za-z0-9._-]+-free$/ && $0 != "opencode/iceman-free" && !seen[$0]++' "$call/available-models.txt")
            models+=("${others[@]}")
        fi
    fi
    if [[ $model == opencode/* && $quota_limited == true ]] && (( index + 1 == ${#models[@]} )); then
        models+=(ovhcloud/gpt-oss-120b aihorde/Qwen/Qwen3.8-Flash-Next)
    fi
done
echo "No usable response from ${#models[@]} hosted model attempts. See $call/attempts." >&2
exit 1
