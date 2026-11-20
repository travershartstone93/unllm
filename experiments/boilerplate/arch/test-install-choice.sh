#!/bin/bash
# Exercise the integrated UI with a pseudo-terminal, replayed interpretations and a fake installer.
set -euo pipefail
here=$(cd -- "$(dirname -- "$0")" && pwd)
root=${CORPUS_ROOT:-/run/media/whisper/UNLLM_CORPUS/unllm}
jpg="$root/experiments/boilerplate-B21/live-A"
ppm="$root/experiments/boilerplate-B22/live-ppm"
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
mkdir "$work/bin"
export PATH="$work/bin:$PATH" CHECK_CASE=A RUNNER="$here/../integrated/run.sh"
cat > "$work/bin/opencode" <<'SH'
#!/bin/bash
set -eu
cmp ../prompt.txt "$SAVED/calls/interpret/prompt.txt"
cat "$SAVED/calls/interpret/events.jsonl"
SH
cat > "$work/bin/pacman" <<'SH'
#!/bin/bash
if [[ $1 == -Qq && $3 == "$PACKAGE" && ! -f $RECEIPT ]]; then exit 1; fi
exec /usr/bin/pacman "$@"
SH
cat > "$work/bin/sudo" <<'SH'
#!/bin/bash
set -eu
[[ $1 == -- && $2 == /usr/bin/bash && $3 == -p && $4 == -c && $6 == unllm-install ]]
[[ $7 == "extra/$PACKAGE="* ]]
printf '%s\n' "$7" > "$RECEIPT"
[[ $MODE == install ]] || exit 1
# Check the actual generated config and package arguments in pacman's print-only mode.
plan=${5/'pacman --config "$cfg" -S --needed -- "$1"'/'pacman --config "$cfg" -Sp --print-format "%r/%n" -- "$1"'}
[[ $plan != "$5" ]]
/usr/bin/bash -c "$plan" "$6" "$7" > "$RECEIPT.plan"
SH
chmod +x "$work/bin/"*
[[ $(command -v sudo) == "$work/bin/sudo" ]]
for MODE in build default unattended failed install; do
    export MODE RECEIPT="$work/$MODE.receipt" OUT="$work/$MODE"
    export SAVED="$jpg" PACKAGE=img2pdf SUITE="$here/../integrated/convert-check.sh"
    if [[ $MODE == install ]]; then
        export SAVED="$ppm" PACKAGE=libjpeg-turbo SUITE="$here/ppm-check.sh"
    fi
    if [[ $MODE == unattended ]]; then
        "$RUNNER" "$SAVED/request.txt" "$OUT" "$SUITE" < /dev/null > "$OUT.log" 2>&1
        ! grep -Fq 'or build from scratch?' "$OUT.log" || exit 1
    else
        case $MODE in
            build) reply='build from scratch' ;;
            default) reply='' ;;
            *) reply=install ;;
        esac
        # The child shell expands the exported paths inside its terminal.
        # shellcheck disable=SC2016
        printf '%s\n' "$reply" | script -qec 'exec "$RUNNER" "$SAVED/request.txt" "$OUT" "$SUITE"' /dev/null > "$OUT.log"
        grep -Fq "install package $PACKAGE from repo extra or build from scratch?" "$OUT.log"
    fi
    case $MODE in
        install)
            grep -Fxq "extra/$PACKAGE" "$RECEIPT.plan"
            jq -e '.passed and .completed_at=="official_arch"' "$OUT/result.json" >/dev/null
            jq -es 'any(.[];.stage=="arch_install_completed")' "$OUT/steps.jsonl" >/dev/null ;;
        *)
            jq -e '.passed and .completed_at=="deterministic"' "$OUT/result.json" >/dev/null
            if [[ $MODE == failed ]]; then
                test -s "$RECEIPT"
                jq -es 'any(.[];.stage=="arch_install_failed")' "$OUT/steps.jsonl" >/dev/null
            else
                test ! -e "$RECEIPT"
                stage=arch_build_selected
                if [[ $MODE == unattended ]]; then stage=arch_install_skipped; fi
                jq -es --arg stage "$stage" 'any(.[];.stage==$stage)' "$OUT/steps.jsonl" >/dev/null
            fi ;;
    esac
    echo "$MODE passed"
done
