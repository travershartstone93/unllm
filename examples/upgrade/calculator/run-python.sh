#!/bin/sh
set -eu
task_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
task_root=$(CDPATH= cd -- "$task_dir/../../.." && pwd)
task_variant=${1:-standard}
case "$task_variant" in standard|memory) ;; *) echo "Usage: $0 [standard|memory]" >&2; exit 2 ;; esac
exec "$task_root/.venv/bin/python" "$task_dir/$task_variant/python/program.py"
