#!/usr/bin/env bash
set -euo pipefail
ulimit -c 0
PROJECT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
SCAN_TEST_DIR="$(mktemp -d)"
cleanup_scan_test() {
    for name in lease11 lease17 iwm-sender iwx-sender bridge terminal ap-resources; do
        rm -f "$SCAN_TEST_DIR/$name"
        rm -rf "$SCAN_TEST_DIR/$name.dSYM"
    done
    rm -f "$SCAN_TEST_DIR/scan-owner-plan.inc" "$SCAN_TEST_DIR/iwm-send-cmd.inc" \
        "$SCAN_TEST_DIR/iwx-send-cmd.inc" "$SCAN_TEST_DIR/scan-admission-bridge.inc" \
        "$SCAN_TEST_DIR/scan-terminal.inc" "$SCAN_TEST_DIR/scan-ap-resources.inc"
    rmdir "$SCAN_TEST_DIR"
}
trap cleanup_scan_test EXIT
bash "$PROJECT_DIR/scripts/test_scan_owner_declarations.sh" > "$SCAN_TEST_DIR/scan-owner-plan.inc"
for standard in 11 17; do
    "${CXX:-clang++}" -std="c++$standard" -Wall -Wextra -Werror -g \
        -fsanitize=address,undefined -fno-omit-frame-pointer \
        -I "$PROJECT_DIR" "$PROJECT_DIR/tests/scan_command_lease_test.cpp" \
        -o "$SCAN_TEST_DIR/lease$standard"
    "$SCAN_TEST_DIR/lease$standard"
done
awk '/^iwm_send_cmd\(/ { selected=1; print "int ItlIwm::" }
     selected { print }
     selected && /^}/ { selected=0 }' \
    "$PROJECT_DIR/itlwm/hal_iwm/phy.cpp" > "$SCAN_TEST_DIR/iwm-send-cmd.inc"
"${CXX:-clang++}" -std=c++17 -Wall -Wextra -Werror \
    -Wno-sign-compare -g -fsanitize=address,undefined \
    -fno-omit-frame-pointer -I "$PROJECT_DIR" -I "$SCAN_TEST_DIR" \
    "$PROJECT_DIR/tests/iwm_scan_command_submission_test.cpp" \
    -o "$SCAN_TEST_DIR/iwm-sender"
"$SCAN_TEST_DIR/iwm-sender"
"$SCAN_TEST_DIR/iwm-sender" dma-failure
awk '/^iwx_send_cmd\(/ { selected=1; print "int ItlIwx::" }
    /^txQueueAllocationCurrentLocked\(/ { selected=1; print "bool ItlIwx::" }
     selected { print }
     selected && /^}/ { selected=0 }' \
    "$PROJECT_DIR/itlwm/hal_iwx/ItlIwx.cpp" > "$SCAN_TEST_DIR/iwx-send-cmd.inc"
"${CXX:-clang++}" -std=c++17 -Wall -Wextra -Werror \
    -Wno-sign-compare -g -fsanitize=address,undefined \
    -fno-omit-frame-pointer -I "$PROJECT_DIR" -I "$SCAN_TEST_DIR" \
    "$PROJECT_DIR/tests/iwx_scan_command_submission_test.cpp" \
    -o "$SCAN_TEST_DIR/iwx-sender"
"$SCAN_TEST_DIR/iwx-sender"
awk '
    /^uint64_t ItlIw[mx]::/ { type=$0 }
    /^bool ItlIw[mx]::/ { type=$0 }
    /^int ItlIw[mx]::/ { type=$0 }
    /^void ItlIw[mx]::/ { type=$0 }
    /^(scanCommandResetEpoch|reopenScanCommands|prepareStateTransition|reserveScanCommand|scanCommandOwnerCurrentLocked|copyScanCommandPolicy|rejectScanCommand|reserveAPScanCommand|currentAPScanCommand|finishAPScanCommand)\(/ { selected=1; print type }
    selected { print }
    selected && /^}/ { selected=0 }
' "$PROJECT_DIR/itlwm/hal_iwm/ItlIwm.cpp" \
  "$PROJECT_DIR/itlwm/hal_iwx/ItlIwx.cpp" > "$SCAN_TEST_DIR/scan-admission-bridge.inc"
"${CXX:-clang++}" -std=c++17 -Wall -Wextra -Werror -g \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I "$PROJECT_DIR" -I "$PROJECT_DIR/include" -I "$SCAN_TEST_DIR" \
    "$PROJECT_DIR/tests/scan_command_admission_bridge_test.cpp" \
    -o "$SCAN_TEST_DIR/bridge"
"$SCAN_TEST_DIR/bridge"
awk '
    /^[[:alnum:]_]+ ItlIw[mx]::$/ { type=$0 }
    /^(claimWclScanTerminal|claimScanCommandTerminal|activateScanCommand|scanCommandCurrent|scanCommandBackgroundPending|readyScanCommand|noteScanCommandTerminal|deferScanCommand|scanCommandReplayPending|resumeScanCommand|reserveScanCommandAbort|waitScanCommandAbort|iwm_endscan|iwx_endscan|iwm_scan_abort|iwx_scan_abort|iwm_bgscan_abort|iwx_bgscan_abort)\(/ { selected=1; print type }
    selected { print }
    selected && /^}/ { selected=0 }
' "$PROJECT_DIR/itlwm/hal_iwm/ItlIwm.cpp" \
  "$PROJECT_DIR/itlwm/hal_iwm/scan.cpp" \
  "$PROJECT_DIR/itlwm/hal_iwm/mac80211.cpp" \
  "$PROJECT_DIR/itlwm/hal_iwx/ItlIwx.cpp" > "$SCAN_TEST_DIR/scan-terminal.inc"
"${CXX:-clang++}" -std=c++17 -Wall -Wextra -Werror -g \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I "$PROJECT_DIR" -I "$PROJECT_DIR/include" -I "$SCAN_TEST_DIR" \
    "$PROJECT_DIR/tests/scan_command_terminal_test.cpp" \
    -o "$SCAN_TEST_DIR/terminal"
"$SCAN_TEST_DIR/terminal"
{
    awk '
        /^[[:alnum:]_]+ ItlIw[mx]::$/ { type=$0 }
        /^(reserveAPScanCommand|currentAPScanCommand|finishAPScanCommand)\(/ { selected=1; print type }
        selected { print }
        selected && /^}/ { selected=0 }
    ' "$PROJECT_DIR/itlwm/hal_iwm/ItlIwm.cpp" "$PROJECT_DIR/itlwm/hal_iwx/ItlIwx.cpp"
    for resource_file in itlwm/hal_iwm/mac80211.cpp itlwm/hal_iwx/ItlIwx.cpp; do
        if [ -n "${SCAN_AP_RESOURCE_NEGATIVE_REF:-}" ]; then
            git -C "$PROJECT_DIR" show "$SCAN_AP_RESOURCE_NEGATIVE_REF:$resource_file"
        else
            sed -n '1,$p' "$PROJECT_DIR/$resource_file"
        fi | awk '
            /^[[:alnum:]_]+ ItlIw[mx]::$/ { type=$0 }
            /^(iwm_start_ap_resources|iwm_stop_ap_resources|iwx_start_ap_mode|iwx_stop_ap_mode)\(/ { selected=1; print type }
            /^iwx_ap_(start|stop)_task\(/ { selected=1; print "static void" }
            selected { print }
            selected && /^}/ { selected=0 }
        '
    done
} > "$SCAN_TEST_DIR/scan-ap-resources.inc"
"${CXX:-clang++}" -std=c++17 -Wall -Wextra -Werror -g \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I "$PROJECT_DIR" -I "$SCAN_TEST_DIR" \
    "$PROJECT_DIR/tests/scan_ap_resource_admission_test.cpp" \
    -o "$SCAN_TEST_DIR/ap-resources"
"$SCAN_TEST_DIR/ap-resources"
