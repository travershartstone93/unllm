#!/usr/bin/env bash
# Prepare the SDKs used by the C++ package. No Python interpreter is required.
set -euo pipefail
root=$(realpath "$(dirname "$0")/..")
deps=$(realpath -m "${1:-$root/.deps}")
cache=${UNLLM_ARCHIVE_CACHE:-$deps/archives}
mkdir -p "$cache" "$deps"
exec 9>"$deps/.setup-native.lock"
flock 9
scratch=$(mktemp -d "$deps/.setup-XXXXXX")
trap 'rm -rf "$scratch"' EXIT
fetch() {
    local lock=$1 field=$2 name=$3 url sha cached alternate
    url=$(jq -er "$field.url" "$root/$lock")
    sha=$(jq -er "$field.sha256" "$root/$lock")
    cached="$cache/$name"
    alternate="$cache/${url##*/}"
    if [[ ! -f "$cached" && -f "$alternate" ]]; then cached=$alternate; fi
    if [[ ! -f "$cached" && -f "$deps/apps/archives/$name" ]]; then cached="$deps/apps/archives/$name"; fi
    if [[ ! -f "$cached" ]]; then
        curl --fail --location --retry 3 --proto '=https' --proto-redir '=https' -o "$scratch/download" -- "$url"
        printf '%s  %s\n' "$sha" "$scratch/download" | sha256sum -c - >&2
        cp "$scratch/download" "$cached.part"
        mv "$cached.part" "$cached"
    fi
    printf '%s  %s\n' "$sha" "$cached" | sha256sum -c - >&2
    printf '%s\n' "$cached"
}
for name in souffle.deb libncurses6 swipl ace.tar.gz erg.dat.bz2; do
    archive=$(fetch dependencies-upgrade.lock.json ".downloads[\"$name\"]" "$name")
    case "$name" in
        souffle.deb|libncurses6)
            mkdir -p "$deps/native"
            member=$(ar t "$archive" | awk '/^data[.]tar/{print;exit}')
            ar p "$archive" "$member" > "$scratch/$member"
            tar -xf "$scratch/$member" -C "$deps/native" ;;
        swipl) mkdir -p "$deps/native"; tar -xf "$archive" -C "$deps/native" ;;
        ace.tar.gz) tar -xf "$archive" -C "$deps" ;;
        erg.dat.bz2) bzip2 -dc "$archive" > "$scratch/erg.dat"; mv "$scratch/erg.dat" "$deps/erg.dat" ;;
    esac
done
mkdir -p "$deps/bin"
cat > "$deps/bin/souffle" <<'SH'
#!/bin/sh
base=$(CDPATH= cd -- "$(dirname "$0")/../native/usr" && pwd)
export LD_LIBRARY_PATH="$base/lib/swipl/lib/x86_64-linux:$base/lib/x86_64-linux-gnu:$base/lib"
exec "$base/bin/souffle" "$@"
SH
chmod +x "$deps/bin/souffle"
cvc=$(fetch dependencies-native.lock.json .cvc5 cvc5-1.3.1-native.zip)
mkdir -p "$deps/cvc5-native"
unzip -qo "$cvc" -d "$deps/cvc5-native"
arrow=$(fetch dependencies-native.lock.json .arrow_cpp arrow-native-sdk.whl)
mkdir -p "$deps/arrow-native"/{lib,include,licenses}
unzip -q "$arrow" 'pyarrow/include/arrow/*' 'pyarrow/include/parquet/*' 'pyarrow/libarrow.so.2600' \
    'pyarrow/libparquet.so.2600' 'pyarrow-*.dist-info/licenses/*' -d "$scratch/arrow"
cp -R "$scratch/arrow/pyarrow/include/." "$deps/arrow-native/include/"
cp "$scratch"/arrow/pyarrow/*.so.2600 "$deps/arrow-native/lib/"
cp -R "$scratch"/arrow/*.dist-info/licenses/. "$deps/arrow-native/licenses/"
apps="$deps/apps"
mkdir -p "$apps/archives" "$apps/antlr-source"
for name in antlr-4.13.2-complete.jar antlr4-cpp-runtime-4.13.2-source.zip; do
    archive=$(fetch dependencies-apps.lock.json ".downloads[\"$name\"]" "$name")
    if [[ "$archive" != "$apps/archives/$name" ]]; then cp "$archive" "$apps/archives/$name"; fi
done
unzip -qo "$apps/archives/antlr4-cpp-runtime-4.13.2-source.zip" -d "$apps/antlr-source"
cmake -S "$apps/antlr-source" -B "$apps/antlr-build" -DCMAKE_BUILD_TYPE=Release \
    -DANTLR_BUILD_CPP_TESTS=OFF -DANTLR_BUILD_SHARED=OFF -DANTLR_BUILD_STATIC=ON \
    -DDISABLE_WARNINGS=ON -DCMAKE_INSTALL_PREFIX="$apps/native"
cmake --build "$apps/antlr-build" --target install --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-4}"
out="$scratch/generated"
mkdir -p "$out"/{python,cpp}
grammar="$root/resources/targets/calculator/Arithmetic.g4"
for pair in Cpp:cpp Python3:python; do
    java -jar "$apps/archives/antlr-4.13.2-complete.jar" "-Dlanguage=${pair%:*}" -visitor -no-listener \
        -Xexact-output-dir -o "$out/${pair#*:}" "$grammar"
done
for mode in normal checked; do
    flags=(-O2)
    if [[ "$mode" == checked ]]; then flags=(-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer); fi
    objects=()
    for src in "$out"/cpp/*.cpp; do
        obj="$scratch/$(basename "$src").$mode.o"
        clang++ -std=c++20 -DANTLR4CPP_STATIC -I "$apps/native/include/antlr4-runtime" -I "$out/cpp" "${flags[@]}" -c "$src" -o "$obj"
        objects+=("$obj")
    done
    ar rcs "$out/expression.$mode.a" "${objects[@]}"
done
(
    cd "$out"
    for file in {cpp,python}/*; do
        sha256sum "$file"
    done | jq -Rn '[inputs|capture("^(?<hash>[0-9a-f]+)  (?<name>.+)$")|{key:.name,value:.hash}]|from_entries' > "$scratch/files.json"
)
hash=$(sha256sum "$grammar" | cut -d' ' -f1)
jq -n --arg hash "$hash" --slurpfile files "$scratch/files.json" '{grammar_sha256:$hash,antlr:"4.13.2",files:$files[0]}' > "$out/manifest.json"
mkdir -p "$apps/generated"
cp -R "$out/." "$apps/generated/"
