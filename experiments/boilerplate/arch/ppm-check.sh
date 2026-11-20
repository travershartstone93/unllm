#!/bin/bash
# Independent JPEG decoding oracle using ImageMagick. The executable receives paths only.
set -euo pipefail
program=$(realpath "$1")
out=$(realpath "$2")
fixtures=${CORPUS_ROOT:-/run/media/whisper/UNLLM_CORPUS/unllm}/experiments/boilerplate-B16/fixtures
test -r "$fixtures/wide source.jpg" && test -r "$fixtures/upright.jpg"
cd "$out"
for name in 'wide source' upright; do
    src="$fixtures/$name.jpg"
    dest="$name.ppm"
    passed=false
    if timeout 15 "$program" "$src" "$dest" && [[ $(head -c2 "$dest") == P6 ]] &&
       [[ $(magick identify -format '%wx%h' "$dest") == "$(magick identify -format '%wx%h' "$src")" ]]; then
        # Normalized RMSE from two decoders, allowing a small rounding difference.
        if magick compare -metric RMSE "$src" "$dest" null: 2>metric; then rc=0; else rc=$?; fi
        if (( rc <= 1 )) && awk -F '[()]' 'NF>=3 && $2 ~ /^[0-9.eE+-]+$/ && $2+0<0.02 {ok=1} END{exit !ok}' metric; then passed=true; fi
    fi
    jq -n --arg name "$name" --argjson pass "$passed" '{($name):{pass_:$pass}}'
done > rows
passed=false
if ! timeout 15 "$program" missing.jpg invalid.ppm && [[ ! -e invalid.ppm ]]; then passed=true; fi
jq -n --argjson pass "$passed" '{"missing input":{pass_:$pass}}' >> rows
passed=false
printf sentinel > occupied.ppm
if ! timeout 15 "$program" "$fixtures/upright.jpg" occupied.ppm && [[ $(cat occupied.ppm) == sentinel ]]; then passed=true; fi
jq -n --argjson pass "$passed" '{"existing output":{pass_:$pass}}' >> rows
jq -s '{checks:add}' rows
