#!/usr/bin/env bash
set -euo pipefail
ulimit -c 0
PROJECT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
IWN_BEACON_TEST_DIR="$(mktemp -d)"
cleanup() {
    rm -f "$IWN_BEACON_TEST_DIR/join.inc" "$IWN_BEACON_TEST_DIR/command.inc" "$IWN_BEACON_TEST_DIR/lower.inc" "$IWN_BEACON_TEST_DIR/start.inc" "$IWN_BEACON_TEST_DIR/test"
    if [[ -d "$IWN_BEACON_TEST_DIR/test.dSYM" ]]; then
        rm -r "$IWN_BEACON_TEST_DIR/test.dSYM"
    fi
    rmdir "$IWN_BEACON_TEST_DIR"
}
trap cleanup EXIT
awk '
    /^ieee80211_wcl_join_(copy_current|fail|cleanup_done)\(/ {
        selected=1
        print /^ieee80211_wcl_join_cleanup_done/ ? "static void" : "static int"
    }
    selected { print }
    selected && /^}$/ { selected=0; count++ }
    END { if (count != 3) exit 1 }
' "$PROJECT_DIR/itl80211/openbsd/net80211/ieee80211_proto.c" > "$IWN_BEACON_TEST_DIR/join.inc"
awk '
    /^iwn_cmd\(struct iwn_softc/ { selected=1; print "int ItlIwn::" }
    selected { print }
    selected && /^}$/ { found=1; exit }
    END { if (!found) exit 1 }
' "$PROJECT_DIR/itlwm/hal_iwn/ItlIwn.cpp" > "$IWN_BEACON_TEST_DIR/command.inc"
awk '
    /^iwn_newstate_impl\(struct ieee80211com/ { selected=1; print "int ItlIwn::" }
    selected { print }
    selected && /^}$/ { found=1; exit }
    END { if (!found) exit 1 }
' "$PROJECT_DIR/itlwm/hal_iwn/ItlIwn.cpp" > "$IWN_BEACON_TEST_DIR/lower.inc"
awk '
    /^_iwn_start_task\(OSObject/ { selected=1; print "IOReturn ItlIwn::" }
    selected { print }
    selected && /^}$/ { found=1; exit }
    END { if (!found) exit 1 }
' "$PROJECT_DIR/itlwm/hal_iwn/ItlIwn.cpp" > "$IWN_BEACON_TEST_DIR/start.inc"
"${CXX:-clang++}" -std=c++17 -Wall -Wextra -Werror -g \
    -fsanitize=address,undefined -fno-omit-frame-pointer -Wno-unused-parameter \
    -I "$PROJECT_DIR" -I "$PROJECT_DIR/include" -I "$IWN_BEACON_TEST_DIR" \
    "$PROJECT_DIR/tests/iwn_auth_beacon_test.cpp" -o "$IWN_BEACON_TEST_DIR/test"
"$IWN_BEACON_TEST_DIR/test"
