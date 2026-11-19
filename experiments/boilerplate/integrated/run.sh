#!/bin/bash
# Live interpreter -> deterministic picks -> Qwen gap picks -> composition -> checks -> optional edit.
set -euo pipefail
if (( $# != 3 )); then echo "Usage: run.sh REQUEST_FILE NEW_RUN_DIR ACCEPTANCE_RUNNER" >&2; exit 2; fi
here=$(cd -- "$(dirname -- "$0")" && pwd)
request=$(realpath "$1"); suite=$(realpath "$3")
test -f "$request" && test -x "$suite"
mkdir -- "$2"
run=$(realpath "$2")
root=${CORPUS_ROOT:-/run/media/whisper/UNLLM_CORPUS/unllm}
b16="$root/experiments/boilerplate-B16"; b19="$root/experiments/boilerplate-B19"
: "${PICKER:?Set PICKER to the rebuilt brick-picker binary}"
annotator=${ANNOTATOR:-$b16/replay-tools/brick-requirements}
: "${UDPIPE_MODEL:?Set UDPIPE_MODEL to ewt.udpipe}"
encoder=${BGE_DIR:-$HOME/.local/share/unllm-models/bge-small-en-v1.5}
cd "$run"
status() { jq -cn --arg stage "$1" --arg detail "$2" '{stage:$stage,detail:$detail}' >> steps.jsonl; }
call_model() {
    local name=$1
    mkdir -p "calls/$name/empty"
    (cd "calls/$name/empty"; OPENCODE_CONFIG_CONTENT='{"permission":{"*":"deny"}}' timeout 480 opencode run --model opencode/big-pickle --format json "$(cat ../prompt.txt)") > "calls/$name/events.jsonl" 2> "calls/$name/stderr.txt"
    jq -es 'if any(.[]; (.type|startswith("tool"))) then error("unexpected tool action") else
      [.[] | select(.type=="text") | .part.text] | join("") | sub("^\\s*```(json)?\\s*";"") | sub("\\s*```\\s*$";"") | fromjson end' "calls/$name/events.jsonl" > "calls/$name/response.json"
}
cp "$request" request.txt
mkdir -p calls/interpret
{ cat "$here/interpret.txt"; printf '\nUser request:\n'; cat request.txt; } > calls/interpret/prompt.txt
status interpret started
call_model interpret
spec=calls/interpret/response.json
if jq -e '.clarify | type=="string"' "$spec" >/dev/null; then status clarify "$(jq -r .clarify "$spec")"; exit 2; fi
jq -e 'type=="object" and (.input_type|type=="string") and (.roles|type=="array" and length>0) and
  (.roles|map(.id)|unique|length)==(.roles|length) and all(.roles[];
  (.id|type=="string" and test("^[a-zA-Z0-9_-]+$")) and (.text|type=="string" and length>0) and
  (.output_type|type=="string") and (.multiple|type=="boolean") and
  (.content|IN("whole","text","embedded_images","pages","metadata","audio","video","other")))' "$spec" >/dev/null
jq -c '[.roles[]|{id,text}]' "$spec" | "$annotator" "$b16/replay-tools/requirement-terms.json" "$UDPIPE_MODEL" > annotated.json
status deterministic_picker started
"$PICKER" evidence-bge "$b16/catalog.json" annotated.json "$b16/reviews.json" "$encoder" > proposals.json
jq --slurpfile p proposals.json '[.[]|select(.id as $id|$p[0].bindings[$id]==null)]' annotated.json > unfilled.json
if jq -e 'length>0' unfilled.json >/dev/null; then
    status qwen_picker started
    "$PICKER" ollama "$b16/catalog.json" unfilled.json qwen3:8b > qwen.json
    # Merge exactly the requested gap IDs; existing assignments cannot be overwritten.
    jq -en --slurpfile p proposals.json --slurpfile q qwen.json --slurpfile gaps unfilled.json '
        if ($q[0].bindings|keys) == ($gaps[0]|map(.id)|sort)
        then $p[0].bindings + $q[0].bindings else error("unexpected Qwen role IDs") end' > bindings.json
else
    status qwen_picker skipped
    jq '.bindings' proposals.json > bindings.json
fi
"$PICKER" resolve "$b16/catalog.json" annotated.json bindings.json > selection.json
jq --slurpfile proposal proposals.json --slurpfile selection selection.json --slurpfile adapters "$here/adapters.json" -f "$here/route.jq" "$spec" > route.json
status assemble started
cp -R "$b19/bricks" .
cp "$b19/library.txt" library.txt
contract() { jq -r '"args "+(.args|join(" ")),"want "+(.goals|join(" "))'; }
# Check whether each selected high-level brick can obtain its smaller providers.
# Preserve failed goals and their CLI slots for the optional editor.
mkdir probes
while IFS= read -r slot; do
    jq --argjson i "$slot" '.goals=[.goals[$i]]' route.json | contract > "probes/$slot.txt"
    if ! "$b19/emit" library.txt "probes/$slot.txt" "probes/$slot" 2>"probes/$slot.log"; then
        jq --argjson i "$slot" --rawfile why "probes/$slot.log" \
           '.rows[$i] += {bound:false,reason:$why} | .goals[$i]="missing_\($i)"' route.json > route.new
        mv route.new route.json
    fi
done < <(jq -r '.rows|to_entries[]|select(.value.bound)|.key' route.json)
contract < route.json > contract.txt
gaps=$(jq '[.rows[]|select(.bound==false)]|length' route.json)
if (( gaps )); then
    printf '#pragma once\n#include <stdexcept>\n' > bricks/gaps.hpp
    while IFS= read -r slot; do
        echo "inline void missing_$slot(const char*, const char*) { throw std::runtime_error(\"Missing role $slot\"); }" >> bricks/gaps.hpp
        jq -r --argjson i "$slot" '"missing_\($i) missing_\($i) gaps.hpp - : "+.args[0]+" "+.args[$i+1]' route.json >> library.txt
    done < <(jq -r '.rows|to_entries[]|select(.value.bound==false)|.key' route.json)
fi
"$b19/emit" library.txt contract.txt project
build_program() {
    local -a packages flags
    mapfile -t packages < project/packages.txt
    local result=""
    if (( ${#packages[@]} )); then result=$(pkg-config --cflags --libs "${packages[@]}") || return 1; fi
    read -r -a flags <<< "$result"
    rm -f project/converter
    c++ -std=c++17 -O2 -Ibricks project/converter.cpp -o project/converter "${flags[@]}"
}
verify() {
    local tag=$1
    mkdir "check-$tag"
    local rc=0
    "$suite" "$run/project/converter" "$run/check-$tag" > "$tag-checks.json" || rc=$?
    # Failed assertions are actionable. A broken test harness must not authorize delivery.
    jq -es 'length==1 and (.[0].checks|type=="object" and length>0) and
            all(.[0].checks[];(.pass_|type)=="boolean")' "$tag-checks.json" >/dev/null ||
        { status harness_error "$tag"; exit 2; }
    if (( rc != 0 )) && passes "$tag-checks.json"; then status harness_error "$tag"; exit 2; fi
}
passes() {
    jq -e '(.checks|type=="object" and length>0) and all(.checks[];.pass_==true)' "$1" >/dev/null
}
accept() {
    jq -n --arg stage "$1" --argjson used "$2" '{passed:true,completed_at:$stage,stage5_used:$used}' > result.json
    status accepted "$1"
}
status build_test started
if build_program initial > initial-build.log 2>&1; then
    verify initial
    if (( gaps == 0 )) && passes initial-checks.json; then accept deterministic false; exit 0; fi
else
    status compiler_fixit attempted
    mapfile -t packages < project/packages.txt
    cflags=""
    if (( ${#packages[@]} )); then cflags=$(pkg-config --cflags "${packages[@]}") || cflags=""; fi
    read -r -a flags <<< "$cflags"
    if "$b19/compiler-repair" project/converter.cpp project/fixed.cpp -std=c++17 "-resource-dir=$(clang -print-resource-dir)" -Ibricks "${flags[@]}" 2> fixit.log; then
        cp project/converter.cpp before-fixit.cpp
        mv project/fixed.cpp project/converter.cpp
        if build_program fixit > fixit-build.log 2>&1; then
            verify fixit
            if (( gaps == 0 )) && passes fixit-checks.json; then accept compiler_fixit false; exit 0; fi
        fi
    fi
fi
status stage5 requested
mkdir -p calls/edit
"$here/edit-context.sh" "$run" > calls/edit/prompt.txt
call_model edit
jq --slurpfile files edit-files.json -f "$here/patch.jq" calls/edit/response.json > edited-files.json
while IFS= read -r file; do jq -r --arg f "$file" '.[$f][]' edited-files.json > "$file"; done < <(jq -r 'keys[]' edited-files.json)
{ cat project/packages.txt; jq -r '.add_packages[]' calls/edit/response.json; } | sort -u > packages.new
mv packages.new project/packages.txt
if build_program edit > edit-build.log 2>&1; then
    verify edit
    if passes edit-checks.json; then accept model_edit true; exit 0; fi
fi
jq -n '{passed:false,completed_at:"after_edit",stage5_used:true}' > result.json
status failed after_edit
exit 1
