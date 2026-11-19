#!/bin/bash
# Replay model answers to exercise the real runner for full and partial deterministic coverage.
set -euo pipefail
pilot=$(cd -- "$(dirname -- "$0")" && pwd)
export CORPUS_ROOT=${CORPUS_ROOT:-/run/media/whisper/UNLLM_CORPUS/unllm}
prior="$CORPUS_ROOT/experiments/boilerplate-B20"
: "${PICKER:?Set PICKER to a native brick-picker with evidence-bge support}"
: "${UDPIPE_MODEL:?Set UDPIPE_MODEL to the English EWT model}"
export ORIGINAL_PICKER="$PICKER"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
export PICKER="$work/picker" PATH="$work:$PATH"
cat > "$work/opencode" <<'SH'
#!/bin/bash
set -eu
[[ $PWD == */interpret/empty ]]
cat "$REFERENCE/calls/interpret/events.jsonl"
SH
cat > "$PICKER" <<'SH'
#!/bin/bash
set -eu
case $1 in
  evidence-bge)
    if [[ $CHECK_CASE == B ]]; then
      # Pin only the proposed bindings to force a mixed-role control.
      jq -n '{bindings:{r1:"r28",r2:null},selected:["r28"],missing:["r2"]}'
    else
      cat "$REFERENCE/proposals.json"
    fi ;;
  ollama)
    [[ $CHECK_CASE == B ]]
    jq -e 'map(.id)==["r2"]' "$3" >/dev/null
    jq -n '{bindings:{r2:"h65"},selected:["h65"],missing:[]}' ;;
  resolve) exec "$ORIGINAL_PICKER" "$@" ;;
  *) exit 2 ;;
esac
SH
chmod +x "$work/opencode" "$PICKER"
for case in A B; do
    export CHECK_CASE=$case REFERENCE="$prior/run-$case"
    out="$work/$case"
    "$pilot/run.sh" "$REFERENCE/request.txt" "$out" "$pilot/convert-check.sh"
    jq -e '.passed and (.stage5_used|not)' "$out/result.json" >/dev/null
    if [[ $case == A ]]; then
        test ! -e "$out/qwen.json"
        jq -e 'length==0' "$out/unfilled.json" >/dev/null
    else
        jq -e '.bindings=={r1:"r28",r2:"h65"}' "$out/selection.json" >/dev/null
    fi
done
