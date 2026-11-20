#!/bin/bash
# The independent tests belong to the verifier, never to a candidate bundle.
set -euo pipefail
here=$(cd -- "$(dirname -- "$0")" && pwd)
bundle=$(realpath -e "$1")
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
cd "$work"
jq -e 'type=="array" and (map(.id)|unique|length)==length and
  all(.[]; (.header|test("^[a-zA-Z0-9_-]+\\.hpp$")) and (.packages|type=="array"))' "$bundle/components.json" >/dev/null
jq -n -e --slurpfile cards "$bundle/components.json" --slurpfile manifest "$bundle/providers/contracts.json" '
  ($cards[0]|map(.id)) as $ids | all($manifest[0].providers[]; .brick as $id|$ids|index($id)!=null)' >/dev/null
for header in $(jq -r '.[].header' "$bundle/components.json" | sort -u); do test -f "$bundle/headers/$header"; done
c++ -std=c++17 -O2 -I"$bundle/headers" "$here/test-components.cpp" -o checks $(pkg-config --cflags --libs zlib json-c)
bwrap --die-with-parent --unshare-all --ro-bind /usr /usr --symlink usr/lib /lib --symlink usr/lib /lib64 \
  --tmpfs /work --ro-bind "$work/checks" /check --chdir /work /check
if jq -e 'any(.[];has("operation"))' "$bundle/components.json" >/dev/null; then
    python3 "$here/expanded/check.py" "$bundle"
fi
jq -e '(map(select((has("operation")|not) and .origin!="generated_gir")|.id)|sort)==(["k42","t80","f81","d82","g83","u84","j85"]|sort)' "$bundle/components.json" >/dev/null
while IFS= read -r id; do
    [[ $id =~ ^gir_g_[a-z0-9_]+$ ]]
    qualification="$bundle/qualifications/$id"
    header="$id.hpp"
    digest=$(sha256sum "$bundle/headers/$header" | cut -d' ' -f1)
    test_digest=$(sha256sum "$qualification/client.cpp" | cut -d' ' -f1)
    jq -e --arg header "$header" --arg digest "$digest" --arg client "$test_digest" \
        '.header==$header and .sha256==$digest and .test_sha256==$client' "$qualification/receipt.json" >/dev/null
    jq -ne --arg id "$id" --arg header "$header" --slurpfile a "$qualification/adapter.json" \
        --slurpfile cards "$bundle/components.json" --slurpfile contracts "$bundle/providers/contracts.json" '
        ($a[0]) as $a | $a.id==$id and $a.header==$header and $a.package=="glib-2.0" and
        any($cards[0][];.id==$id and .header==$header and .packages==[$a.package]) and
        ([$contracts[0].providers[]|select(.brick==$id)]==[($a|{id,brick:.id,from,to,header,call})])' >/dev/null
    call=$(jq -er '.call|select(test("^unllm_generated::read_g_[a-z0-9_]+$"))' "$qualification/adapter.json")
    c++ -std=c++17 -fPIC "-DUNLLM_TEST_BRICK=$call" -I"$bundle/headers" -include "$header" "$qualification/client.cpp" -o client $(pkg-config --cflags --libs glib-2.0)
    bwrap --die-with-parent --unshare-all --ro-bind /usr /usr --symlink usr/lib /lib --symlink usr/lib /lib64 \
        --tmpfs /work --ro-bind "$work/client" /client --chdir /work /usr/bin/timeout 30 /client
done < <(jq -r '.[]|select(.origin=="generated_gir")|.id' "$bundle/components.json")
(cd "$bundle"; sha256sum components.json providers/* headers/*) > "$work/hashes"
jq -n --rawfile hashes "$work/hashes" --slurpfile cards "$bundle/components.json" \
  '{checked:($cards[0]|map(.id)),sha256:$hashes}' > "$bundle/verification.json"
