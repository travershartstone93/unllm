#!/usr/bin/env bash
# Build the Linux package from source with its prepared native SDKs and tools.
# Keep the caller's build trees, home, venv, Python executables and site-packages
# outside the build namespace. The only writable host path is the new output.
set -euo pipefail
root=$(realpath "$(dirname "$0")/../..")
out=$(realpath -m "${1:?Specify a new build evidence directory}")
mkdir "$out"
mkdir "$out/test-state"
trap 'rm -rf "$out/sysroot"' EXIT
args=(--unshare-all --die-with-parent --clearenv --proc /proc --dev /dev --tmpfs /tmp
      --symlink usr/bin /bin --symlink usr/lib /lib --symlink usr/lib /lib64
      --symlink lib /usr/lib64 --dir /usr/bin --dir /usr/lib --dir /usr/include
      --bind "$out/test-state" "$root/.state" --ro-bind /usr/share /usr/share --ro-bind /etc /etc --bind "$out" /output
      --setenv PATH /usr/bin --setenv HOME /tmp --setenv LANG C.UTF-8)
for name in cmake ninja sh bash c++ cc g++ gcc as ld ar ranlib strip nm pkg-config pkgconf rm ls readlink dirname sed grep cat ctest clang clang++ clang-format ld.lld bwrap prlimit sqlite3 7z awk mktemp mkdir head sleep touch chmod cp mv env sha256sum realpath true false tail tr timeout
do
    args+=(--ro-bind "$(realpath "/usr/bin/$name")" "/usr/bin/$name")
done
# Copy top-level files by reflink. Binding directories as units keeps the
# mount count below bubblewrap's argument limit on a full developer machine.
args+=(--symlink /usr/lib/jvm/default-runtime/bin/java /usr/bin/java)
for tree in lib include; do
    mirror="$out/sysroot/usr/$tree"
    mkdir -p "$mirror"
    find "/usr/$tree" -mindepth 1 -maxdepth 1 ! -type d ! -iname '*python*' -readable \
        -exec cp --reflink=auto -P -t "$mirror" -- {} +
    args+=(--ro-bind "$mirror" "/usr/$tree")
    for path in "/usr/$tree"/*/; do
        path=${path%/}
        [[ ! -L "$path" ]] || continue
        case "${path##*/}" in *python*|*Python*|perl*|node_modules|ruby*|debug) continue ;; esac
        mkdir -p "$mirror/${path##*/}" 2>/dev/null || true
        args+=(--ro-bind "$path" "$path")
    done
done
for name in CMakeLists.txt native tools src include experiments data grammars reasoning tests resources LICENSE dependencies-{upgrade,apps,native}.lock.json; do
    args+=(--ro-bind "$root/$name" "$root/$name")
done
sdk=${UNLLM_SDK_DIR:-$root/.deps}
for name in bin native cvc5-native arrow-native apps ace-0.9.34 erg.dat; do
    args+=(--ro-bind "$sdk/$name" "$root/.deps/$name")
done
args+=(--ro-bind "$HOME/.local/lib/libsentencepiece.a" /sdk/lib/libsentencepiece.a
       --ro-bind "$HOME/.local/include" /sdk/include)
strace -f -qq -e trace=execve -o "$out/exec.trace" bwrap "${args[@]}" -- /bin/sh -ec '
  if command -v python || command -v python3; then exit 1; fi
  cmake -G Ninja -S "$1" -B /output/build -DCMAKE_BUILD_TYPE=Release \
    -DSENTENCEPIECE_LIB=/sdk/lib/libsentencepiece.a -DSENTENCEPIECE_INC=/sdk/include
  cmake --build /output/build --parallel 4
  cmake --install /output/build --prefix /output/prefix
  ctest --test-dir /output/build --output-on-failure --no-tests=error -j3 > /output/ctest.log
' sh "$root" > "$out/build.log" 2>&1
if grep -Eq 'execve\("[^"]*/(python[0-9.]*|cargo|rustc)"[^=]*= 0' "$out/exec.trace"; then exit 1; fi
