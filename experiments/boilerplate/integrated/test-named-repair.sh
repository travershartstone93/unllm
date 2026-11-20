#!/bin/bash
# Exercise the same two validation passes used by the integrated runner.
set -euo pipefail
pilot=$(cd -- "$(dirname -- "$0")" && pwd)
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
cd "$work"
mkdir -p bricks project calls/interpret
printf '%s\n' '{"rows":[{"bound":false},{"bound":true},{"bound":false}]}' > route.json
touch request.txt project/packages.txt
echo '{}' > calls/interpret/response.json
cat > bricks/gaps.hpp <<'CPP'
#pragma once
#include <stdexcept>

inline void missing_0(const char*, const char*) {
    throw std::runtime_error("Missing role 0");
}
inline void missing_2(const char*, const char*) { throw std::runtime_error("Missing role 2"); }
CPP
cat > project/converter.cpp <<'CPP'
#include "gaps.hpp"
int main() {
    missing_0("left", "right");
    missing_2("before", "after");
}
CPP
"$pilot/edit-context.sh" "$work" > prompt.txt
jq -e '. == ["missing_0","missing_2"]' edit-gaps.json >/dev/null
cat > repair.json <<'JSON'
{
  "functions": [
    {"name":"missing_2","support":["#include <string>"],"body":["if (std::string(input_path) != \"before\" || std::string(output_path) != \"changed\") throw 2;"]},
    {"name":"missing_0","support":["#include <cstring>"],"body":["if (std::strcmp(input_path, \"left\") || std::strcmp(output_path, \"right\")) throw 1;"]}
  ],
  "edits":[{"file":"project/converter.cpp","start":4,"end":4,"lines":["    missing_2(\"before\", \"changed\");"]}],
  "add_packages":[]
}
JSON
resolve() {
    jq -es --slurpfile files edit-files.json --slurpfile gaps edit-gaps.json -f "$pilot/bind-gaps.jq" "$1" |
        jq -e --slurpfile files edit-files.json -f "$pilot/patch.jq"
}
resolve repair.json > updated.json
jq -r '."bricks/gaps.hpp"[]' updated.json > bricks/gaps.hpp
jq -r '."project/converter.cpp"[]' updated.json > project/converter.cpp
c++ -std=c++17 -Wall -Wextra -Werror -Ibricks project/converter.cpp -o converter
./converter
for bad in '.functions |= .[:1]' '.functions[1].name="missing_2"' '.functions[0].name="missing_7"' \
    '.functions[0].body=["line\nbreak"]' '.edits[0].file="bricks/gaps.hpp"' '.edits[0].end=100'; do
    jq "$bad" repair.json > bad.json
    if resolve bad.json >/dev/null 2>&1; then echo "Unexpectedly accepted $bad" >&2; exit 1; fi
done

# An existing behavior repair with no missing functions retains the range-patch interface.
echo '[]' > edit-gaps.json
jq 'del(.functions)' repair.json > ranges.json
resolve ranges.json > range-output.json
jq --slurpfile files edit-files.json -f "$pilot/patch.jq" ranges.json > previous.json
cmp previous.json range-output.json
# Typed repairs may define a multiline support helper, but the connector is owned by the planner.
echo '["gap_0_0"]' > edit-gaps.json
echo '#pragma once' > bricks/provider_types.hpp
typed='{"gap_0_0":{"signature":"inline int gap_0_0(int value)"}}'
cat > typed.json <<'JSON'
{"functions":[{"name":"gap_0_0","support":["inline int twice(int n) {\n    return 2 * n;\n}"],"body":["return twice(value);"]}],"edits":[],"add_packages":[]}
JSON
resolve_typed() {
    jq -es --argjson typed "$typed" --slurpfile files edit-files.json --slurpfile gaps edit-gaps.json -f "$pilot/bind-gaps.jq" "$1" |
        jq -e --slurpfile files edit-files.json -f "$pilot/patch.jq"
}
resolve_typed typed.json | jq -r '."bricks/gaps.hpp"[]' > bricks/gaps.hpp
printf '%s\n' '#include "gaps.hpp"' 'int main() { return gap_0_0(2) == 4 ? 0 : 1; }' > project/test.cpp
c++ -std=c++17 -Wall -Wextra -Werror -Ibricks project/test.cpp -o typed-test
./typed-test
jq --slurpfile other repair.json '.edits=$other[0].edits' typed.json > crosses-boundary.json
if resolve_typed crosses-boundary.json >/dev/null 2>&1; then echo "Typed repair changed the connector" >&2; exit 1; fi
echo 'Named repairs, typed gap boundaries, mixed wiring and range-only repairs passed.'
