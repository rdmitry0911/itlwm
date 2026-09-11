#!/usr/bin/env bash
# A full-production baseline, intentionally red until the deferred BSS lifetime
# is implemented. EXPECT_DEFECTS verifies failures; it is not a green driver gate.
set -euo pipefail
ulimit -c 0
BSS_SWITCH_ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
BSS_SWITCH_TEST="$(mktemp -d)"
trap 'rm -f "$BSS_SWITCH_TEST/node-ref.inc" "$BSS_SWITCH_TEST/node-switch.inc" "$BSS_SWITCH_TEST/iwn-terminal.inc" "$BSS_SWITCH_TEST/tx-node-retire.inc" "$BSS_SWITCH_TEST/test"; rm -rf "$BSS_SWITCH_TEST/test.dSYM"; rmdir "$BSS_SWITCH_TEST"' EXIT
case "${BSS_SWITCH_EXPECT_DEFECTS:-0}" in 0|1) ;; *) exit 2 ;; esac
case "${BSS_SWITCH_PASSING_ONLY:-0}" in 0|1) ;; *) exit 2 ;; esac
awk '/^ieee80211_node_incref\(/ { selected=1; print "static inline void" }
     /^ieee80211_node_decref\(/ { selected=1; print "static inline u_int" }
     /^ieee80211_ref_node\(/ { selected=1; print "static inline struct ieee80211_node *" }
     selected { print } selected && /^}/ { selected=0 }' \
    "$BSS_SWITCH_ROOT/itl80211/openbsd/net80211/ieee80211_node.h" \
    > "$BSS_SWITCH_TEST/node-ref.inc"
awk '/^struct ieee80211_node_switch_bss_arg/ { structure=1 }
     structure { print } structure && /^};/ { structure=0 }
     /^ieee80211_node_(lateattach|switch_bss|copy|cleanup_internal)\(/ { selected=1; print "void" }
     /^ieee80211_bss_switch_identity_(current_locked|capture|current)\(/ { selected=1; print "int" }
     /^ieee80211_node_switch_bss_fail\(/ { selected=1; print "void" }
     /^ieee80211_node_defer_bss_switch\(/ { selected=1; print "int" }
     /^ieee80211_release_node\(/ { selected=1; print "void" }
     selected { print } selected && /^}/ { selected=0 }' \
    "$BSS_SWITCH_ROOT/itl80211/openbsd/net80211/ieee80211_node.c" \
    > "$BSS_SWITCH_TEST/node-switch.inc"
awk '/^ieee80211_tx_node_retire_(append|drain)\(/ { selected=1; print "void" }
     selected { print } selected && /^}/ { selected=0 }' \
    "$BSS_SWITCH_ROOT/itl80211/openbsd/net80211/ieee80211_node.c" \
    > "$BSS_SWITCH_TEST/tx-node-retire.inc"
awk '/^iwn_(tx_done(_free_txdata)?|reset_tx_ring|free_tx_ring)\(/ { selected=1; print "void ItlIwn::" }
     selected { print } selected && /^}/ { selected=0 }' \
    "$BSS_SWITCH_ROOT/itlwm/hal_iwn/ItlIwn.cpp" \
    > "$BSS_SWITCH_TEST/iwn-terminal.inc"
"${CXX:-clang++}" -std=c++17 -g -Wall -Wextra -Werror \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I "$BSS_SWITCH_TEST" "$BSS_SWITCH_ROOT/tests/reassoc_deferred_bss_test.cpp" \
    -o "$BSS_SWITCH_TEST/test"
"$BSS_SWITCH_TEST/test" 0
"$BSS_SWITCH_TEST/test" 5
"$BSS_SWITCH_TEST/test" 4
"$BSS_SWITCH_TEST/test" 7
"$BSS_SWITCH_TEST/test" 8
"$BSS_SWITCH_TEST/test" 6
for scenario in 9 10 11 12; do "$BSS_SWITCH_TEST/test" "$scenario"; done
for scenario in 2 3 13 14 15 16 17 18 19 20 21 22 23 25 26 27; do
    "$BSS_SWITCH_TEST/test" "$scenario"
done
if [ "${BSS_SWITCH_PASSING_ONLY:-0}" = 1 ]; then
    printf 'Callback detach/rearm and actual-copy controls pass; full deferred BSS/TX gate remains separately red.\n'
    exit 0
fi
for scenario in 1 24; do
    result=0
    "$BSS_SWITCH_TEST/test" "$scenario" || result=$?
    printf 'deferred BSS scenario=%s exit=%s\n' "$scenario" "$result"
    if [ "${BSS_SWITCH_EXPECT_DEFECTS:-0}" = 1 ]; then
        test "$result" -eq 134
    else
        test "$result" -eq 0
    fi
done
if [ "${BSS_SWITCH_EXPECT_DEFECTS:-0}" = 1 ]; then
    printf 'Pre-copy and terminal-before-arm liveness remain red; immutable deferred identity passes, NOT a full source-drain or roaming qualification.\n'
fi
