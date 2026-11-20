#!/bin/bash
# Runs inside a fresh integrated run directory. Only the local bundle supplies code.
set -euo pipefail
bundle=$1
repo=$(cd "$(dirname "$0")/../../.." && pwd)
planner=${BRICK_PROVIDER_PLANNER:-"$repo/build/native/brick-provider-plan"}
worker=${BRICK_LOGIC_WORKER:-"$repo/build/native/unllm_logic_worker"}
"$planner" "$repo" "$worker" "$bundle/providers/contracts.json" "$bundle/components.json" \
    components/helpers.json calls/interpret/response.json route.json requirements.json "$repo/experiments/boilerplate/integrated/cli-guarantees.json" > components/emission.json
cp components/helpers.json components/picker-helpers.json
jq --slurpfile picks components/picker-helpers.json --slurpfile plans components/emission.json '
    ([$picks[0][].id,$plans[0].providers[].bricks[]]|unique) as $ids |
    [.[]|select(.id as $id|$ids|index($id))]' "$bundle/components.json" > components/helpers.json
stage_header() {
    local directory=$1 name=$2
    [[ $name =~ ^[a-zA-Z0-9_-]+\.hpp$ && -f "$directory/$name" ]] || return 2
    if [[ -e bricks/$name ]]; then cmp -s "$directory/$name" "bricks/$name"
    else cp "$directory/$name" "bricks/$name"; fi
}
while IFS= read -r header; do stage_header "$bundle/headers" "$header"; done < <(jq -r '.[].header' components/helpers.json)
stage_header "$bundle/providers" provider_types.hpp
while IFS= read -r header; do
    if [[ -f $bundle/providers/$header ]]; then stage_header "$bundle/providers" "$header"
    else stage_header "$bundle/headers" "$header"; fi
done < <(jq -r '[.providers[].headers[]]|unique[]' components/emission.json)
jq '[.providers[].typed_gaps[]]|INDEX(.name)' components/emission.json > components/typed-gaps.json
