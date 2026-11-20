#!/bin/bash
set -euo pipefail
bin=$(realpath "$1")
here=$(cd "$(dirname "$0")" && pwd)
trial="$here/../component-trial"
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
cd "$work"
cat > spec <<'JSON'
{"input_type":"text/tab-separated-values","roles":[{"id":"existing"},{"id":"extract","multiple":false,"content":"whole","output_type":"application/json"}]}
JSON
echo '{"rows":[{"role":"existing","bound":true},{"role":"extract","bound":false}]}' > route
cat > wiring <<'JSON'
{"existing":[["unsupported"]],"extract":[["let","out",["texts"]],["each_row",[["if",["eq",["field",0],"take"],[["append",{"ref":"out"},["field",1]]]]]],["write_array",{"ref":"out"}]]}
JSON
jq '[.[]|select(.id=="t80" or .id=="j85")]' "$trial/components.json" > helpers
emit() { "$bin" "$here/wiring-operations.json" "$1" spec route "$2"; }
emit helpers wiring > report
jq -e '.gaps=={} and [.providers[].slot]==[1] and .providers[0].bricks==["j85","t80"]' report >/dev/null
jq -r '.providers[0].source' report > component.hpp
cat > main.cpp <<'CPP'
#include "component.hpp"
int main() { component_1("source.tsv", "out.json"); }
CPP
flags=$(pkg-config --cflags --libs json-c)
read -r -a libs <<< "$flags"
c++ -std=c++17 -I"$trial/include" main.cpp -o converter "${libs[@]}"
printf 'skip\ta\ntake\tb\ntake\tc\n' > source.tsv
./converter
jq -e '.==["b","c"]' out.json >/dev/null

# A missing selected provider and out-of-scope or incomplete operations stay editable gaps.
jq 'map(select(.id!="t80"))' helpers > partial
emit partial wiring | jq -e '.providers==[] and (.gaps.extract|contains("t80"))' >/dev/null
for change in '.extract=[["write_array",["field",0]]]' '.extract=[["write_array",["texts"]]]' \
              '.extract=[["run arbitrary C++"]]' '.extract=[["let","tmp",{"ref":"outside"}]]'; do
    jq "$change" wiring > invalid
    emit helpers invalid | jq -e '.providers==[] and (.gaps.extract|type=="string")' >/dev/null
done
