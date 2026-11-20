#!/bin/bash
# Select partial implementations for independent responsibilities.
set -euo pipefail
bundle=$1 roles=$2 out=$3 mode=$4
mkdir "$out"
catfile="$bundle/catalog.json"
case "$mode" in
 trained)
    training="$catfile"
    [[ ! -e "$bundle/trained-catalog.json" ]] || training="$bundle/trained-catalog.json"
    "$PICKER" trained "$training" "$roles" "$bundle/model.json" "$BGE_DIR" > "$out/selection.json"
    ;;
 current)
    "$PICKER" evidence-bge "$catfile" "$roles" "$bundle/reviews.json" "$BGE_DIR" > "$out/selection.json"
    ;;
 *) echo "Unknown component selector: $mode" >&2; exit 2 ;;
esac
jq --slurpfile picks "$out/selection.json" '[.[]|select(.id as $id|$picks[0].selected|index($id))]' \
    "$bundle/components.json" > "$out/helpers.json"
