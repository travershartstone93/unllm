#!/bin/bash
# Learn one missing file reader in an isolated copy of an assembled CLI, then publish a verified catalog generation.
set -euo pipefail
[[ $# == 5 ]] || { echo 'Usage: learn.sh BASE_BUNDLE RUN TASK_EVALUATOR INDEPENDENT_CLIENT.cpp NEW_BUNDLE' >&2; exit 2; }
base=$(realpath -e "$1"); run=$(realpath -e "$2"); suite=$(realpath -e "$3"); client=$(realpath -e "$4"); dest=$(realpath -m "$5")
trial=$(cd "$(dirname "$0")/.." && pwd)
repo=$(cd "$trial/../../.." && pwd)
[[ -d $base && -x $suite && -f $client && ! -e $dest && ! -L $dest && $dest != "$base" && $dest != "$base/"* ]]
cd "$run"
jq -e '(.gaps|length)==0 and (.interpretation_errors|length)==0 and
  ([.providers[].typed_gaps[]]|length)==1 and all(.providers[]; .uncovered==[])' components/emission.json >/dev/null
jq -e '(.rows|length)>0 and all(.rows[];.bound==true)' route.json >/dev/null
mkdir learning
work="$run/learning"
gir=${BRICK_GIR_FILE:-/usr/share/gir-1.0/GLib-2.0.gir}
cp -R bricks project "$work/"
cd "$work"
signature=$(jq -er 'values[]|.signature' "$run/components/typed-gaps.json")
for mode in whole-file per-line; do
    mkdir -p "$mode/check"
    "${BRICK_GIR_EMITTER:-$repo/build/native/brick-gir-adapter}" "$run/components/typed-gaps.json" "$gir" "$mode" > "$mode/adapter.json"
    candidate="$work/$mode/adapter.json"
    id=$(jq -er .id "$candidate"); header=$(jq -er .header "$candidate"); package=$(jq -er .package "$candidate")
    call=$(jq -er .call "$candidate")
    jq -er .source "$candidate" > "bricks/$header"
    printf '#pragma once\n#include "%s"\n%s { return %s(input_path); }\n' "$header" "$signature" "$call" > bricks/gaps.hpp
    { cat "$run/project/packages.txt"; echo "$package"; } | sort -u > project/packages.txt
    mapfile -t packages < project/packages.txt
    opts=$(pkg-config --cflags --libs "${packages[@]}"); read -r -a flags <<< "$opts"
    c++ -std=c++17 -fPIC -O2 -Ibricks project/converter.cpp -o project/converter "${flags[@]}"
    code=0
    "$suite" "$work/project/converter" "$work/$mode/check" > "$mode/checks.json" || code=$?
    jq -es 'length==1 and (.[0].checks|type=="object" and length>0 and all(.[];(.pass_|type)=="boolean"))' "$mode/checks.json" >/dev/null
    if jq -e 'all(.checks[];.pass_)' "$mode/checks.json" >/dev/null; then
        (( code == 0 )) || exit 2
        cp "$mode/checks.json" checks.json
        cp "$candidate" adapter.json
        break
    fi
done
[[ -f checks.json ]] || { echo 'No generated variant passed the task checks' >&2; exit 1; }
echo '{"passed":true,"completed_at":"generated_candidate"}' > result.json
"$trial/verify-repair.sh" "$work" "$suite" "$header" "$client" "$work/receipt.json" "$call"

# Prepare the new generation off to the side. The original bundle and run remain usable after a failure.
staging=$(mktemp -d -- "$(dirname "$dest")/.qualifying-XXXXXXXX")
trap 'rm -rf -- "$staging"' EXIT
cp -aL --reflink=auto "$base/." "$staging/"
cd "$staging"
[[ ! -e headers/$header ]]
jq -e --arg id "$id" 'all(.[]; .id!=$id)' catalog.json >/dev/null
jq -e --slurpfile a "$candidate" 'all(.providers[]; .id!=$a[0].id and (.effect!=null or .from!=$a[0].from or .to!=$a[0].to))' providers/contracts.json >/dev/null
[[ -f trained-catalog.json ]] || cp catalog.json trained-catalog.json
mkdir -p "qualifications/$id"
cp "$work/bricks/$header" "headers/$header"
cp "$client" "qualifications/$id/client.cpp"
cp "$work/receipt.json" "qualifications/$id/"
jq 'del(.source)' "$candidate" > "qualifications/$id/adapter.json"
jq -n --slurpfile a "$candidate" '$a[0]|{id,description,outputs:[.description],tags:[]}' > "$work/card.json"
jq --slurpfile c "$work/card.json" '.+$c' catalog.json > "$work/catalog.json"; cp "$work/catalog.json" catalog.json
jq --slurpfile a "$candidate" --slurpfile c "$work/card.json" '.+[$c[0]+($a[0]|{header,packages:[.package],origin:"generated_gir",source:("headers/"+.header),
    provenance:{symbol,metadata_sha256}})]' components.json > "$work/components.json"; cp "$work/components.json" components.json
jq --slurpfile a "$candidate" '.providers += [$a[0]|{id,brick:.id,from,to,header,call}]' providers/contracts.json > "$work/contracts.json"; cp "$work/contracts.json" providers/contracts.json
jq --slurpfile c catalog.json '.catalog=$c[0]' reviews.json > "$work/reviews.json"; cp "$work/reviews.json" reviews.json
if jq -e 'has("base_outcomes")' providers/contracts.json >/dev/null; then python3 "$trial/expanded/index-formats.py" providers/contracts.json; fi
"$trial/verify-bundle.sh" "$staging"
exec {lock}<"$(dirname "$dest")"; flock "$lock"
[[ ! -e $dest && ! -L $dest ]]
mv -T -- "$staging" "$dest"
