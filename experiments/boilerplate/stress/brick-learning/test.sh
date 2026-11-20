#!/bin/bash
# Replay the controlled ablation through the actual interpreter, native provider planner and learning gate.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
pilot="$here/../../integrated"; trial="$here/../../component-trial"
w=$(realpath -m "${1:?Supply a new empty experiment directory}")
mkdir "$w"
python3 "$here/fixture.py" "$w/base"
"$trial/verify-bundle.sh" "$w/base"
interpret() (
    task=$1
    bundle=${2:-"$w/base"}; label=${3:-$task}
    mkdir -p "$w/english-$label/calls/interpret"; cd "$w/english-$label"
    jq -r --arg t "$task" '.[$t]' "$here/requests.json" > request.txt
    export BRICK_COMPONENT_CATALOG="$bundle"
    { "$pilot/provider-prompt.sh" "$BRICK_COMPONENT_CATALOG"; printf '\nUser request:\n'; cat request.txt; } > calls/interpret/prompt.txt
    if [[ -n ${BRICK_REPLAY_DIR:-} ]]; then
        cp "$BRICK_REPLAY_DIR/$label.json" calls/interpret/response.json
    else
        bash "$pilot/opencode-call.sh" calls/interpret
    fi
)
assemble() (
    local name=$1 task=$2 bundle=$3
    mkdir -p "$w/$name"/{project,bricks,components,calls/interpret,check}
    cd "$w/$name"
    cp "$w/english-${4:-$task}/calls/interpret/response.json" calls/interpret/
    jq -e '.roles|length==1' calls/interpret/response.json >/dev/null
    jq '[.roles[]|{key:.id,value:.requirement}]|from_entries' calls/interpret/response.json > requirements.json
    jq '{rows:[.roles[]|{role:.id,bound:false}]}' calls/interpret/response.json > route.json
    echo '[]' > components/helpers.json
    "$pilot/provider-stage.sh" "$bundle"
    jq -er '.providers[0].source' components/emission.json > bricks/component_0.hpp
    jq '.rows[0].bound=true' route.json > route.tmp; mv route.tmp route.json
    jq -r '[.[].packages[]]|unique[]' components/helpers.json > project/packages.txt
    { echo '#pragma once'; echo '#include <stdexcept>'; jq -r '.[]|.signature+" { throw std::runtime_error(\"Unimplemented interface\"); }"' components/typed-gaps.json; } > bricks/gaps.hpp
    cat > project/converter.cpp <<'CPP'
#include "component_0.hpp"
int main(int argc, char** argv) {
    if (argc!=3) return 2;
    try { component_0(argv[1],argv[2]); } catch(const std::exception&) { return 1; }
}
CPP
    mapfile -t pkgs < project/packages.txt
    c++ -std=c++17 -fPIC -O2 -Ibricks project/converter.cpp -o project/converter $(pkg-config --cflags --libs "${pkgs[@]}")
    LEARNING_CASE="$task" "$here/check.py" project/converter check > checks.json
)
assert_gap() {
    jq -e 'length==1 and all(.[];.from=="in:text/uri-list" and .to=="strings")' "$w/$1/components/typed-gaps.json" >/dev/null
    jq -e 'any(.checks[];.pass_==false)' "$w/$1/checks.json" >/dev/null
}
for t in create transfer; do interpret "$t"; done
assemble create create "$w/base"; assert_gap create
assemble before transfer "$w/base"; assert_gap before
export LEARNING_CASE=create
"$trial/learning/learn.sh" "$w/base" "$w/create" "$here/check.py" "$here/client.cpp" "$w/learned"
assemble after transfer "$w/learned"
jq -e --slurpfile a "$w/create/learning/adapter.json" '.providers[0]|.status=="complete" and (.bricks|index($a[0].id)!=null)' "$w/after/components/emission.json" >/dev/null
jq -e 'all(.checks[];.pass_)' "$w/after/checks.json" >/dev/null
assemble removed transfer "$w/base"; assert_gap removed
cmp "$w/before/calls/interpret/response.json" "$w/after/calls/interpret/response.json"
cmp "$w/before/components/emission.json" "$w/removed/components/emission.json"
# Reject a candidate if a separate client fails, even though its creating task passes.
assemble reject create "$w/base"
echo 'int main(){return 1;}' > "$w/failing-client.cpp"
if "$trial/learning/learn.sh" "$w/base" "$w/reject" "$here/check.py" "$w/failing-client.cpp" "$w/rejected"; then exit 1; fi
test ! -e "$w/rejected"
jq -e 'all(.checks[];.pass_)' "$w/reject/learning/checks.json" >/dev/null
for a in create create/learning before after removed; do
    jq -r --arg arm "$a" '[ $arm, ([.checks[]|select(.pass_)]|length), (.checks|length) ]|@tsv' "$w/$a/checks.json"
done

# Separate from the matched test: interpret again while actually seeing the published catalog.
if [[ -z ${BRICK_REPLAY_DIR:-} || -f $BRICK_REPLAY_DIR/published-transfer.json ]]; then
    interpret transfer "$w/learned" published-transfer
    assemble published-transfer transfer "$w/learned" published-transfer
    jq -e --slurpfile a "$w/create/learning/adapter.json" '.providers[0]|.status=="complete" and (.bricks|index($a[0].id)!=null)' "$w/published-transfer/components/emission.json" >/dev/null
    jq -e 'all(.checks[];.pass_)' "$w/published-transfer/checks.json" >/dev/null
fi
