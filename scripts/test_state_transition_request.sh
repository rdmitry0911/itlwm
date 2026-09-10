#!/usr/bin/env bash
set -euo pipefail
ulimit -c 0
PROJECT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
STATE_TEST_DIR="$(mktemp -d)"
trap 'rm -f "$STATE_TEST_DIR/scan-owner-plan.inc" "$STATE_TEST_DIR/state-transition.inc" "$STATE_TEST_DIR/state-test"; rm -rf "$STATE_TEST_DIR/state-test.dSYM"; rmdir "$STATE_TEST_DIR"' EXIT
bash "$PROJECT_DIR/scripts/test_scan_owner_declarations.sh" > "$STATE_TEST_DIR/scan-owner-plan.inc"
{
    awk '/^ieee80211_wcl_join_state_identity\(/ { selected=1; print "int" }
         selected { print } selected && /^}/ { selected=0 }' \
        "$PROJECT_DIR/itl80211/openbsd/net80211/ieee80211_proto.c"
    awk '
        /^[[:alnum:]_]+ ItlIw[mx]::$/ { type=$0 }
        /^(initStateTransitions|shutdownStateTransitions|prepareStateTransition|stateTransitionCurrent|primaryFirmwareContextsPresent|enqueueStateTransition|takeStateTransition|postStateTransitionCommit|recoverStateTransition|drainStateTransitionCommit|stateTransitionEvent|noteStateTransitionProgress|deferScanCommand|resumeScanCommand|scanCommandReplayPending|iwm_newstate_task_dispatch|iwx_newstate_task_dispatch)\(/ { selected=1; print type }
        selected { print } selected && /^}/ { selected=0 }
    ' "$PROJECT_DIR/itlwm/hal_iwm/ItlIwm.cpp" "$PROJECT_DIR/itlwm/hal_iwx/ItlIwx.cpp"
    for source_file in itlwm/hal_iwm/mac80211.cpp itlwm/hal_iwx/ItlIwx.cpp; do
        if [ -n "${STATE_TRANSITION_NEGATIVE_REF:-}" ]; then
            git -C "$PROJECT_DIR" show "$STATE_TRANSITION_NEGATIVE_REF:$source_file"
        else
            sed -n '1,$p' "$PROJECT_DIR/$source_file"
        fi | awk '
            /^[[:alnum:]_]+ ItlIw[mx]::$/ { type=$0 }
            /^(iwm_newstate|iwx_newstate|iwm_newstate_task|iwx_newstate_task)\(/ { selected=1; print type }
            selected { print } selected && /^}/ { selected=0 }
        '
    done
} > "$STATE_TEST_DIR/state-transition.inc"
"${CXX:-clang++}" -std=c++17 -Wall -Wextra -Werror -g \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I "$PROJECT_DIR" -I "$PROJECT_DIR/include" -I "$STATE_TEST_DIR" \
    "$PROJECT_DIR/tests/state_transition_request_test.cpp" \
    -o "$STATE_TEST_DIR/state-test"
"$STATE_TEST_DIR/state-test" "${STATE_TEST_FAMILY:-all}"
