#!/usr/bin/env bash
set -euo pipefail
dir=$(realpath "${1:?supply existing experiment directory containing levels/}")
bin=$(realpath "${2:?supply the native build directory}")
here=$(cd "$(dirname "$0")" && pwd)
(cd "$here" && sha256sum -c FROZEN.sha256 && sha256sum -c LEVELS.sha256)
mkdir "$dir/predictions"
arms=(stock none curated 512 8192 full placebo reuse-curated reuse-full)
for round in 0 1 2; do
  for ((i=0;i<${#arms[@]};i++)); do
    arm=${arms[$(((i+round*3)%${#arms[@]}))]}
    case "$arm" in
      stock|none) index=$arm ;;
      reuse-*) index="reuse:$dir/levels/${arm#reuse-}.json" ;;
      placebo) index="placebo:$dir/levels/full.json" ;;
      *) index="$dir/levels/$arm.json" ;;
    esac
    "$bin/corpus_ablation_trial" "$index" "$here/requests.json" > "$dir/predictions/$round-$arm.jsonl"
    [[ $(wc -l < "$dir/predictions/$round-$arm.jsonl") == 16 ]]
  done
done
(cd "$dir" && sha256sum predictions/*.jsonl > predictions.sha256)
