#!/bin/bash
# Download one HuggingFace model repo over IPv4 (this host's IPv6 route to HF is dead).
# Usage: hf_get.sh ORG/NAME   -> ~/Downloads/unllm-models/hf/NAME
# Takes config, tokenizer and safetensors weights (pytorch_model.bin only when no safetensors).
set -euo pipefail
repo=$1
out=~/Downloads/unllm-models/hf/${repo#*/}
mkdir -p "$out"
files=$(curl -4 -sfL "https://huggingface.co/api/models/$repo" | jq -r '.siblings[].rfilename')
want=$(grep -E '^[^/]+\.(json|txt|model|safetensors)$|^[^/]+merges\.txt$' <<<"$files" || true)
grep -q '\.safetensors$' <<<"$want" || want+=$'\n'$(grep -E '^pytorch_model\.bin$' <<<"$files" || true)
for f in $want; do
  [ -s "$out/$f" ] && continue
  curl -4 -sfL --retry 3 -o "$out/$f.part" "https://huggingface.co/$repo/resolve/main/$f" && mv "$out/$f.part" "$out/$f"
done
echo "$repo -> $out ($(du -sh "$out" | cut -f1))"
