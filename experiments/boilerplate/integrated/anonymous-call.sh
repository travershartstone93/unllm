#!/bin/bash
# Run one keyless hosted attempt. Emit the text event consumed by opencode-call.sh.
set -euo pipefail
provider=${1%%/*}
model=${1#*/}
call=$2
cd "$3"
http() {
    local label=$1 limit=$2
    shift 2
    curl -q --silent --show-error --fail-with-body --connect-timeout 10 --max-time "$limit" \
        --output "$label.json" --write-out '%{http_code}\n' "$@" > "$label.http"
}
payload() {
    local budget=$1 effort=$2 options=$3
    jq -cn --arg model "$model" --argjson budget "$budget" --arg effort "$effort" \
        --argjson options "$options" --rawfile prompt "$call/prompt.txt" \
        '{model:$model,messages:[{role:"user",content:$prompt}],temperature:0,max_tokens:$budget,
          reasoning_effort:$effort,response_format:{type:"json_object"}} + $options' > payload.json
}
case "$provider" in
    ovhcloud)
        budget=1500
        reply_timeout=90
        if [[ ${call##*/} == edit ]]; then
            budget=4096
            reply_timeout=180
        fi
        payload "$budget" low '{}'
        http reply "$reply_timeout" -H 'Content-Type: application/json' --data-binary @payload.json \
            https://oai.endpoints.kepler.ai.cloud.ovh.net/v1/chat/completions
        jq -ce '.choices[0] | select(.finish_reason=="stop" and (.message.tool_calls|length)==0) |
            {type:"text",part:{text:(.message.content|strings)}}' reply.json
        ;;
    aihorde)
        endpoint=https://aihorde.net/api/v2/generate/chat
        headers=(-H 'apikey: 0000000000' -H 'Client-Agent: unllm-interpreter-pilot:0.1:anonymous-cli')
        # Anonymous workers can require prepaid kudos for a large output reservation.
        budget=512
        [[ ${call##*/} != edit ]] || budget=1024
        payload "$budget" none '{"horde":{"context_fit":"grow","availability_mode":"fail_fast"}}'
        http submit 25 "${headers[@]}" -H 'Content-Type: application/json' --data-binary @payload.json "$endpoint/async"
        id=$(jq -er '.id | strings | select(test("^[0-9a-fA-F-]{36}$"))' submit.json)
        url="$endpoint/status/$id"
        # Cancel an outstanding job on a timeout, transport failure or interruption.
        trap 'http cancel 10 "${headers[@]}" -X DELETE "$url" || true' EXIT
        trap 'exit 130' INT
        trap 'exit 143' TERM
        deadline=$((SECONDS + 360))
        while (( SECONDS < deadline )); do
            http status 15 "${headers[@]}" "$url"
            if jq -e '.done or .faulted' status.json >/dev/null; then
                trap - EXIT
                jq -ce --arg model "$model" 'select(.done and (.faulted|not)) | .generations[0] |
                    select(.model==$model and .finish_reason=="stop" and (.message.tool_calls|length)==0) |
                    {type:"text",part:{text:(.message.content|strings)}}' status.json
                exit
            fi
            sleep 3
        done
        echo 'Timed out waiting for AI Horde' >&2
        exit 1
        ;;
    *) exit 2 ;;
esac
