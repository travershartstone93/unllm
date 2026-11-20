#!/bin/bash
# Runs inside the request directory after a header deliverable has been interpreted.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
bundle=$1
spec=calls/interpret/response.json
repo=$(cd "$here/../../.." && pwd)
root=${CORPUS_ROOT:-/run/media/whisper/UNLLM_CORPUS/unllm}
if ! jq -ec '.responsibilities|select(type=="array" and length>0 and all(.[];(.id|type)=="string" and (.text|type)=="string"))' "$spec" > responsibilities.json; then exit 2; fi
annotator=${ANNOTATOR:-"$root/experiments/boilerplate-B16/replay-tools/brick-requirements"}
"$annotator" "$root/experiments/boilerplate-B16/replay-tools/requirement-terms.json" "${UDPIPE_MODEL:?}" < responsibilities.json > component-roles.json
BGE_DIR="${BGE_DIR:-$HOME/.local/share/unllm-models/bge-small-en-v1.5}" "$here/../component-trial/select.sh" "$bundle" component-roles.json components "${BRICK_COMPONENT_MODE:-trained}"
"${BRICK_HEADER_EMITTER:-$repo/build/native/brick-header-emit}" "$bundle" "$spec" components/helpers.json > components/header.json
name=$(jq -er .filename components/header.json)
mkdir project
jq -r .source components/header.json > "project/$name"
printf '#include "%s"\nint main() {}\n' "$name" | c++ -std=c++17 -Iproject -x c++ - -o project/header-smoke
