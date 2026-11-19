#!/bin/bash
# Adapter for the original, independent B16 acceptance harness.
set -euo pipefail
: "${CHECK_CASE:?Select A (JPG) or B (PDF) for the evaluation harness only}"
[[ $CHECK_CASE == A || $CHECK_CASE == B ]]
archive=${CORPUS_ROOT:-/run/media/whisper/UNLLM_CORPUS/unllm}/experiments/boilerplate-B16
program=$(realpath "$1"); dir=$2
mkdir -p "$dir/programs/$CHECK_CASE-suite"
ln -s "$program" "$dir/programs/$CHECK_CASE-suite/converter"
ln -s "$archive/fixtures" "$dir/fixtures"
(cd "$dir" && python3 "$archive/validate.py" "$CHECK_CASE-suite" run) >&2
cat "$dir/checks/run/$CHECK_CASE-suite/report.json"
