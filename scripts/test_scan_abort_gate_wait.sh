#!/usr/bin/env bash
# Complete production scan abort waiter and terminal owners; IRQ scheduling is
# an explicit same-workloop double, not an on-air scan qualification.
set -euo pipefail
ulimit -c 0
root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
scan_abort_dir=$(mktemp -d)
trap 'rm -f "$scan_abort_dir/scan-terminal.inc" "$scan_abort_dir/scan-owner-plan.inc" "$scan_abort_dir/test"; rm -rf "$scan_abort_dir/test.dSYM"; rmdir "$scan_abort_dir"' EXIT
bash "$root/scripts/test_scan_owner_declarations.sh" > "$scan_abort_dir/scan-owner-plan.inc"
{
    awk '/^ieee80211_wcl_join_failure_pending\(/ { selected=1; print "int" }
         /^ieee80211_wcl_join_cleanup_done\(/ { selected=1; print "void" }
         selected { print } selected && /^}/ { selected=0 }' \
        "$root/itl80211/openbsd/net80211/ieee80211_proto.c"
    for scan_abort_source in itlwm/hal_iwm/ItlIwm.cpp itlwm/hal_iwm/scan.cpp \
        itlwm/hal_iwm/mac80211.cpp itlwm/hal_iwx/ItlIwx.cpp; do
        if [ -n "${SCAN_ABORT_GATE_NEGATIVE_REF:-}" ]; then
            git -C "$root" show "$SCAN_ABORT_GATE_NEGATIVE_REF:$scan_abort_source"
        else
            sed -n '1,$p' "$root/$scan_abort_source"
        fi
    done | awk '
        /^[[:alnum:]_]+ ItlIw[mx]::$/ { type=$0 }
        /^(claimWclScanTerminal|claimScanCommandTerminal|activateScanCommand|scanCommandCurrent|scanCommandBackgroundPending|readyScanCommand|noteScanCommandTerminal|deferScanCommand|scanCommandReplayPending|resumeScanCommand|reserveScanCommandAbort|waitScanCommandAbort|invalidateWclScanForReset|iwm_endscan|iwx_endscan|iwm_scan_abort|iwx_scan_abort|iwm_bgscan_abort|iwx_bgscan_abort)\(/ { selected=1; print type }
        selected { print } selected && /^}/ { selected=0 }
    '
} > "$scan_abort_dir/scan-terminal.inc"
"${CXX:-clang++}" -std=c++17 -Wall -Wextra -Werror -g \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I "$root" -I "$root/include" -I "$scan_abort_dir" \
    "$root/tests/scan_command_terminal_test.cpp" -o "$scan_abort_dir/test"
if [ "${1:-all}" = all ]; then
    for scan_abort_case in offgate-abort-iwm offgate-abort-iwx \
        gated-abort-iwm gated-abort-iwx spurious-abort-iwm spurious-abort-iwx \
        abort-matrix-iwm abort-matrix-iwx; do
        "$scan_abort_dir/test" "$scan_abort_case"
    done
else
    "$scan_abort_dir/test" "$1"
fi
