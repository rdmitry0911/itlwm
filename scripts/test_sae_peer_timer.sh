#!/usr/bin/env bash
set -euo pipefail
ulimit -c 0
task_root=$(cd "$(dirname "$0")/.." && pwd)
task_out=$(mktemp -d "${TMPDIR:-/tmp}/itlwm-sae-peer-timer.XXXXXX")
# Compile the complete production class, replacing only IOKit/clock objects.
sed '/^#include /d' "$task_root/include/HAL/ItlSaePeerTimer.hpp" > "$task_out/timer.inc"
"${CXX:-clang++}" -std=c++17 -g -Wall -Wextra -Werror \
  -fsanitize=address,undefined -fno-omit-frame-pointer -I"$task_out" \
  "$task_root/tests/sae_peer_timer_test.cpp" -o "$task_out/test"
"$task_out/test"
printf 'SAE timer test artifacts: %s\n' "$task_out"
