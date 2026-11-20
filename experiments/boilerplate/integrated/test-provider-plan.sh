#!/bin/bash
# Change the catalog while keeping the executable, requirement and source files fixed.
set -euo pipefail
bin=$(realpath "$1") root=$(realpath "$2") worker=$(realpath "$3")
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
cd "$work"
cat > catalog <<'JSON'
{"types":{"words":"std::vector<std::string>","numbers":"std::vector<int>"},
 "outcomes":{"word_lengths":{"parameters":{}}},
 "providers":[
 {"id":"parse","brick":"A","from":"in:text/plain","to":"words","header":"demo.hpp","call":"read_words"},
 {"id":"sizes","brick":"B","effect":"word_lengths","from":"words","to":"numbers","header":"demo.hpp","call":"sizes"},
 {"id":"wrong","brick":"D","effect":"word_offsets","from":"words","to":"numbers","header":"demo.hpp","call":"wrong"},
 {"id":"save","brick":"C","from":"numbers","to":"out:text/plain","header":"demo.hpp","call":"save"}]}
JSON
echo '[{"id":"D"},{"id":"B"},{"id":"A"},{"id":"C"}]' > available
echo '[{"id":"D"}]' > picks
echo '{"input_type":"text/plain","roles":[{"id":"r","output_type":"text/plain","content":"whole","multiple":false}]}' > spec
echo '{"rows":[{"role":"r","bound":false}]}' > route
echo '{"r":{"outcome":"word_lengths","parameters":{},"uncovered":[]}}' > goal
plan() { "$bin" "$root" "$worker" "$1" "$2" picks spec route goal; }
plan catalog available > full
jq -e '.providers[0] | .status=="complete" and .bricks==["A","B","C"] and .plan.proof.wire_derivation!=null and .typed_gaps==[]' full >/dev/null
jq '.providers|=reverse' catalog > reversed
plan reversed available | jq -e --slurpfile f full '.providers[0].source==$f[0].providers[0].source' >/dev/null
cat > provider_types.hpp <<'CPP'
#pragma once
#include <string>
#include <vector>
CPP
cat > demo.hpp <<'CPP'
#pragma once
#include "provider_types.hpp"
#include <fstream>
inline std::vector<std::string> read_words(const char* path) {
    std::ifstream in(path); std::vector<std::string> result;
    for (std::string s; in >> s;) result.push_back(s);
    return result;
}
inline std::vector<int> sizes(const std::vector<std::string>& words) {
    std::vector<int> result;
    for (const auto& word : words) result.push_back(static_cast<int>(word.size()));
    return result;
}
inline void save(const std::vector<int>& values, const char* path) {
    std::ofstream out(path);
    for (int v : values) out << v << '\n';
}
CPP
jq -r '.providers[0].source' full > component.hpp
printf '%s\n' '#include "component.hpp"' 'int main(){component_0("words","lengths");}' > main.cpp
c++ -std=c++17 main.cpp -o demo
echo 'a red train' > words
./demo
diff -u <(printf '1\n3\n5\n') lengths
echo '[{"id":"B"},{"id":"D"}]' > partial
plan catalog partial | jq -e '.providers[0] | .status=="partial" and .bricks==["B"] and
 (.plan.steps|map(.provider))==[null,"sizes",null] and (.typed_gaps|map([.from,.to]))==[["in:text/plain","words"],["numbers","out:text/plain"]]' >/dev/null
echo '[{"id":"A"},{"id":"C"},{"id":"D"}]' > unavailable
plan catalog unavailable | jq -e '.providers==[] and (.gaps.r|contains("word_lengths"))' >/dev/null
# An unrecognized runtime claim is open evidence on otherwise usable wiring.
jq '.r.runtime=["atomic_write"]' goal > with-claim
"$bin" "$root" "$worker" catalog available picks spec route with-claim |
    jq -e --slurpfile f full '.providers[0] | .source==$f[0].providers[0].source and .status=="partial"
      and .typed_gaps==[] and .runtime==[] and (.uncovered[0]|contains("atomic_write"))' >/dev/null
# A repeated observable has independent parameters and order, even with identical ports.
cat >> demo.hpp <<'CPP'
inline std::vector<int> affine(std::vector<int> values,int factor,int offset) {
    for(auto& value:values) value=factor*value+offset;
    return values;
}
CPP
jq '.outcomes.affine={parameters:{factor:"integer",offset:"integer"}} |
    .providers += [{id:"aff",brick:"B",effect:"affine",from:"numbers",to:"numbers",header:"demo.hpp",call:"affine",args:["factor","offset"]}]' catalog > multi
cat > goal <<'JSON'
{"r":{"operations":[{"outcome":"word_lengths","parameters":{}},{"outcome":"affine","parameters":{"factor":2,"offset":3}},{"outcome":"affine","parameters":{"factor":1,"offset":-5}}],"uncovered":[]}}
JSON
plan multi available > composed
jq -e '.providers[0]|.typed_gaps==[] and (.plan.steps|length)==5' composed >/dev/null
jq -r '.providers[0].source' composed > component.hpp
c++ -std=c++17 main.cpp -o demo
./demo
diff -u <(printf '0\n4\n8\n') lengths
jq '.r.operations[1].outcome="absent"' goal > bad-goal
"$bin" "$root" "$worker" multi available picks spec route bad-goal | jq -e '.providers==[] and (.gaps.r|contains("absent"))' >/dev/null
