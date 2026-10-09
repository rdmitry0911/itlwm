#!/usr/bin/env bash
# Complete IWX lifecycle and q0 start/stop; firmware/task/IOKit doubles.
set -euo pipefail
ulimit -c 0
root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
iwx_lifecycle_dir="$(mktemp -d)"
trap 'rm -f "$iwx_lifecycle_dir/lifecycle.inc" "$iwx_lifecycle_dir/test"; rm -rf "$iwx_lifecycle_dir/test.dSYM"; rmdir "$iwx_lifecycle_dir"' EXIT
if [ -n "${IWX_RADIO_INIT_STOP_NEGATIVE_REF:-}" ]; then
    git -C "$root" show "$IWX_RADIO_INIT_STOP_NEGATIVE_REF:itlwm/hal_iwx/ItlIwx.cpp"
else
    sed -n '1,$p' "$root/itlwm/hal_iwx/ItlIwx.cpp"
fi | awk '
    /^IOReturn ItlIwx::(disable|enable)\(/ { selected=1 }
    /^scanCommandResetEpoch\(/ { selected=1; print "uint64_t ItlIwx::" }
    /^reopenScanCommands\(/ || /^isRadioScanReady\(/ || /^isRadioReadyCurrent\(/ { selected=1; print "bool ItlIwx::" }
    /^iwx_activate\(/ || /^iwx_init_internal\(/ { selected=1; print "int ItlIwx::" }
    /^iwx_stop(_internal)?\(/ { selected=1; print "void ItlIwx::" }
    /^iwx_task_gate_(close|begin_epoch|epoch_live|open|enter)\(/ { selected=1; print "bool ItlIwx::" }
    /^iwx_task_gate_(rearm|leave|end_epoch|drain)\(/ || /^iwx_bootstrap_init_task\(/ { selected=1; print "void ItlIwx::" }
    /^iwx_cmdq_ring_valid\(/ || /^iwx_cmdq_start_locked\(/ { selected=1; print "static bool" }
    /^iwx_cmdq_(start|enter)\(/ { selected=1; print "bool ItlIwx::" }
    /^iwx_cmdq_(stop|leave)\(/ { selected=1; print "void ItlIwx::" }
    selected { print } selected && /^}/ { selected=0 }
' > "$iwx_lifecycle_dir/lifecycle.inc"
"${CXX:-clang++}" -std=c++17 -Wall -Wextra -Werror -Wno-unused-function \
    -Wno-unused-parameter -g -pthread -fsanitize=address,undefined \
    -fno-omit-frame-pointer -I "$iwx_lifecycle_dir" -I "$root/include" \
    "$root/tests/iwx_radio_init_stop_test.cpp" -o "$iwx_lifecycle_dir/test"
if [ "${1:-all}" = all ]; then
    for iwx_lifecycle_case in normal timeout hardware-failure monitor \
        early-off early-off-on early-off-primary-down overlapping-off \
        on-during-stop self-task-stop-collision self-epoch-stop-collision; do
        "$iwx_lifecycle_dir/test" "$iwx_lifecycle_case"
    done
else
    "$iwx_lifecycle_dir/test" "$1"
fi
