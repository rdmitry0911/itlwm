#!/usr/bin/env bash
# Actual complete sender and ACK. Same-workloop IRQ delivery and main gate
# scheduling are explicit doubles, not a hardware firmware qualification.
set -euo pipefail
ulimit -c 0
root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
gate_test_dir=$(mktemp -d)
trap 'rm -f "$gate_test_dir/iwx-send-cmd.inc" "$gate_test_dir/test"; rm -rf "$gate_test_dir/test.dSYM"; rmdir "$gate_test_dir"' EXIT
if [ -n "${IWX_COMMAND_GATE_NEGATIVE_REF:-}" ]; then
    git -C "$root" show "$IWX_COMMAND_GATE_NEGATIVE_REF:itlwm/hal_iwx/ItlIwx.cpp"
else
    sed -n '1,$p' "$root/itlwm/hal_iwx/ItlIwx.cpp"
fi | awk -v historical_stop="${IWX_COMMAND_STOP_NEGATIVE_REF:-}" '
    /^iwx_send_cmd\(/ { selected=1; print "int ItlIwx::" }
    /^iwx_cmd_done\(/ { selected=1; print "void ItlIwx::" }
    /^txQueueAllocationCurrentLocked\(/ { selected=1; print "bool ItlIwx::" }
    /^iwx_cmdq_(ring_valid|start_locked)\(/ { selected=1; print "static bool" }
    /^iwx_cmdq_enter\(/ { selected=1; print "bool ItlIwx::" }
    /^iwx_cmdq_leave\(/ { selected=1; print "void ItlIwx::" }
    /^iwx_cmdq_stop\(/ && historical_stop == "" { selected=1; print "void ItlIwx::" }
    selected { print } selected && /^}/ { selected=0 }
' > "$gate_test_dir/iwx-send-cmd.inc"
if [ -n "${IWX_COMMAND_STOP_NEGATIVE_REF:-}" ]; then
    git -C "$root" show "$IWX_COMMAND_STOP_NEGATIVE_REF:itlwm/hal_iwx/ItlIwx.cpp" | awk '
        /^iwx_cmdq_stop\(/ { selected=1; print "void ItlIwx::" }
        selected { print } selected && /^}/ { selected=0 }
    ' >> "$gate_test_dir/iwx-send-cmd.inc"
fi
"${CXX:-clang++}" -std=c++17 -Wall -Wextra -Werror -Wno-sign-compare \
    -g -pthread -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I "$root" -I "$gate_test_dir" "$root/tests/iwx_scan_command_submission_test.cpp" \
    -o "$gate_test_dir/test"
if [ "${1:-all}" = all ]; then
    for command_gate_case in offgate-ack gated-ack gated-ack-dma \
        command-gate-matrix command-stop-threaded command-stop-gated-threaded \
        command-stop-cleanup-threaded; do
        "$gate_test_dir/test" "$command_gate_case"
    done
else
    "$gate_test_dir/test" "$1"
fi
