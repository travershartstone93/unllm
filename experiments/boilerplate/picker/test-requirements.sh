#!/usr/bin/env bash
set -euo pipefail
bin=$1
vocab=$2
model=$3
input='[{"id":"cpp","text":"Create a C++ file."},{"id":"test","text":"Generate a unit test for Google Test."},{"id":"output","text":"Create a plugin."},{"id":"context","text":"Read settings for my plugin."},{"id":"options","text":"Use GLFW or SDL2 for a window."},{"id":"supplied","text":"Use an SDL2 renderer.","requires":["explicit"]}]'
printf '%s\n' "$input" | "$bin" "$vocab" "$model" |
    jq -e 'map(.requires // [])==[[],["gtest"],["output:plugin"],[],[],["explicit","sdl2"]]' >/dev/null
