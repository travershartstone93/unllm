#!/usr/bin/env bash
# Linux x86-64 release probe. Build the isolated view from selected tools and
# their ELF dependencies. The caller's checkout, home, PATH and site-packages
# are absent. The outer harness and evidence directory stay on the host.
set -euo pipefail
prefix=$(realpath "${1:?Usage: installed_runtime.sh PREFIX NEW_EVIDENCE_DIR}")
out=$(realpath -m "${2:?Provide a new evidence directory}")
mkdir "$out"
mkdir "$out/state"
declare -A files
add_file() {
    local src=$1 dest=${2:-$1}
    [[ -f "$src" ]] || { echo "Missing test dependency $src" >&2; return 1; }
    files["$dest"]=$(realpath "$src")
}
dependencies() {
    local elf=$1 path listing
    if [[ "$elf" == "$prefix/"* ]]; then
        listing=$(LC_ALL=C LD_LIBRARY_PATH="$prefix/lib" ldd "$elf")
    else
        listing=$(LC_ALL=C ldd "$elf")
    fi
    while read -r path; do
        # Objects inside the installed prefix travel with it.
        [[ -z "$path" || "$path" == "$prefix/"* ]] && continue
        case "$path" in
            /lib64/*|/lib/*|/usr/lib64/*) add_file "$path" "/usr/lib/${path##*/}" ;;
            /usr/lib/*) add_file "$path" ;;
            */libonnxruntime.so.1) add_file "$path" /usr/lib/libonnxruntime.so.1 ;;
            *) echo "Unexpected runtime dependency: $path" >&2; return 1 ;;
        esac
    done < <(printf '%s\n' "$listing" | awk '/=> \// {print $3} /^[[:space:]]*\// {print $1}')
    if [[ "$listing" == *"not found"* ]]; then
        echo "Unresolved runtime dependency in $elf" >&2
        return 1
    fi
}
for elf in "$prefix"/bin/* "$prefix"/lib/*.so* "$prefix/share/unllm/.deps/ace-0.9.34/ace"; do
    dependencies "$elf"
done
for name in clang++ clang ld ld.lld bwrap prlimit pkg-config env sha256sum; do
    add_file "/usr/bin/$name"
    dependencies "/usr/bin/$name"
done
for name in Qt6Core Qt6Gui Qt6Widgets Qt6Test stdc++ m c; do
    add_file "/usr/lib/lib$name.so"
done
for name in Qt6Core Qt6Gui Qt6Widgets Qt6Test; do
    add_file "/usr/lib/lib$name.so.6"
    dependencies "/usr/lib/lib$name.so.6"
done
for path in /usr/lib/{Scrt1.o,crt1.o,crti.o,crtn.o,libc_nonshared.a,libpthread.a,librt.a,libdl.a,libresolv.so,libresolv.so.2,libm.so.6,libgcc_s.so.1}; do add_file "$path"; done
plugin=/usr/lib/qt6/plugins/platforms/libqoffscreen.so
add_file "$plugin"
dependencies "$plugin"
# Linker scripts on some systems also reference these objects.
for path in /usr/lib/{libmvec.so.1,libmvec_nonshared.a,libgcc_s.so}; do [[ ! -f "$path" ]] || add_file "$path"; done
gcc_lib=$(dirname "$(clang++ -print-file-name=crtbeginS.o)")
resource=$(clang++ -print-resource-dir)
args=(--unshare-all --die-with-parent --clearenv --proc /proc --dev /dev --tmpfs /tmp
      --symlink usr/lib /lib --symlink usr/lib /lib64 --symlink lib /usr/lib64
      --ro-bind "$prefix" /opt/unllm --bind "$out/state" /state
      --setenv HOME /state --setenv XDG_STATE_HOME /state --setenv PATH /usr/bin
      --setenv LANG C.UTF-8 --chdir /state)
# Binding individual header trees would also expose distro-specific optional
# toolchains. Only the standard and Qt development headers are needed here.
for hdr in /usr/include/*; do
    case "${hdr##*/}" in
        *.h|c++|bits|gnu|sys|asm|asm-generic|linux|qt6) args+=(--ro-bind "$hdr" "$hdr") ;;
    esac
done
for dir in "$resource" "$gcc_lib" /usr/lib/locale /usr/lib/pkgconfig /usr/lib/qt6/mkspecs/linux-g++; do
    args+=(--ro-bind "$dir" "$dir")
done
if [[ -d /etc/fonts && -d /usr/share/fonts ]]; then
    args+=(--ro-bind /etc/fonts /etc/fonts --ro-bind /usr/share/fonts /usr/share/fonts)
fi
for dest in "${!files[@]}"; do args+=(--ro-bind "${files[$dest]}" "$dest"); done
printf '%s\n' "${!files[@]}" | sort > "$out/system-files.txt"
cat > "$out/requests.jsonl" <<'JSON'
{"question":"Paris is in France."}
{"question":"Is Paris in France?"}
{"question":"Make a standard calculator app.","language":"python"}
{"question":"Make a standard calculator app.","language":"cpp"}
JSON
strace -f -qq -e trace=execve,openat -o "$out/process.trace" \
    bwrap "${args[@]}" -- /opt/unllm/bin/unllm_symbolic_worker \
    < "$out/requests.jsonl" > "$out/replies.jsonl" 2>"$out/stderr"
jq -es 'length==4 and .[0].truth=="asserted" and .[1].truth=="supported" and
    .[2].status=="emitted" and .[3].status=="validated" and .[3].validation.sanitizers' \
    "$out/replies.jsonl" >/dev/null
traces=("$out/process.trace")
cat > "$out/state/import.json" <<'JSON'
{"source":{"id":"deployment-probe"},"entities":[{"id":"north","name":"Northport","type":"city"},{"id":"vale","name":"Vale","type":"country"}],"claims":[{"predicate":"located_in","subject":"north","object":"vale"}]}
JSON
for operation in import-json prepare status; do
    command=(/opt/unllm/bin/unllm_facts_native "$operation")
    if [[ "$operation" == import-json ]]; then command+=(/state/import.json); fi
    strace -f -qq -e trace=execve,openat -o "$out/facts-$operation.trace" \
        bwrap "${args[@]}" -- "${command[@]}" > "$out/facts-$operation.json"
    traces+=("$out/facts-$operation.trace")
done
jq -e '.[0].claims>0' "$out/facts-import-json.json" >/dev/null
jq -e '.status=="complete" and .tuples>0' "$out/facts-prepare.json" >/dev/null
jq -e '.database=="/state/unllm-native/facts.sqlite"' "$out/facts-status.json" >/dev/null
printf '%s\n' '{"question":"Is Northport in Vale?"}' | \
    strace -f -qq -e trace=execve,openat -o "$out/import-query.trace" \
    bwrap "${args[@]}" -- /opt/unllm/bin/unllm_symbolic_worker > "$out/import-query.json"
jq -e '.truth=="supported"' "$out/import-query.json" >/dev/null
traces+=("$out/import-query.trace")
if [[ -x "$prefix/bin/unllm" ]]; then
    cp "$(dirname "$0")/../../experiments/algorithm_ir/evaluation/prefix-example.json" "$out/state/scan-contract.json"
    strace -f -qq -e trace=execve,openat -o "$out/scan.trace" \
        bwrap "${args[@]}" -- /opt/unllm/bin/unllm --algorithm synthesize /state/scan-contract.json > "$out/scan.json"
    jq -e '.status=="candidate" and .ir.functions[0].body[0][0]=="each"' "$out/scan.json" >/dev/null
    traces+=("$out/scan.trace")
    strace -f -qq -e trace=execve,openat -o "$out/public.trace" \
        bwrap "${args[@]}" --setenv UNLLM_NO_SERVER 1 \
        --setenv UNLLM_MODEL off -- /opt/unllm/bin/unllm --json --code cpp 'Make a standard calculator app.' \
        > "$out/public.json" 2>"$out/public.stderr"
    jq -e '.status=="validated" and .validation.sanitizers and .validation.cases>0' "$out/public.json" >/dev/null
    traces+=("$out/public.trace")
    for mode in legacy shadow; do
        question='What is 2 plus 3?'
        if [[ "$mode" == shadow ]]; then question='Paris is in France.'; fi
        strace -f -qq -e trace=execve,openat -o "$out/$mode.trace" \
            bwrap "${args[@]}" --setenv UNLLM_NO_SERVER 1 --setenv UNLLM_MODEL off \
            -- /opt/unllm/bin/unllm --engine "$mode" --json "$question" > "$out/$mode.json"
        traces+=("$out/$mode.trace")
    done
    jq -e '.branch=="calc" and .text=="2 plus 3 is 5."' "$out/legacy.json" >/dev/null
    jq -e '.status=="shadow" and .symbolic.truth=="asserted"' "$out/shadow.json" >/dev/null
    mkdir "$out/state/documents"
    echo 'The Faroe Islands are an archipelago in the North Atlantic Ocean.' > "$out/state/documents/islands.txt"
    for operation in stage query; do
        if [[ "$operation" == stage ]]; then
            command=(/opt/unllm/bin/unllm_ingest_native stage /state/documents --name fixture)
        else
            command=(/opt/unllm/bin/unllm_pack query /state/.local/share/unllm/staging/packs 'What are the Faroe Islands?')
        fi
        strace -f -qq -e trace=execve,openat -o "$out/search-$operation.trace" \
            bwrap "${args[@]}" -- "${command[@]}" > "$out/search-$operation.result"
        traces+=("$out/search-$operation.trace")
    done
    grep -q 'archipelago in the North Atlantic' "$out/search-query.result"
    if [[ -x "$prefix/bin/unllm_index" ]]; then
        for operation in index search; do
            if [[ "$operation" == index ]]; then
                command=(/opt/unllm/bin/unllm_index /state/.local/share/unllm/staging/packs/fixture)
            else
                command=(/opt/unllm/bin/unllm_pack search /state/.local/share/unllm/staging/packs 'Faroe Islands')
            fi
            strace -f -qq -e trace=execve,openat -o "$out/ranked-$operation.trace" \
                bwrap "${args[@]}" -- "${command[@]}" > "$out/ranked-$operation.result"
            traces+=("$out/ranked-$operation.trace")
        done
        grep -q 'archipelago in the North Atlantic' "$out/ranked-search.result"
    fi
    if [[ -n "${UNLLM_TEST_REFRESH_FIXTURE:-}" ]]; then
        strace -f -qq -e trace=execve,openat -o "$out/update.trace" \
            bwrap "${args[@]}" --ro-bind "$UNLLM_TEST_REFRESH_FIXTURE" /fixture/refresh \
            --ro-bind "${UNLLM_TEST_EMBED_MODEL:?Provide the embedding model folder}" /fixture/model \
            --setenv LD_LIBRARY_PATH /opt/unllm/lib -- /fixture/refresh /state/update-fixture /opt/unllm/bin /fixture/model > "$out/update.log" 2>&1
        traces+=("$out/update.trace")
    fi
fi
if grep -E 'execve\("[^"]*/python([0-9.]*|w)?",|openat\([^,]+, "[^"]*/libpython[^"]*\.so[^"]*"[^=]*= [0-9]+' "${traces[@]}"; then
    echo "A Python executable or shared library was used" >&2
    exit 1
fi
jq -sc 'map({status,truth,language,cases:.validation.cases})' "$out/replies.jsonl"
