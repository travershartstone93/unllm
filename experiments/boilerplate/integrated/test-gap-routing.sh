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
if [[ $PWD == */edit/empty && ${FIXIT_CASE:-} == link_failure ]]; then
    # Apply a range edit only if the editor received the original, unfixed line.
    jq -ce '(.["project/converter.cpp"] | index("int fixit_probe = external_fixit_symbol()") // empty) + 1 as $line |
        {edits:[{file:"project/converter.cpp",start:$line,end:$line,lines:["int fixit_probe = 0;"]}],add_packages:[]} |
        {type:"text", part:{text:tojson}}' ../../../edit-files.json
    exit
fi
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

# Let Clang fix a missing semicolon. In one case the candidate still fails to link.
export CHECK_CASE=A REFERENCE="$prior/run-A" ORIGINAL_B19="$CORPUS_ROOT/experiments/boilerplate-B19"
fixture="$work/corpus/experiments"
mkdir -p "$fixture/boilerplate-B19"
ln -s "$CORPUS_ROOT/experiments/boilerplate-B16" "$fixture/boilerplate-B16"
ln -s "$ORIGINAL_B19"/{bricks,library.txt,compiler-repair} "$fixture/boilerplate-B19/"
cat > "$fixture/boilerplate-B19/emit" <<'SH'
#!/bin/bash
set -eu
"$ORIGINAL_B19/emit" "$@"
if [[ $3 == project ]]; then
    if [[ $FIXIT_CASE == link_failure ]]; then
        printf '%s\n' 'extern int external_fixit_symbol();' 'int fixit_probe = external_fixit_symbol()' >> project/converter.cpp
    else
        echo 'int fixit_probe = 0' >> project/converter.cpp
    fi
fi
SH
chmod +x "$fixture/boilerplate-B19/emit"
export CORPUS_ROOT="$work/corpus" BRICK_EDITOR=on
for FIXIT_CASE in link_failure success; do
    export FIXIT_CASE
    out="$work/fixit-$FIXIT_CASE"
    "$pilot/run.sh" "$REFERENCE/request.txt" "$out" "$pilot/convert-check.sh"
    if [[ $FIXIT_CASE == link_failure ]]; then
        jq -e '.passed and .completed_at=="model_edit"' "$out/result.json" >/dev/null
        jq -r '."project/converter.cpp"[]' "$out/edit-files.json" | cmp - "$out/before-fixit.cpp"
        grep -Fxq 'int fixit_probe = external_fixit_symbol();' "$out/project/fixed.cpp"
        grep -q 'external_fixit_symbol' "$out/fixit-build.log"
        jq -ne --rawfile prompt "$out/calls/edit/prompt.txt" --rawfile rejected "$out/fixit-build.log" \
            '$prompt | contains($rejected) | not' >/dev/null
    else
        jq -e '.passed and .completed_at=="compiler_fixit"' "$out/result.json" >/dev/null
        grep -Fxq 'int fixit_probe = 0;' "$out/project/converter.cpp"
        test ! -d "$out/calls/edit"
    fi
done
