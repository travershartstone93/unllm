#!/bin/sh
set -eu
task_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
task_root=$(CDPATH= cd -- "$task_dir/../../.." && pwd)
task_name=${1:-todo}
task_language=${2:-python}
case "$task_name" in ""|*[!a-zA-Z0-9_]*) echo "Use an example folder name." >&2; exit 2 ;; esac
task_source="$task_dir/$task_name"
if [ ! -f "$task_source/specification.json" ]; then echo "Unknown example." >&2; exit 2; fi
case "$task_language" in
    python) exec "$task_root/.venv/bin/python" "$task_source/program.py" ;;
    cpp)
        task_output="$task_root/.state/request-examples/$task_name"
        if [ ! -x "$task_output/program" ] || [ "$task_source/program.cpp" -nt "$task_output/program" ]; then
            mkdir -p "$task_output"
            clang++ -std=c++20 -O2 "$task_source/program.cpp" $(pkg-config --cflags --libs Qt6Widgets) -o "$task_output/program"
        fi
        exec "$task_output/program"
        ;;
    *) echo "Usage: $0 [example] [python|cpp]" >&2; exit 2 ;;
esac
