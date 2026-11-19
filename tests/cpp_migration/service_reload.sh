#!/usr/bin/env bash
set -euo pipefail
cli=$(realpath "${1:?Provide the public CLI}")
out=$(realpath -m "${2:?Provide a fresh evidence directory}")
mkdir "$out"
# Socket paths are limited to 108 bytes on Linux.
sockets=$(mktemp -d)
pids=()
cleanup() {
    for pid in "${pids[@]}"; do kill "$pid" 2>/dev/null || true; done
    rm -rf "$sockets"
}
trap cleanup EXIT
run() {
    local set=$1; shift
    env -i HOME="$out/$set" PATH=/usr/bin LANG=C.UTF-8 UNLLM_MODEL=off \
        UNLLM_SOCKET="$sockets/$set.sock" UNLLM_IDLE=5 UNLLM_DATA="$out/$set/data" "$cli" "$@"
}
for set in a b; do
    mkdir -p "$out/$set/data/vectors"
    run "$set" --serve >"$out/$set/server.log" 2>&1 &
    pids+=("$!")
    for ((i=0;i<100;i++)); do [[ -S "$sockets/$set.sock" ]] && break; sleep .03; done
    run "$set" --json 'What is 2 plus 3?' > "$out/$set/before.json"
done
# Publishing or rolling back an index atomically replaces a directory entry.
touch "$out/a/data/vectors/candidate"
mv "$out/a/data/vectors/candidate" "$out/a/data/vectors/published"
run a --json 'What is 2 plus 3?' > "$out/a/transition.json"
run a --json 'What is 2 plus 3?' > "$out/a/after.json"
run b --json 'What is 2 plus 3?' > "$out/b/after.json"
jq -se '.[0].server.pid!=.[1].server.pid and all(.[]; .text=="2 plus 3 is 5.")' \
    "$out/a/before.json" "$out/a/after.json" >/dev/null
jq -se '.[0].server.pid==.[1].server.pid' "$out/b/before.json" "$out/b/after.json" >/dev/null
