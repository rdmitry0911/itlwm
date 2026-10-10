#!/usr/bin/env bash
# Complete production sender and cancellation wake helper. Kernel/DMA/wait
# services are explicit doubles; this is not firmware or on-air qualification.
set -euo pipefail
ulimit -c 0
root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
command_test_dir="$(mktemp -d)"
trap 'rm -f "$command_test_dir/iwm-send-cmd.inc" "$command_test_dir/test"; rm -rf "$command_test_dir/test.dSYM"; rmdir "$command_test_dir"' EXIT
command_ref=${IWM_COMMAND_CANCELLATION_NEGATIVE_REF:-}
if [ -n "$command_ref" ]; then
    git -C "$root" show "$command_ref:itlwm/hal_iwm/phy.cpp"
else
    sed -n '1,$p' "$root/itlwm/hal_iwm/phy.cpp"
fi | awk '/^iwm_send_cmd\(/ { selected=1; print "int ItlIwm::" }
    selected { print } selected && /^}/ { selected=0 }' \
    > "$command_test_dir/iwm-send-cmd.inc"
# A historical sender has no cancellation helper. Keep today's exact helper
# in the explicit stop double: waking first still cannot repair an old sender
# which subsequently registers an unconditional wait. No historical body edits.
awk '/^iwm_radio_abort_command_waits\(/ { selected=1; print "void ItlIwm::" }
    selected { print } selected && /^}/ { selected=0 }' \
    "$root/itlwm/hal_iwm/phy.cpp" >> "$command_test_dir/iwm-send-cmd.inc"
"${CXX:-clang++}" -std=c++17 -Wall -Wextra -Werror -Wno-sign-compare \
    -g -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I "$root" -I "$command_test_dir" \
    "$root/tests/iwm_scan_command_submission_test.cpp" -o "$command_test_dir/test"
if [ "${1:-all}" = all ]; then
    for command_case in stop-before-wait stop-during-wait abort-partial-ring; do
        "$command_test_dir/test" "$command_case"
    done
else
    "$command_test_dir/test" "$1"
fi
