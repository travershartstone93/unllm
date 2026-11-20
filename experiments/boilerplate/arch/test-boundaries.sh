#!/bin/bash
# Requires a successful live JPEG-to-PPM run and its installed verified package.
set -euo pipefail
here=$(cd -- "$(dirname -- "$0")" && pwd)
saved=$(realpath "${1:?pass the successful PPM run directory}")
catalog=${ARCH_PROGRAM_CATALOG:-"${CORPUS_ROOT:-/run/media/whisper/UNLLM_CORPUS/unllm}/arch-programs/catalog"}
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
spec="$saved/calls/interpret/response.json"
candidate="$saved/arch/extra-libjpeg-turbo/candidate.json"
export SAVED="$saved" PATH="$work:$PATH"
# Put the same name and terms in both sources, plus an unreviewed official match.
mkdir "$work/catalog"
ln -s "$catalog/catalog" "$work/catalog/catalog"
jq -r '.[]|[.package,.terms]|@tsv' "$here/profiles.json" > "$work/profiles"
printf '%s\n' $'aur\tlibjpeg-turbo\t1\tconvert jpeg ppm' $'extra\tlibjpeg-turbo\t1\tconvert jpeg ppm' \
    $'extra\tunknown-tool\t1\tconvert jpeg ppm' | "$catalog/catalog" index "$work/catalog/index" "$work/profiles"
"$here/discover.sh" "$work/catalog" "$spec" > "$work/leads"
jq -e '(length==3) and all(.[]; if .repo=="aur" then .profile==null and .disposition=="review_only"
    elif .package=="unknown-tool" then .profile==null else .disposition=="check_provenance" end)' "$work/leads" >/dev/null
jq '.roles += [(.roles[0] | .id="second")]' "$spec" > "$work/partial"
"$here/discover.sh" "$work/catalog" "$work/partial" | jq -e 'length==3 and all(.[];.profile==null)' >/dev/null
mkdir "$work/probe"
for change in '.repo="aur"' '.version="mismatched-version"' '.profile.argv=["unreviewed"]'; do
    jq "$change" "$candidate" > "$work/altered"
    if "$here/prepare.sh" "$work/altered" "$work/probe" >/dev/null 2>&1; then exit 1; fi
    test ! -e "$work/probe/converter"
done

# A real signed executable must still pass the supplied evaluator. Stop the brick
# picker on entry to isolate routing after the deliberate acceptance failure.
cat > "$work/opencode" <<'SH'
#!/bin/bash
set -eu
cmp ../prompt.txt "$SAVED/calls/interpret/prompt.txt"
cat "$SAVED/calls/interpret/events.jsonl"
SH
cat > "$work/evaluator" <<'SH'
#!/bin/bash
if [[ $CONTROL == reject ]]; then echo '{"checks":{"control":{"pass_":false}}}'; else echo '{}'; fi
SH
cat > "$work/brick-picker" <<'SH'
#!/bin/bash
[[ $1 == evidence-bge ]] || exit 90
exit 71
SH
chmod +x "$work"/{opencode,evaluator,brick-picker}
export PICKER="$work/brick-picker"
for control in reject malformed; do
    export CONTROL="$control"
    rc=0
    "$here/../integrated/run.sh" "$saved/request.txt" "$work/$control" "$work/evaluator" || rc=$?
    test ! -e "$work/$control/result.json"
    if [[ $control == reject ]]; then
        [[ $rc == 71 ]]
        jq -es 'any(.[];.stage=="arch_candidate_failed") and any(.[];.stage=="deterministic_picker")' "$work/$control/steps.jsonl" >/dev/null
    else
        [[ $rc == 2 ]]
        jq -es 'last.stage=="harness_error" and all(.[];.stage!="deterministic_picker")' "$work/$control/steps.jsonl" >/dev/null
    fi
done
echo 'Source isolation, partial coverage, package provenance, failed checks and malformed evaluator controls passed.'
