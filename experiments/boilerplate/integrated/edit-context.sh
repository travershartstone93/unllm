#!/bin/bash
set -euo pipefail
cd "$1"
files=(project/converter.cpp)
# The generated main includes only the selected assembly's headers.
if jq -e 'any(.rows[]; .bound==false)' route.json >/dev/null; then
    files+=("bricks/gaps.hpp")
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
cat <<'PROMPT'
Repair the unresolved roles or failing behavior of this assembled C++ program. Return JSON only:
{"edits":[{"file":"file from context","start":1,"end":1,"lines":["replacement code"]}],"add_packages":[]}
Line numbers are one-based inclusive original ranges. Change only what the diagnostics or unfilled roles require. Retain working code. You can add pkg-config packages. Use no tools.
PROMPT
cat request.txt calls/interpret/response.json route.json project/packages.txt
for p in initial-build.log fixit.log fixit-build.log; do if [[ -f $p ]]; then head -c 3500 "$p"; fi; done
for p in initial-checks.json fixit-checks.json; do if [[ -f $p ]]; then jq '.checks|with_entries(select(.value.pass_ != true))' "$p"; fi; done
for f in "${files[@]}"; do printf '\n%s\n' "$f"; nl -ba "$f"; done
