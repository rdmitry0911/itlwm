#!/usr/bin/env bash
# Full production lower bodies. Default is deliberately red, not in the green
# aggregate until the source-TX/reset/BSS continuation lifetime is implemented.
set -euo pipefail
ulimit -c 0
TX_RETIRE_ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
TX_RETIRE_TEST="$(mktemp -d)"
trap 'rm -f "$TX_RETIRE_TEST/node-ref.inc" "$TX_RETIRE_TEST/node-release.inc" "$TX_RETIRE_TEST/iwm-retirement.inc" "$TX_RETIRE_TEST/iwx-retirement.inc" "$TX_RETIRE_TEST/test"; rm -rf "$TX_RETIRE_TEST/test.dSYM"; rmdir "$TX_RETIRE_TEST"' EXIT
case "${TX_RETIRE_EXPECT_DEFECTS:-0}" in 0|1) ;; *) exit 2 ;; esac
awk '/^ieee80211_node_incref\(/ { selected=1; print "static inline void" }
     /^ieee80211_node_decref\(/ { selected=1; print "static inline u_int" }
     /^ieee80211_ref_node\(/ { selected=1; print "static inline struct ieee80211_node *" }
     selected { print } selected && /^}/ { selected=0 }' \
    "$TX_RETIRE_ROOT/itl80211/openbsd/net80211/ieee80211_node.h" > "$TX_RETIRE_TEST/node-ref.inc"
awk '/^ieee80211_release_node\(/ { selected=1; print "void" }
     selected { print } selected && /^}/ { selected=0 }' \
    "$TX_RETIRE_ROOT/itl80211/openbsd/net80211/ieee80211_node.c" > "$TX_RETIRE_TEST/node-release.inc"
awk '/^iwm_(txd_done|ampdu_txq_advance|reset_tx_ring|free_tx_ring)\(/ { selected=1; print "void ItlIwm::" }
     selected { print } selected && /^}/ { selected=0 }' \
    "$TX_RETIRE_ROOT/itlwm/hal_iwm/mac80211.cpp" \
    "$TX_RETIRE_ROOT/itlwm/hal_iwm/tx.cpp" > "$TX_RETIRE_TEST/iwm-retirement.inc"
awk '/^iwx_(txd_done|ampdu_txq_advance|reset_tx_ring|free_tx_ring)\(/ { selected=1; print "void ItlIwx::" }
     /^iwx_clear_tx_desc\(/ { selected=1; print "void" }
     selected { print } selected && /^}/ { selected=0 }' \
    "$TX_RETIRE_ROOT/itlwm/hal_iwx/ItlIwx.cpp" > "$TX_RETIRE_TEST/iwx-retirement.inc"
"${CXX:-clang++}" -std=c++17 -g -Wall -Wextra -Werror \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I "$TX_RETIRE_TEST" "$TX_RETIRE_ROOT/tests/reassoc_tx_retirement_test.cpp" \
    -o "$TX_RETIRE_TEST/test"
"$TX_RETIRE_TEST/test" 0
"$TX_RETIRE_TEST/test" 9
for scenario in 1 2 3 4 5 6 7 8; do
    result=0
    "$TX_RETIRE_TEST/test" "$scenario" || result=$?
    printf 'TX retirement scenario=%s exit=%s\n' "$scenario" "$result"
    if [ "${TX_RETIRE_EXPECT_DEFECTS:-0}" = 1 ]; then
        test "$result" -eq 134
    else
        test "$result" -eq 0
    fi
done
if [ "${TX_RETIRE_EXPECT_DEFECTS:-0}" = 1 ]; then
    printf 'Eight physical-retirement requirements remain RED; expected failures are NOT driver qualification.\n'
fi
