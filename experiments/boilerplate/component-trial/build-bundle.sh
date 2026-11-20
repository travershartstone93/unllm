#!/bin/bash
set -euo pipefail
here=$(cd -- "$(dirname -- "$0")" && pwd)
if (( $# < 2 || $# > 3 )); then echo "Usage: build-bundle.sh B16_DIRECTORY NEW_DIRECTORY [50|75|100]" >&2; exit 2; fi
original=$(realpath "$1")
destination=$(realpath -m "$2")
[[ ! -e $destination ]] || { echo "Destination already exists" >&2; exit 2; }
bundle=$(mktemp -d -- "$(dirname "$destination")/.building-XXXXXXXX")
trap 'rm -rf -- "$bundle"' EXIT
cp -R "$here/providers" "$bundle/providers"
cp "$here/components.json" "$bundle/components.json"
jq --slurpfile c "$here/components.json" '. + [$c[0][]|{id,description,outputs,tags:[]}]' "$original/catalog.json" > "$bundle/catalog.json"
jq --slurpfile c "$bundle/catalog.json" '.catalog=$c[0]' "$original/reviews.json" > "$bundle/reviews.json"
mkdir "$bundle/headers"
while IFS=$'\t' read -r name source; do
    cp "$here/$source" "$bundle/headers/$name"
done < <(jq -r '.[] | [.header,.source]|@tsv' "$here/components.json")
size=${3:-100}
case $size in
  50) ;;
  75|100) python3 "$here/expanded/register.py" "$bundle" "$((size-50))" ;;
  *) echo "Supported catalog sizes: 50, 75, 100" >&2; exit 2 ;;
esac
if [[ $size != 50 ]]; then python3 "$here/expanded/index-formats.py" "$bundle/providers/contracts.json"; fi
"$here/verify-bundle.sh" "$bundle"
training="$bundle/catalog.json"
[[ ! -e $bundle/trained-catalog.json ]] || training="$bundle/trained-catalog.json"
python3 "$here/train.py" "$training" "$bundle/reviews.json" \
    "${EMBEDDER:-$here/../../../build/unllm_meaning}" "${BGE_DIR:-$HOME/.local/share/unllm-models/bge-small-en-v1.5}" "$bundle/model.json"
# Serialize publication into this parent to keep concurrent attempts independent.
exec {parent}<"$(dirname "$destination")"
flock "$parent"
[[ ! -e $destination ]] || exit 2
mv -T -- "$bundle" "$destination"
