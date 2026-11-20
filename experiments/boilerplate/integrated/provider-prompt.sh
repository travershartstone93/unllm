#!/bin/bash
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
bundle=$1
"$here/vocabulary.sh" "$bundle/providers/contracts.json" request.txt > vocabulary.json
profile=$(jq -er '.profile' vocabulary.json)
if [[ $profile == single ]]; then cat "$here/interpret-provider.txt"; else cat "$here/interpret-sequence.txt"; fi
cat <<'FIELDS'
For content, use "whole" when the output is the complete transformed result. Use "text", "pages", "embedded_images", "metadata", "audio" or "video" for extracting that part of a source document, and "other" for an unlisted part. The output MIME type describes the result's encoding.
Include an operation for a requested change or independently requested outcome. Ordinary reading and writing are implicit. Use the role's text to describe the affected collection, what changes, and what must be preserved.
FIELDS
jq -c '{runtime:.}' "$here/cli-guarantees.json"
jq -c 'del(.profile)' vocabulary.json
# Present literal media identifiers separately from catalog examples. The interpreter still assigns their roles.
explicit=$(jq -Rrs '[scan("\\b(?:application|audio|font|image|message|model|multipart|text|video)/[A-Za-z0-9.+_-]*[A-Za-z0-9_-]")]|unique|join(" ")' request.txt)
if [[ -n $explicit ]]; then
    printf 'Media types explicitly named in this request: %s. Assign each to the file it describes. Preserve its exact subtype even when it is absent from the catalog.\n' "$explicit"
fi
if [[ $profile != single && -f $bundle/providers/library.json ]]; then
    cat "$here/header-instructions.txt"
    jq -c '([.classes[].methods]|add) as $methods |
            {type:"object",required:["kind","filename","class_name","value_type","methods","uncovered"],
             properties:{kind:{const:"header"},filename:{type:"string"},class_name:{type:"string"},
             value_type:{enum:(.value_types|keys)},methods:{type:"object",additionalProperties:{enum:(($methods|keys)+["unknown"])}},
             uncovered:{type:"array",items:{type:"string"}}},operations:($methods|map_values(.meaning))}' "$bundle/providers/library.json"
fi
