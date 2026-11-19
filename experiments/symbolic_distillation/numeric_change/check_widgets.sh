#!/usr/bin/env bash
set -euo pipefail
job=$(realpath "${1:?generated job directory}")
mode=${2:?python or cpp}
case $mode in
  cpp) driver=(/job/gui_driver) ;;
  python) driver=(/usr/bin/python3 -I /job/gui_driver.py) ;;
  *) exit 2 ;;
esac
# This oracle is written directly from the supplied frame, independent of cvc5.
jq -e '.state.value.initial == 8.5 and (.actions | map(.id)) == ["change"]' "$job/specification.json" >/dev/null
bwrap --unshare-all --die-with-parent --clearenv --ro-bind /usr /usr --symlink usr/lib /lib --symlink usr/lib /lib64 \
  --dev /dev --proc /proc --tmpfs /tmp --ro-bind "$job" /job --chdir /job \
  --setenv QT_QPA_PLATFORM offscreen --setenv HOME /tmp --setenv PATH /usr/bin \
  -- "${driver[@]}" <<'JSON' | jq -e '.[0].state.value==8.5 and (.[1].trace|map(.value))==[6.25,4,7.5,5.25]'
{"scenarios":[{"events":[]},{"trace":true,"events":[{"button":"change"},{"button":"change"},{"number":"number_view","value":7.5},{"button":"change"}]}]}
JSON
