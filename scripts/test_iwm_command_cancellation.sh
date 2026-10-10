#!/usr/bin/env bash
# Complete production sender and cancellation wake helper. Kernel/DMA/wait
# services are explicit doubles; this is not firmware or on-air qualification.
set -euo pipefail
ulimit -c 0
root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
command_test_dir="$(mktemp -d)"
trap 'rm -f "$command_test_dir/iwm-send-cmd.inc" "$command_test_dir/test"; rm -rf "$command_test_dir/test.dSYM"; rmdir "$command_test_dir"' EXIT
bash "$root/scripts/extract_iwm_command_queue.sh" > "$command_test_dir/iwm-send-cmd.inc"
command_historical=0
if [ -n "${IWM_COMMAND_CANCELLATION_NEGATIVE_REF:-}" ]; then command_historical=1; fi
"${CXX:-clang++}" -std=c++17 -Wall -Wextra -Werror -Wno-sign-compare \
    -g -pthread -fsanitize=address,undefined -fno-omit-frame-pointer \
    -DIWM_COMMAND_SLOT_HISTORICAL="$command_historical" \
    -I "$root" -I "$command_test_dir" \
    "$root/tests/iwm_scan_command_submission_test.cpp" -o "$command_test_dir/test"
if [ "${1:-all}" = all ]; then
    for command_case in stop-before-wait stop-during-wait abort-partial-ring \
        ack-before-wait ack-before-wait-dma command-slot-matrix command-slot-threaded; do
        "$command_test_dir/test" "$command_case"
    done
else
    "$command_test_dir/test" "$1"
fi
