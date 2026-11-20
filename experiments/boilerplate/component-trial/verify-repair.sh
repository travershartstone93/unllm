#!/bin/bash
# Qualify an archived CLI repair for catalog review using an independent C++ client.
# The evaluator and test source are supplied by the maintainer outside the candidate run.
set -euo pipefail
[[ $# == 5 || $# == 6 ]] || { echo 'Usage: verify-repair.sh RUN BEHAVIOR_EVALUATOR HEADER_NAME INDEPENDENT_TEST.cpp NEW_RECEIPT [QUALIFIED_CALLABLE]' >&2; exit 2; }
run=$(realpath -e "$1"); suite=$(realpath -e "$2"); header=$3; client=$(realpath -e "$4"); receipt=$(realpath -m "$5")
[[ $header =~ ^[A-Za-z_][A-Za-z_0-9]*\.hpp$ && -f $run/bricks/$header && ! -e $receipt ]]
jq -e '.passed == true' "$run/result.json" >/dev/null
work=$(mktemp -d)
pending=
trap 'rm -rf -- "$work"; if [[ -n $pending ]]; then rm -f -- "$pending"; fi' EXIT
mkdir "$work/task"
mapfile -t packages < "$run/project/packages.txt"
flags=()
if (( ${#packages[@]} )); then
    package_flags=$(pkg-config --cflags --libs "${packages[@]}")
    read -r -a flags <<< "$package_flags"
fi
c++ -std=c++17 -fPIC -O2 -I"$run/bricks" "$run/project/converter.cpp" -o "$work/program" "${flags[@]}"
"$suite" "$work/program" "$work/task" > "$work/task.json"
jq -e '.checks|type=="object" and length>0 and all(.[];.pass_==true)' "$work/task.json" >/dev/null
test_binding=()
if [[ $# == 6 ]]; then
    [[ $6 =~ ^[a-zA-Z_][a-zA-Z_0-9]*(::[a-zA-Z_][a-zA-Z_0-9]*)*$ ]]
    test_binding+=("-DUNLLM_TEST_BRICK=$6")
fi
c++ -std=c++17 -fPIC -O2 "${test_binding[@]}" -I"$run/bricks" -include "$header" "$client" -o "$work/client" "${flags[@]}"
timeout 30 bwrap --die-with-parent --unshare-all --ro-bind /usr /usr --symlink usr/lib /lib --symlink usr/lib /lib64 \
  --tmpfs /work --ro-bind "$work/client" /client --chdir /work /client
pending=$(mktemp -- "$(dirname -- "$receipt")/.qualification-XXXXXXXX")
jq -n --arg header "$header" --arg source "$(sha256sum "$run/bricks/$header" | cut -d' ' -f1)" \
 --arg client "$(sha256sum "$client" | cut -d' ' -f1)" --slurpfile task "$work/task.json" \
 '{header:$header,sha256:$source,test_sha256:$client,behavior:$task[0].checks}' > "$pending"
mv -T --no-clobber -- "$pending" "$receipt"
[[ ! -e $pending ]]
