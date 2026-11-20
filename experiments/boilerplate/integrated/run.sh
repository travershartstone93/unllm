#!/bin/bash
# Interpret, check whole Arch programs, then pick and assemble bricks for remaining requests.
set -euo pipefail
if (( $# != 3 )); then echo "Usage: run.sh REQUEST_FILE NEW_RUN_DIR ACCEPTANCE_RUNNER" >&2; exit 2; fi
here=$(cd -- "$(dirname -- "$0")" && pwd)
request=$(realpath "$1"); suite=$(realpath "$3")
interpreted_requirements=
if [[ -n ${BRICK_REQUIREMENTS_FILE:-} ]]; then interpreted_requirements=$(realpath -e "$BRICK_REQUIREMENTS_FILE"); fi
learning_client=; learning_dest=
if [[ -n ${BRICK_LEARN_CLIENT:-} || -n ${BRICK_LEARN_DEST:-} ]]; then
    learning_client=$(realpath -e "${BRICK_LEARN_CLIENT:?Set an independent C++ client}")
    learning_dest=$(realpath -m "${BRICK_LEARN_DEST:?Set the new bundle path}")
fi
test -f "$request" && test -x "$suite"
mkdir -- "$2"
run=$(realpath "$2")
root=${CORPUS_ROOT:-/run/media/whisper/UNLLM_CORPUS/unllm}
if [[ ! -v BRICK_COMPONENT_CATALOG && -f $root/boilerplate/active/providers/contracts.json ]]; then
    export BRICK_COMPONENT_CATALOG="$root/boilerplate/active"
elif [[ ${BRICK_COMPONENT_CATALOG:-} == off ]]; then
    unset BRICK_COMPONENT_CATALOG
fi
b16="$root/experiments/boilerplate-B16"; b19="$root/experiments/boilerplate-B19"
cd "$run"
status() { jq -cn --arg stage "$1" --arg detail "$2" '{stage:$stage,detail:$detail}' >> steps.jsonl; }
call_model() { bash "$here/opencode-call.sh" "calls/$1"; }
# A provider bundle needs one interpretation shared by lookup, picking and planning.
combined=false
if [[ -f ${BRICK_COMPONENT_CATALOG:-}/providers/contracts.json && -z $interpreted_requirements ]]; then combined=true; fi
cp "$request" request.txt
mkdir -p calls/interpret
{ if $combined; then
      "$here/provider-prompt.sh" "$BRICK_COMPONENT_CATALOG"
  else cat "$here/interpret.txt"; fi
  printf '\nUser request:\n'; cat request.txt; } > calls/interpret/prompt.txt
status interpret started
if ! call_model interpret; then
    status failed interpret_response_failed
    jq -n '{passed:false,completed_at:"interpret_response_failed",stage5_used:false}' > result.json
    exit 1
fi
spec=calls/interpret/response.json
invalid_interpretation() {
    status failed invalid_interpretation
    jq -n '{passed:false,completed_at:"invalid_interpretation",stage5_used:false}' > result.json
    exit 2
}
if jq -e '.clarify | type=="string"' "$spec" >/dev/null; then status clarify "$(jq -r .clarify "$spec")"; exit 2; fi
verify() {
    local tag=$1 program=${2:-"$run/project/converter"}
    mkdir "check-$tag"
    local rc=0
    "$suite" "$program" "$run/check-$tag" > "$tag-checks.json" || rc=$?
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
if jq -e '.artifact.kind=="header"' "$spec" >/dev/null; then
    status header_assembly started
    if [[ ! -f ${BRICK_COMPONENT_CATALOG:-}/providers/library.json ]] ||
       ! "$here/header-stage.sh" "$BRICK_COMPONENT_CATALOG" > header-build.log 2>&1; then
        status failed unsupported_header
        jq -n '{passed:false,completed_at:"unsupported_header",stage5_used:false}' > result.json
        exit 2
    fi
    header=$(jq -r '.filename' components/header.json)
    verify initial "$run/project/$header"
    if ! passes initial-checks.json; then
        status failed header_checks
        jq -n '{passed:false,completed_at:"header_checks",stage5_used:false}' > result.json
        exit 1
    fi
    if jq -e '.uncovered|length>0' components/header.json >/dev/null; then
        jq '{passed:false,completed_at:"requires_verification",candidate_stage:"deterministic_header",
             stage5_used:false,checks_passed:true,unresolved:.uncovered}' components/header.json > result.json
        status requires_verification header
        exit 2
    fi
    accept deterministic_header false
    exit 0
fi
jq -e -L "$here" 'include "interpret-shape"; valid_cli_roles' "$spec" >/dev/null || invalid_interpretation
catalog=${ARCH_PROGRAM_CATALOG:-"$root/arch-programs/catalog"}
if [[ $catalog == off ]]; then
    status arch_lookup disabled
elif [[ ! -x $catalog/catalog || ! -d $catalog/index ]]; then
    status arch_lookup catalog_unavailable
else
    status arch_lookup started
    mkdir arch
    if "$here/../arch/discover.sh" "$catalog" "$spec" > arch/candidates.json 2> arch/search.log; then
        mapfile -t candidates < <(jq -c '.[] | select(.disposition=="check_provenance")' arch/candidates.json)
        for candidate in "${candidates[@]}"; do
            dir="arch/$(jq -r .repo <<< "$candidate")-$(jq -r .package <<< "$candidate")"
            mkdir "$dir"
            printf '%s\n' "$candidate" > "$dir/candidate.json"
            rc=0
            "$here/../arch/prepare.sh" "$dir/candidate.json" "$run/$dir" > "$dir/prepare.log" 2>&1 || rc=$?
            if (( rc == 10 )); then
                if [[ ! -t 0 && ${ARCH_INSTALL_TERMINAL:-} != alacritty ]]; then
                    status arch_install_skipped "$dir"
                    continue
                fi
                status arch_install_offered "$dir"
                rc=0
                "$here/../arch/offer-install.sh" "$dir/candidate.json" || rc=$?
                if (( rc == 10 )); then status arch_build_selected "$dir"; break; fi
                if (( rc != 0 )); then status arch_install_failed "$dir"; break; fi
                status arch_install_completed "$dir"
                "$here/../arch/prepare.sh" "$dir/candidate.json" "$run/$dir" >> "$dir/prepare.log" 2>&1 || rc=$?
            fi
            if (( rc != 0 )); then
                status arch_candidate_unavailable "$dir"
                continue
            fi
            tag="arch-$(basename "$dir")"
            verify "$tag" "$run/$dir/converter" < /dev/null
            if passes "$tag-checks.json"; then
                mkdir project
                cp "$dir"/{converter.cpp,converter,dependency.json} project/
                accept official_arch false
                exit 0
            fi
            status arch_candidate_failed "$dir"
        done
        status arch_lookup no_verified_program
    else
        status arch_lookup search_unavailable
    fi
fi
: "${PICKER:?Set PICKER to the rebuilt brick-picker binary}"
annotator=${ANNOTATOR:-$b16/replay-tools/brick-requirements}
: "${UDPIPE_MODEL:?Set UDPIPE_MODEL to ewt.udpipe}"
encoder=${BGE_DIR:-$HOME/.local/share/unllm-models/bge-small-en-v1.5}
jq -c '[.roles[]|{id,text}]' "$spec" | "$annotator" "$b16/replay-tools/requirement-terms.json" "$UDPIPE_MODEL" > annotated.json
status deterministic_picker started
"$PICKER" evidence-bge "$b16/catalog.json" annotated.json "$b16/reviews.json" "$encoder" > proposals.json
jq --slurpfile p proposals.json '[.[]|select(.id as $id|$p[0].bindings[$id]==null)]' annotated.json > unfilled.json
jq '.bindings' proposals.json > bindings.json
"$PICKER" resolve "$b16/catalog.json" annotated.json bindings.json > selection.json
jq --slurpfile proposal proposals.json --slurpfile selection selection.json --slurpfile adapters "$here/adapters.json" -f "$here/route.jq" "$spec" > route.json
cp -R "$b19/bricks" .
cp "$b19/library.txt" library.txt
if [[ -n ${BRICK_COMPONENT_CATALOG:-} ]] && jq -e 'any(.rows[]; .bound|not)' route.json >/dev/null; then
    status component_contract started
    mkdir -p calls/responsibilities
    response=calls/responsibilities/response.json
    if $combined; then
        jq -e 'if .requirements==null then .requirements=(.roles|map({key:.id,value:.requirement})|from_entries) else . end |
              if (.requirements|type)=="object" and all(.requirements[];type=="object") and
              (.requirements|keys)==(.roles|map(.id)|sort) and (.responsibilities|type)=="array"
              then {requirements,responsibilities} else error("incomplete combined interpretation") end' \
            "$spec" > "$response" || invalid_interpretation
        status component_contract from_interpretation
    elif [[ -n $interpreted_requirements && -f $BRICK_COMPONENT_CATALOG/providers/contracts.json ]]; then
        jq -e 'select(type=="object" and (.requirements|type=="object") and (.responsibilities|type=="array"))' \
            "$interpreted_requirements" > "$response"
        status component_contract supplied
    else
        { cat "$here/wiring.txt"; cat request.txt; jq -c '{input_type,outputs:[.roles[]|{id,output_type,multiple,content}]}' "$spec"; } > calls/responsibilities/prompt.txt
        if ! call_model responsibilities; then
            status component_contract unavailable
            echo '{}' > "$response"
        fi
    fi
    jq 'if type=="object" and (.wiring|type=="object") then .wiring else {} end' "$response" > wiring.json
    jq 'if type=="object" and (.requirements|type=="object") then .requirements else {} end' "$response" > requirements.json
    if [[ -n ${BRICK_COMPONENT_ROLES:-} ]]; then
        cp "$BRICK_COMPONENT_ROLES" component-roles.json
    elif jq -ec 'select(type=="object") | .responsibilities | select(type=="array" and length>0 and
             all(.[]; (.id|type=="string" and length>0) and (.text|type=="string" and length>0))
             and (map(.id)|unique|length)==length)' "$response" > responsibilities.json; then
        "$annotator" "$b16/replay-tools/requirement-terms.json" "$UDPIPE_MODEL" < responsibilities.json > component-roles.json
    else
        cp annotated.json component-roles.json
    fi
    status component_picker "${BRICK_COMPONENT_MODE:-trained}"
    BGE_DIR="$encoder" "$here/../component-trial/select.sh" "$BRICK_COMPONENT_CATALOG" component-roles.json components "${BRICK_COMPONENT_MODE:-trained}"
    while IFS= read -r header; do
        [[ $header =~ ^[a-zA-Z0-9_-]+\.hpp$ && ! -e bricks/$header ]] || exit 2
        cp "$BRICK_COMPONENT_CATALOG/headers/$header" "bricks/$header"
    done < <(jq -r '[.[].header]|unique[]' components/helpers.json)
fi
status assemble started
if [[ -f components/helpers.json ]]; then
    if [[ -f $BRICK_COMPONENT_CATALOG/providers/contracts.json ]]; then
        status provider_plan started
        "$here/provider-stage.sh" "$BRICK_COMPONENT_CATALOG"
    else
        component_emitter=${COMPONENT_EMITTER:-"$(dirname "$PICKER")/brick-component-emit"}
        if [[ -z ${COMPONENT_EMITTER:-} && ! -x $component_emitter ]]; then
            component_emitter="$root/experiments/boilerplate-B28/tools/brick-component-emit"
        fi
        [[ -x $component_emitter ]] || { echo "Build brick-component-emit or set COMPONENT_EMITTER to its path" >&2; exit 2; }
        "$component_emitter" "$here/wiring-operations.json" components/helpers.json "$spec" route.json wiring.json > components/emission.json
    fi
    while IFS= read -r slot; do
        fn="component_$slot"
        jq -er --argjson slot "$slot" '.providers[]|select(.slot==$slot)|.source' components/emission.json > "bricks/$fn.hpp"
        packages=$(jq -r --argjson slot "$slot" --slurpfile helpers components/helpers.json '
            .providers[]|select(.slot==$slot)|.bricks as $ids |
            [$helpers[0][]|select(.id as $id|$ids|index($id))|.packages[]]|unique|join(",")' components/emission.json)
        jq -r --arg fn "$fn" --arg pkgs "${packages:--}" --argjson slot "$slot" \
            '"\($fn) \($fn) \($fn).hpp \($pkgs) : \(.args[0]) \(.args[$slot+1])"' route.json >> library.txt
        jq --arg fn "$fn" --argjson slot "$slot" '
            .goals[$slot]=$fn | .rows[$slot] += {bound:true,provider:$fn,adapter:{output:.args[$slot+1]}}' route.json > route.new
        mv route.new route.json
    done < <(jq -r '.providers[].slot' components/emission.json)
fi
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
if [[ -f components/typed-gaps.json ]]; then
    gaps=$((gaps + $(jq 'length' components/typed-gaps.json)))
fi
if (( gaps )); then
    printf '#pragma once\n#include <stdexcept>\n' > bricks/gaps.hpp
    if [[ -f components/typed-gaps.json ]] && jq -e 'length>0' components/typed-gaps.json >/dev/null; then
        echo '#include "provider_types.hpp"' >> bricks/gaps.hpp
        jq -r '.[]|.signature + " { throw std::runtime_error(\"Unimplemented interface\"); }"' components/typed-gaps.json >> bricks/gaps.hpp
    fi
    while IFS= read -r slot; do
        echo "inline void missing_$slot(const char* input_path, const char* output_path) { throw std::runtime_error(\"Missing role $slot\"); }" >> bricks/gaps.hpp
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
    c++ -std=c++17 -fPIC -O2 -Ibricks project/converter.cpp -o project/converter "${flags[@]}"
}
assess_candidate() {
    local tag=$1 stage=$2 edited=$3
    passes "$tag-checks.json" || return 0
    if [[ $edited == false ]] && (( gaps )); then return 0; fi
    # No test links a free-text residual to an assertion. Preserve the passing
    # artifact and surface that obligation rather than guessing another edit.
    if [[ -f components/typed-gaps.json ]] && jq -e 'any(.providers[]; (.uncovered|length)>0)' components/emission.json >/dev/null; then
        jq --arg stage "$stage" --argjson used "$edited" '{passed:false,completed_at:"requires_verification",
            candidate_stage:$stage,stage5_used:$used,checks_passed:true,
            unresolved:[.providers[]|select(.uncovered|length>0)|{slot,uncovered}]}' components/emission.json > result.json
        status requires_verification "All executable checks passed; residual requirements remain"
        exit 2
    fi
    accept "$stage" "$edited"
    exit 0
}
status build_test started
if build_program initial > initial-build.log 2>&1; then
    verify initial
    assess_candidate initial deterministic false
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
            assess_candidate fixit compiler_fixit false
        fi
    fi
fi
if [[ -f components/emission.json ]] && jq -e '(.interpretation_errors|length)>0' components/emission.json >/dev/null; then
    jq '{passed:false,completed_at:"invalid_interpretation",stage5_used:false,
        interpretation_errors}' components/emission.json > result.json
    status failed invalid_interpretation
    exit 2
fi
if [[ -n $learning_client && -f components/typed-gaps.json ]] && (( gaps == 1 )); then
    status brick_learning attempted
    if "$here/../component-trial/learning/learn.sh" "$BRICK_COMPONENT_CATALOG" "$run" "$suite" "$learning_client" "$learning_dest" > learning.log 2>&1; then
        header=$(jq -er .header learning/adapter.json)
        cp "learning/bricks/$header" learning/bricks/gaps.hpp bricks/
        cp learning/project/{packages.txt,converter} project/
        cp learning/checks.json learned-checks.json
        status brick_published "$learning_dest"
        accept generated_brick false
        exit 0
    fi
    status brick_learning no_promotion
fi
if [[ ${BRICK_EDITOR:-} == off ]]; then
    status stage5 disabled
    jq -n '{passed:false,completed_at:"editor_disabled",stage5_used:false}' > result.json
    exit 2
fi
status stage5 requested
if [[ -f components/helpers.json ]]; then
    { cat project/packages.txt; jq -r '.[].packages[]' components/helpers.json; } | sort -u > packages.new
    mv packages.new project/packages.txt
fi
mkdir -p calls/edit
"$here/edit-context.sh" "$run" > calls/edit/prompt.txt
reject_edit() {
    jq -n --arg stage "$1" '{passed:false,completed_at:$stage,stage5_used:true}' > result.json
    status failed "$1"
    exit 1
}
call_model edit || reject_edit edit_response_failed
typed='{}'
if [[ -f components/typed-gaps.json ]]; then typed=$(cat components/typed-gaps.json); fi
jq -es --argjson typed "$typed" --slurpfile files edit-files.json --slurpfile gaps edit-gaps.json -f "$here/bind-gaps.jq" \
    calls/edit/response.json > resolved-edit.json || reject_edit invalid_edit
jq --slurpfile files edit-files.json -f "$here/patch.jq" resolved-edit.json > edited-files.json || reject_edit invalid_edit
while IFS= read -r file; do jq -r --arg f "$file" '.[$f][]' edited-files.json > "$file"; done < <(jq -r 'keys[]' edited-files.json)
{ cat project/packages.txt; jq -r '.add_packages[]' calls/edit/response.json; } | sort -u > packages.new
mv packages.new project/packages.txt
if build_program edit > edit-build.log 2>&1; then
    verify edit
    assess_candidate edit model_edit true
fi
jq -n '{passed:false,completed_at:"after_edit",stage5_used:true}' > result.json
status failed after_edit
exit 1
