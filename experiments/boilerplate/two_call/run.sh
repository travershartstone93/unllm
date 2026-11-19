#!/usr/bin/env bash
# Replay a prepared UTF-8 source tree in two isolated, metered model calls.
set -euo pipefail
if [[ $# != 3 ]]; then echo "Usage: $0 REQUEST.txt ASSEMBLED_DIR NEW_RUN_DIR" >&2; exit 2; fi
request=$(realpath "$1")
assembled=$(realpath "$2")
dir=$(realpath -m "$3")
if [[ ! -f "$request" || ! -d "$assembled" || "$dir" == "$assembled" || "$dir" == "$assembled/"* ]]; then
    echo "Expected a request file, a source directory and a separate output path" >&2
    exit 2
fi
source_dir=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$source_dir/../../.." && pwd)
mkdir "$dir"
mkdir "$dir/empty"
cp "$request" "$dir/request.txt"
cp -R "$assembled" "$dir/assembled"
cd "$dir"
model=${MODEL:-gpt-6.2-astra}
effort=${EFFORT:-xhigh}
jq -n --arg model "$model" --arg effort "$effort" '{model:$model,effort:$effort}' > settings.json
metered_call() {
    local phase=$1
    timeout 240 codex exec --skip-git-repo-check --ephemeral --ignore-user-config \
        -m "$model" -c "model_reasoning_effort=\"$effort\"" -s read-only \
        -C "$dir/empty" --json -o "$dir/$phase.json" - \
        < "$phase.prompt" > "$phase.events" 2> "$phase.stderr"
    jq -es 'select(([.[] | select(.type=="turn.completed")] | length)==1)
      | select(all(.[]; .type!="item.completed" or .item.type=="agent_message" or .item.type=="reasoning"))
      | map(select(.type=="turn.completed"))[0].usage' "$phase.events" > "$phase.usage"
}
read -r -a json_flags <<< "$(pkg-config --cflags --libs json-c)"
c++ -std=c++17 -Wall -Wextra -Werror -I"$repo/include" "$source_dir/replay.cpp" "${json_flags[@]}" -o replay
./replay show assembled > numbered-source.txt
cat > interpret.prompt <<'PROMPT'
Translate the request below into compact JSON with fields goal (string), inputs, outputs, behaviors, and constraints (arrays of strings). Preserve explicit requirements and conventions. This specification is the only task description the editor receives. Output JSON only. Do not use tools or write code.
PROMPT
cat request.txt >> interpret.prompt
metered_call interpret
jq -ce 'select((.goal | type=="string" and length>0) and all(.inputs,.outputs,.behaviors,.constraints; type=="array" and all(.[];type=="string")))' interpret.json > spec.json
cat > edit.prompt <<'PROMPT'
Finish the existing C++ project to satisfy the specification. Return only {"edits":[{"path":"existing file","first":1,"last":2,"text":"replacement lines"}]}. Each range is inclusive and refers to the ORIGINAL line numbers, in any order. Ranges cannot overlap. To insert, replace a nearby line and include that line in the replacement. To delete, use empty text. Omit unchanged ranges and files. The numbers and tabs prefixing source lines below are labels, not source. Use no tools. Preserve working scaffolding and write meaningful unit assertions.
PROMPT
cat spec.json >> edit.prompt
cat numbered-source.txt >> edit.prompt
metered_call edit
./replay apply assembled edit.json project
