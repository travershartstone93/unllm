#!/bin/bash
# Called after prepare verifies the invocation profile and reports an absent package.
# 0: installation completed, 10: build with bricks, 1: transaction failed.
set -euo pipefail
repo=$(jq -er '.repo | select(IN("core","extra","multilib"))' "$1")
package=$(jq -er '.package | select(test("^[a-z0-9][a-z0-9@._+-]*$"))' "$1")
version=$(jq -er '.version | select(type=="string" and length>0)' "$1")
if [[ ${ARCH_INSTALL_TERMINAL:-} == alacritty ]]; then
    replydir=$(mktemp -d)
    trap 'rm -rf -- "$replydir"' EXIT
    # The new terminal runs the inner script with its own stdin and arguments.
    # shellcheck disable=SC2016
    ARCH_INSTALL_TERMINAL="" alacritty --title "unllm package choice: $repo/$package" -e /bin/bash -c '
        rc=0
        "$1" "$2" || rc=$?
        printf "%s\n" "$rc" > "$3"
    ' unllm-choice "$(realpath "$0")" "$(realpath "$1")" "$replydir/status" || exit 10
    case $(cat "$replydir/status" 2>/dev/null) in
        0) exit 0 ;; 1) exit 1 ;; *) exit 10 ;;
    esac
fi
[[ -t 0 ]] || exit 10
printf 'install package %s from repo %s or build from scratch?\n' "$package" "$repo" >&2
while true; do
    printf '[install/build, default: build] ' >&2
    IFS= read -r reply || exit 10
    case ${reply,,} in
        install) break ;;
        ''|build|'build from scratch') exit 10 ;;
    esac
done

# Keep pacman's own transaction preview, confirmation and signature policy.
# Expand the system config in the privileged process and retain official repositories
# for the dependencies as well as the named target. Never modify the system config.
sudo -- /usr/bin/bash -p -c '
    set -euo pipefail
    export PATH=/usr/bin
    cfg=$(mktemp)
    trap '\''rm -f -- "$cfg"'\'' EXIT
    pacman-conf --config /etc/pacman.conf |
        awk '\''/^\[/ {keep=($0 ~ /^\[(options|core|extra|multilib)\]$/)} keep'\'' > "$cfg"
    pacman --config "$cfg" -S --needed -- "$1"
' unllm-install "$repo/$package=$version" || exit 1
