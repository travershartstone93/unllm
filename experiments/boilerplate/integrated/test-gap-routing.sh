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
    test ! -e "$out/qwen.json"
    if [[ $case == A ]]; then
        jq -e 'length==0' "$out/unfilled.json" >/dev/null
    else
        jq -e '.bindings=={r1:"r28",r2:null}' "$out/selection.json" >/dev/null
        jq -e '[.rows[].adapter_id]==["r28","h65"]' "$out/route.json" >/dev/null
    fi
done

# A MIME match alone cannot choose between competing adapters or satisfy extra operations.
jq '. + {duplicate:.h65}' "$pilot/adapters.json" > "$work/ambiguous.json"
spec="$out/calls/interpret/response.json"
jq '.roles[1].requirement={operations:[{outcome:"sort"}]}' "$spec" > "$work/extra.json"
for control in ambiguous extra; do
    adapters="$pilot/adapters.json"; input="$spec"
    if [[ $control == ambiguous ]]; then adapters="$work/ambiguous.json"; else input="$work/extra.json"; fi
    jq --slurpfile proposal "$out/proposals.json" --slurpfile selection "$out/selection.json" \
        --slurpfile adapters "$adapters" -f "$pilot/route.jq" "$input" |
        jq -e '.rows[0].bound and (.rows[1].bound|not)' >/dev/null
done
