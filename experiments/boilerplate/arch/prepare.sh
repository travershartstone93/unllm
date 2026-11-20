#!/bin/bash
# Create a source artifact only for a reviewed command and a matching signed installed package.
set -euo pipefail
here=$(cd -- "$(dirname -- "$0")" && pwd)
candidate=$1
dir=$2
repo=$(jq -er '.repo|select(IN("core","extra","multilib"))' "$candidate")
package=$(jq -er .package "$candidate")
profile=$(jq -ce --arg p "$package" --slurpfile candidate "$candidate" \
    'first(.[] | select(.package==$p and .==$candidate[0].profile))' "$here/profiles.json")
bin=$(jq -r .binary <<< "$profile")
version=$(jq -r .version "$candidate")
tool=/usr/bin/$bin
reject() { echo "$*" >&2; exit 1; }
if ! pacman -Qq -- "$package" >/dev/null 2>&1; then
    [[ $(expac -S '%v' "$repo/$package") == "$version" ]] || reject "Official catalog version changed"
    echo "Package not installed: $repo/$package" >&2
    exit 10
fi
[[ -f $tool && -x $tool && ! -L $tool ]] || reject "Executable unavailable: $tool"
[[ $(pacman -Qoq -- "$tool") == "$package" ]] || reject "Executable ownership mismatch"
[[ $(pacman -Q -- "$package") == "$package $version" ]] || reject "Installed version differs from official snapshot"
info=$(LC_ALL=C pacman -Qi -- "$package")
[[ $(awk -F ' *: *' '/^Validated By/{print $2}' <<< "$info") == *Signature* ]] || reject "Installation lacks pacman signature validation"
read -r checksum filename < <(expac -S '%h %f' "$repo/$package")
[[ $checksum =~ ^[0-9a-f]{64}$ && $filename != */* && -n $filename ]] || reject "Missing official package digest"
archive=/var/cache/pacman/pkg/$filename
[[ -r $archive ]] || reject "Matching cached package required"
signature=$archive.sig
if [[ ! -r $signature ]]; then
    signature=$dir/package.sig
    expac -S '%g' "$repo/$package" | base64 --decode > "$signature" || reject "Missing package signature"
fi
printf '%s  %s\n' "$checksum" "$archive" | sha256sum --check --status || reject "Official archive digest mismatch"
gpg --batch --dearmor < /usr/share/pacman/keyrings/archlinux.gpg > "$dir/arch-keyring.gpg"
gpgv --status-fd 1 --keyring "$dir/arch-keyring.gpg" "$signature" "$archive" > "$dir/signature.txt" ||
    reject "Official packager signature failed"
# Arch can disable a packager key independently of the certificate's own revocation.
awk 'NR==FNR {disabled[$1]=1; next} $2=="VALIDSIG" {seen=1; if ($3 in disabled || $NF in disabled) revoked=1}
    END {exit !(seen && !revoked)}' /usr/share/pacman/keyrings/archlinux-revoked "$dir/signature.txt" ||
    reject "Disabled or unidentified Arch signing key"
binary_sha=$(bsdtar -xOf "$archive" "usr/bin/$bin" | sha256sum | cut -d' ' -f1)
printf '%s  %s\n' "$binary_sha" "$tool" | sha256sum --check --status || reject "Installed executable differs from official archive"
jq -n --arg repo "$repo" --arg package "$package" --arg version "$version" --arg tool "$tool" \
    --arg package_sha256 "$checksum" --arg binary_sha256 "$binary_sha" \
    '{repo:$repo,package:$package,version:$version,tool:$tool,package_sha256:$package_sha256,binary_sha256:$binary_sha256}' > "$dir/dependency.json"
{
    jq -r --arg tool "$tool" '"static const char* tool = "+($tool|@json)+";",
      "static const char* arguments[] = {"+([.argv[]| if .=="IN" then "/input" elif .=="OUT" then "/out/result" else . end|@json]|join(", "))+"};"' <<< "$profile"
    cat "$here/launcher.cpp.in"
} > "$dir/converter.cpp"
c++ -std=c++17 -O2 -Wall -Wextra -Werror "$dir/converter.cpp" -o "$dir/converter"
