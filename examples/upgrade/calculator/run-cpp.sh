#!/bin/sh
set -eu
task_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
task_root=$(CDPATH= cd -- "$task_dir/../../.." && pwd)
task_variant=${1:-standard}
case "$task_variant" in standard|memory) ;; *) echo "Usage: $0 [standard|memory]" >&2; exit 2 ;; esac
task_source="$task_dir/$task_variant/cpp"
task_runtime="$task_root/.deps/apps/native"
task_output="$task_root/.state/calculator-examples/$task_variant"
task_rebuild=false
if [ ! -x "$task_output/program" ]; then task_rebuild=true; fi
for task_input in "$task_source/program.cpp" "$task_source"/Arithmetic*.cpp "$task_source"/Arithmetic*.h "$task_runtime/lib/libantlr4-runtime.a"; do
    if [ "$task_input" -nt "$task_output/program" ]; then task_rebuild=true; fi
done
if [ "$task_rebuild" = true ]; then
    mkdir -p "$task_output"
    clang++ -std=c++20 -O2 -DANTLR4CPP_STATIC \
        "$task_source/program.cpp" "$task_source"/Arithmetic*.cpp \
        "-I$task_source" "-I$task_runtime/include/antlr4-runtime" \
        "$task_runtime/lib/libantlr4-runtime.a" \
        $(pkg-config --cflags --libs Qt6Widgets) -o "$task_output/program"
fi
exec "$task_output/program"
