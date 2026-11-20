#!/bin/bash
# Publish a complete snapshot in one rename. A failed refresh leaves existing catalogs usable.
set -euo pipefail
if (( $# < 1 || $# > 2 )); then echo "Usage: build-catalog.sh NEW_DIRECTORY [AUR_METADATA.json.gz]" >&2; exit 2; fi
here=$(cd -- "$(dirname -- "$0")" && pwd)
dest=$(realpath -m "$1")
[[ ! -e $dest ]]
stage=$(mktemp -d -- "${dest}.XXXXXX")
trap 'rm -rf -- "$stage"' EXIT
c++ -std=c++17 -O2 "$here/catalog.cpp" -lxapian -o "$stage/catalog"
aur=${2:-"$stage/aur.json.gz"}
if (( $# == 1 )); then
    curl --fail --silent --show-error --proto '=https' --max-time 180 --retry 1 \
        https://aur.archlinux.org/packages-meta-ext-v1.json.gz -o "$aur"
fi
expac -S $'%r\t%n\t%v\t%d' | awk -F '\t' '$1=="core" || $1=="extra" || $1=="multilib"' > "$stage/official.tsv"
gzip -dc "$aur" | jq -r '.[] | ["aur",.Name,.Version,(.Description // "")] | @tsv' > "$stage/aur.tsv"
for file in official aur; do test -s "$stage/$file.tsv"; done
jq -r '.[] | [.package,.terms] | @tsv' "$here/profiles.json" > "$stage/profiles.tsv"
cat "$stage/official.tsv" "$stage/aur.tsv" | "$stage/catalog" index "$stage/index" "$stage/profiles.tsv"
(cd "$stage"; sha256sum official.tsv aur.tsv profiles.tsv > inputs.sha256; wc -l official.tsv aur.tsv > counts.txt)
mv -T -- "$stage" "$dest"
