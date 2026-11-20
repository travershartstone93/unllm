#!/bin/bash
set -euo pipefail
cd "$1"
files=(project/converter.cpp)
# The generated main includes only the selected assembly's headers.
jq '[.rows|to_entries[]|select(.value.bound==false)|"missing_\(.key)"]' route.json > edit-gaps.json
if [[ -f components/typed-gaps.json ]]; then
    jq --slurpfile typed components/typed-gaps.json '. + ($typed[0]|keys)' edit-gaps.json > edit-gaps.tmp
    mv edit-gaps.tmp edit-gaps.json
fi
if jq -e 'length>0' edit-gaps.json >/dev/null; then
    files+=("bricks/gaps.hpp")
    # Mixed runs may need to repair an emitted connector as well as fill other slots.
    locked='[]'
    if [[ -f components/typed-gaps.json ]]; then
        locked=$(jq -c '[.providers[]|select(.typed_gaps|length>0)|.function]' components/emission.json)
    fi
    while IFS= read -r h; do
        [[ $h =~ ^component_[0-9]+$ && -f bricks/$h.hpp ]] || exit 2
        files+=("bricks/$h.hpp")
    done < <(jq -r --argjson locked "$locked" '
        .rows[]|select(.bound and .provider!=null)|.provider|select(. as $fn|$locked|index($fn)|not)' route.json)
else
while IFS= read -r h; do
    [[ $h =~ ^[A-Za-z0-9_-]+\.hpp$ && -f bricks/$h ]] || exit 2
    files+=("bricks/$h")
done < <(sed -n 's/^#include "\([^"]*\)".*/\1/p' project/converter.cpp)
fi
printf '{}\n' > edit-files.json
for f in "${files[@]}"; do
    jq --arg f "$f" --rawfile src "$f" '.[$f]=($src|split("\n")|if last=="" then .[:-1] else . end)' edit-files.json > edit-files.tmp
    mv edit-files.tmp edit-files.json
done
if jq -e 'length>0' edit-gaps.json >/dev/null; then
    if [[ -f components/typed-gaps.json ]] && jq -e 'length>0' components/typed-gaps.json >/dev/null; then
        cat <<'PROMPT'
Implement the named missing functions below. Return JSON {"functions":[{"name":"name","support":["file-scope include or helper"],"body":["one source line"]}],"edits":[],"add_packages":[]}. The assembler owns the signatures and all planned wiring. Supply a body for each required name, in the exact signature shown. Use only the typed function arguments. For any additional missing_N file slot its signature is inline void missing_N(const char* input_path, const char* output_path). Use no tools.
PROMPT
        jq -r '.[]|.signature + "  // " + .from + " -> " + .to' components/typed-gaps.json
        cat bricks/provider_types.hpp
    else
    cat <<'PROMPT'
Implement every named missing C++ function. Return exactly one JSON object:
{"functions":[{"name":"missing_N","support":["#include <fstream>"],"body":["// C++ statements"]}],"edits":[],"add_packages":[]}
Use the actual names listed below. The assembler writes each signature and its outer braces:
inline void NAME(const char* input_path, const char* output_path)
Put function statements in body and any file-scope includes or helper definitions in support. Supply one array entry per source line. The assembler owns bricks/gaps.hpp. If wiring must change in project/converter.cpp, use edits with file, start, end and lines (one-based inclusive original range). Keep accepted operations. Add required pkg-config packages in add_packages. Use no tools.
Required functions:
PROMPT
    fi
    jq -r '.[]' edit-gaps.json
    if jq -e 'any(.rows[]; .bound) and any(.rows[]; .bound|not)' route.json >/dev/null; then
        cat <<'PROMPT'
Existing output paths in main are listed below. When a missing operation consumes an earlier result, use a wiring edit to pass that output path as input_path. Its contents will already be available after the producing call. For independent operations, keep the original input.
PROMPT
        if (( ${#files[@]} > 2 )); then
            echo 'Generated component wiring files listed below also support range edits when an existing connection needs repair.'
        fi
        jq -r '.rows|to_entries[]|select(.value.bound)|
            "argv[\(.key + 2)] = output of role \(.value.role) (\(.value.adapter.output))"' route.json
    fi
else
    cat <<'PROMPT'
Repair the failing behavior of this assembled C++ program. Return JSON only:
{"edits":[{"file":"file from context","start":1,"end":1,"lines":["replacement code"]}],"add_packages":[]}
Line numbers are one-based inclusive original ranges. Retain working code. You can add pkg-config packages. Use no tools.
PROMPT
fi
if [[ -f components/typed-gaps.json ]]; then
    jq -c '{missing_outputs:.gaps,plans:[.providers[]|{function,status,steps:.plan.steps,uncovered}]}' components/emission.json
fi
cat request.txt calls/interpret/response.json route.json project/packages.txt
build_logs=(initial-build.log fixit.log)
checks=initial-checks.json
if [[ -f fixit-checks.json ]]; then
    build_logs=(fixit-build.log)
    checks=fixit-checks.json
fi
for p in "${build_logs[@]}"; do if [[ -f $p ]]; then head -c 3500 "$p"; fi; done
if [[ -f $checks ]]; then jq '.checks|with_entries(select(.value.pass_ != true))' "$checks"; fi
for f in "${files[@]}"; do [[ $f != bricks/gaps.hpp ]] || continue; printf '\n%s\n' "$f"; nl -ba "$f"; done

if [[ -f components/helpers.json ]] && jq -e 'length>0' components/helpers.json >/dev/null; then
    echo 'The assembler has placed the following reusable C++ headers. Include and call the parts that implement the requested behavior; supply only the necessary wiring and missing logic. The headers are read-only.'
    while IFS= read -r header; do
        printf '\nHeader "%s"\n' "$header"
        cat "bricks/$header"
    done < <(jq -r '.[].header' components/helpers.json)
fi
