#!/bin/bash
# Experimental offline interpreter. Stage-five editing still uses the configured normal editor.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
library=/dev/null
[[ ! -f $BRICK_COMPONENT_CATALOG/providers/library.json ]] || library=$BRICK_COMPONENT_CATALOG/providers/library.json
jq --slurpfile providers "$BRICK_COMPONENT_CATALOG/providers/contracts.json" --slurpfile adapters "$here/adapters.json" --slurpfile runtime "$here/cli-guarantees.json" --slurpfile library "$library" -f "$here/interpret-schema.jq" vocabulary.json > calls/interpret/schema.json
jq -n --arg model "$BRICK_INTERPRETER_OLLAMA" --rawfile prompt calls/interpret/prompt.txt --slurpfile grammar calls/interpret/schema.json \
    '{model:$model,prompt:$prompt,format:$grammar[0],think:false,stream:false,options:{temperature:0,seed:42,num_ctx:8192,num_predict:1300}}' |
    curl --silent --show-error --fail --max-time 180 -H 'Content-Type: application/json' --data-binary @- http://127.0.0.1:11434/api/generate > calls/interpret/ollama.json
jq -e 'select(.done==true and .done_reason!="length")|.response|fromjson |
    if (.roles|type)=="array" then .roles |= map(if .output_type=="other" then
        if (.requested_output_type|type)=="string" and (.requested_output_type|test("^[a-zA-Z0-9.+-]+/[a-zA-Z0-9.+_-]+$")) then .output_type=.requested_output_type|del(.requested_output_type)
        else error("Missing custom output media type") end else . end) else . end' calls/interpret/ollama.json > calls/interpret/response.json
