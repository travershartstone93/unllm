#!/bin/bash
# Exercise provider ordering and exhaustion without spending hosted quota.
set -euo pipefail
pilot=$(cd "$(dirname "$0")" && pwd)
work=$(mktemp -d)
trap 'rm -r -- "$work"' EXIT
mkdir "$work/bin"
export PATH="$work/bin:$PATH" BRICK_INTERPRETER_OLLAMA=qwen3:8b BRICK_OPENCODE_MODEL=ollama/qwen3:8b
cat > "$work/bin/opencode" <<'SH'
#!/bin/bash
set -eu
if [[ $1 == models ]]; then
    printf '%s\n' ollama/test-free opencode/iceman-free opencode/test-free opencode/test-paid opencode/test-free opencode/next-free
    exit 0
fi
[[ $1 == run && $2 == --model && $4 == --format && $5 == json && $6 == 'Test prompt' ]]
echo "$3" >> "$TRACE"
if [[ $3 == "$SUCCESS" ]]; then
    echo '{"type":"text","part":{"text":"{\"ok\":true}"}}'
elif [[ $SUCCESS == tool ]]; then
    echo '{"type":"tool_use"}'
elif [[ $3 == opencode/big-pickle ]]; then
    if [[ $SUCCESS == no-quota ]]; then exit 1; fi
    echo '{"type":"error","error":{"data":{"statusCode":429}}}'
elif [[ $3 == opencode/iceman-free ]]; then
    exit 1
else
    echo '{"type":"text","part":{"text":"invalid JSON"}}'
fi
SH
cat > "$work/bin/curl" <<'SH'
#!/bin/bash
set -eu
while [[ $1 != --output ]]; do shift; done
out=$2
case "${!#}" in
    https://oai.endpoints.kepler.ai.cloud.ovh.net/v1/chat/completions)
        echo ovhcloud >> "$TRACE"
        jq -e '.model=="gpt-oss-120b" and .messages[0].content=="Test prompt\n"' payload.json >/dev/null
        if [[ $SUCCESS == ovhcloud ]]; then
            echo '{"choices":[{"finish_reason":"stop","message":{"content":"{\"ok\":true}"}}]}' > "$out"
        elif [[ $SUCCESS == wrong-shape ]]; then
            echo '{"choices":[{"finish_reason":"stop","message":{"content":"{\"ok\":true}"}}]}' > "$out"
        elif [[ $SUCCESS == malformed ]]; then
            echo '{"choices":[{"finish_reason":"stop","message":{"content":"invalid JSON"}}]}' > "$out"
        else echo '429'; exit 22; fi ;;
    https://aihorde.net/api/v2/generate/chat/async)
        echo aihorde >> "$TRACE"
        jq -e '.model=="Qwen/Qwen3.8-Flash-Next" and .messages[0].content=="Test prompt\n"' payload.json >/dev/null
        echo '{"id":"12345678-1111-2222-3333-999999999999"}' > "$out" ;;
    https://aihorde.net/api/v2/generate/chat/status/12345678-1111-2222-3333-999999999999)
        if [[ $out == cancel.json ]]; then echo cancelled > "$CANCEL"; echo '{}' > "$out"
        elif [[ $SUCCESS == exhausted ]]; then echo '503'; exit 22
        elif [[ $SUCCESS == wrong-shape ]]; then
            jq -cn --arg content "$VALID" '{done:true,generations:[{model:"Qwen/Qwen3.8-Flash-Next",
                finish_reason:"stop",message:{content:$content}}]}' > "$out"
        else echo '{"done":true,"generations":[{"model":"Qwen/Qwen3.8-Flash-Next","finish_reason":"stop","message":{"content":"{\"ok\":true}"}}]}' > "$out"; fi ;;
    *) exit 88 ;;
esac
echo 200
SH
chmod +x "$work/bin/opencode" "$work/bin/curl"
pool=(opencode/big-pickle opencode/iceman-free opencode/test-free opencode/next-free)
for success in "${pool[@]}" ovhcloud aihorde malformed exhausted no-quota tool; do
    call="$work/${success##*/}"
    mkdir "$call"
    echo 'Test prompt' > "$call/prompt.txt"
    export SUCCESS="$success" TRACE="$call/trace" CANCEL="$call/cancelled"
    rc=0
    bash "$pilot/opencode-call.sh" "$call" 2> "$call/error" || rc=$?
    expected=()
    for model in "${pool[@]}"; do
        expected+=("$model")
        if [[ $model == "$success" || $success == tool ]]; then break; fi
    done
    case $success in
        ovhcloud) expected+=(ovhcloud);;
        aihorde|malformed|exhausted) expected+=(ovhcloud aihorde);;
    esac
    diff -u <(printf '%s\n' "${expected[@]}") "$TRACE"
    if [[ $success == opencode/* || $success == ovhcloud || $success == aihorde || $success == malformed ]]; then
        case $success in
            ovhcloud) winner=ovhcloud/gpt-oss-120b ;;
            aihorde|malformed) winner=aihorde/Qwen/Qwen3.8-Flash-Next ;;
            *) winner=$success ;;
        esac
        [[ $rc == 0 && $(<"$call/model.txt") == "$winner" ]]
        jq -e '.ok==true' "$call/response.json" >/dev/null
    else
        [[ $rc != 0 && ! -e $call/response.json ]]
    fi
    if [[ $success == exhausted ]]; then test -f "$CANCEL"; else test ! -e "$CANCEL"; fi
done
export VALID
for stage in interpret responsibilities edit; do
    call="$work/wrong-shape/$stage"
    mkdir -p "$call"
    echo 'Test prompt' > "$call/prompt.txt"
    case $stage in
        interpret) VALID='{"input_type":"text/plain","roles":[{"id":"out","text":"copy file","output_type":"text/plain","multiple":false,"content":"whole"}]}' ;;
        responsibilities) VALID='{"responsibilities":[{"id":"parse","text":"parse text"}]}' ;;
        edit) VALID='{"edits":[],"add_packages":[]}' ;;
    esac
    export SUCCESS=wrong-shape TRACE="$call/trace" CANCEL="$call/cancelled"
    bash "$pilot/opencode-call.sh" "$call"
    diff -u <(printf '%s\n' "${pool[@]}" ovhcloud aihorde) "$TRACE"
    jq -e --argjson expected "$VALID" '.==$expected' "$call/response.json" >/dev/null
done
echo 'All thirteen hosted routing controls passed.'
