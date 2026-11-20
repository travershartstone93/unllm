#!/bin/bash
set -euo pipefail
emit=$(realpath "$1")
here=$(cd "$(dirname "$0")/../component-trial" && pwd)
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
cd "$work"
mkdir -p bundle/{providers,headers}
cp "$here"/providers/library.json bundle/providers
cp "$here"/include/unique_fifo.hpp bundle/headers
cp "$here"/components.json bundle/
echo '[]' > picks
cat > spec <<'JSON'
{"artifact":{"kind":"header","filename":"numbers.h","class_name":"Numbers","value_type":"integer","methods":[{"name":"add","operation":"insert_unique"},{"name":"next","operation":"take_oldest"}],"uncovered":[]}}
JSON
"$emit" bundle spec picks > plan
jq -e '.brick=="u84" and .filename=="numbers.h"' plan >/dev/null
jq -r .source plan > numbers.h
jq '.artifact.filename="others.h" | .artifact.class_name="Others"' spec > other-spec
"$emit" bundle other-spec picks | jq -er .source > others.h
cat > caller.cpp <<'CPP'
#include "numbers.h"
#include "others.h"
#include "others.h"
#include "numbers.h"
int main() {
    Numbers first,second;
    Others third;
    return !(first.add(9) && !first.add(9) && first.next()==9 && first.add(9)
             && !second.next() && third.add(9) && third.next()==9);
}
CPP
c++ -std=c++17 caller.cpp -o caller
./caller
for invalid in '.artifact.methods[0].operation="sort_queue"' '.artifact.filename="../numbers.h"' '.artifact.methods[1].name="add"'; do
    jq "$invalid" spec > bad
    if "$emit" bundle bad picks >/dev/null 2>&1; then exit 1; fi
done
