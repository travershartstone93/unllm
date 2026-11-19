#!/usr/bin/env bash
set -euo pipefail
bin=$(realpath "$1")
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT
cd "$scratch"
cat > cards <<'JSON'
[{"id":"br1","description":"status window","tags":["one"]},
 {"id":"br2","description":"status window","tags":["two"]}]
JSON
cat > roles <<'JSON'
[{"id":"x","text":"status window","requires":["two"]},
 {"id":"y","text":"status window","requires":["two"]},
 {"id":"z","text":"status window"},
 {"id":"w","text":"read compressed archives"}]
JSON
"$bin" bm25 cards roles > result
jq -e '.bindings=={x:"br2",y:"br2",z:null,w:null} and .selected==["br2"] and .missing==["z","w"]' result >/dev/null
jq '.bindings' result > bindings
"$bin" resolve cards roles bindings > replay
cmp result replay
for change in 'del(.w)' '.extra=null' '.x="br1"' '.x="unknown"' '.x=123'; do
    jq "$change" bindings > invalid
    if "$bin" resolve cards roles invalid > unexpected 2> diagnostic; then exit 1; fi
    test ! -s unexpected && test -s diagnostic
done
jq '. + [.[0]]' roles > duplicate
if "$bin" bm25 cards duplicate > unexpected 2> diagnostic; then exit 1; fi

echo '[{"id":"clock","description":"crystal quartz oscillator"}]' > cards
echo '[{"id":"x","text":"attach crystal quartz oscillator for generating regular pulses across disparate boards"}]' > roles
"$bin" bm25 cards roles | jq -e '.bindings=={x:null}' >/dev/null
"$bin" bm25-capped cards roles | jq -e '.bindings=={x:"clock"}' >/dev/null

# Reviews must refer to the current catalog. Incompatible evidence cannot assign a brick.
cat > cards <<'JSON'
[{"id":"a","description":"pressure measurement","tags":["analog"]},
 {"id":"d","description":"plot readings","tags":["display"]}]
JSON
jq -n --slurpfile catalog cards '{catalog:$catalog[0], examples:[
    {text:"Sample chamber pressure",brick:"a"},
    {text:"Export pressure chart",brick:null}]}' > reviews
cat > roles <<'JSON'
[{"id":"p","text":"Sample chamber pressure"},{"id":"n","text":"Export pressure chart"},
 {"id":"t","text":"Sample chamber pressure","requires":["display"]}]
JSON
for mode in reviewed reviewed-guard; do
    "$bin" "$mode" cards roles reviews | jq -e '.bindings=={p:"a",n:null,t:null}' >/dev/null
done
jq '[.[] | select(.id=="p")]' roles > covered
"$bin" fill-gaps cards covered reviews unused-model |
    jq -e '.first_pass=={p:"a"} and .bindings==.first_pass and (has("model")|not)' >/dev/null
echo '[]' > empty
"$bin" review-all cards empty reviews unused-model |
    jq -e '.first_pass=={} and .bindings=={} and .selected==[] and (has("model")|not)' >/dev/null
if [[ ${2:-} == bge ]]; then
    jq -c . covered empty |
        "$bin" guarded-bge cards - reviews missing-model |
        jq -es 'map(.bindings)==[{p:"a"},{}] and all(.bindings==.first_pass)' >/dev/null
    if "$bin" guarded-bge cards roles reviews missing-model > unexpected 2> diagnostic; then exit 1; fi
    test ! -s unexpected && test -s diagnostic
    if [[ -n ${PICKER_BGE_TEST_MODEL:-} ]]; then
        jq '.examples += [{text:"Measure the trapped gas",brick:"a"},{text:"Measure the trapped gas",brick:"d"}]' reviews > ties
        echo '[{"id":"tied","text":"Measure the trapped gas"},{"id":"excluded","text":"Sample chamber pressure","requires":["missing-tag"]}]' > probe
        for mode in guarded-bge evidence-bge; do
            "$bin" "$mode" cards probe ties "$PICKER_BGE_TEST_MODEL" |
                jq -e '.bindings=={tied:null,excluded:null}' >/dev/null
        done
    fi
fi
jq 'reverse' cards > reversed
"$bin" reviewed-guard reversed roles reviews | jq -e '.bindings=={p:"a",n:null,t:null}' >/dev/null
for change in '.catalog[0].description="changed"' '.catalog[0].outputs=["different artifact"]' '.examples[0].brick="unknown"' '.examples=[]'; do
    jq "$change" reviews > invalid
    if "$bin" reviewed-guard cards roles invalid > unexpected 2> diagnostic; then exit 1; fi
    test ! -s unexpected && test -s diagnostic
done
