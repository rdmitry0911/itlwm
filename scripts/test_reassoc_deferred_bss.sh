#!/usr/bin/env bash
# A full-production baseline, intentionally red until the deferred BSS lifetime
# is implemented. EXPECT_DEFECTS verifies failures; it is not a green driver gate.
set -euo pipefail
ulimit -c 0
BSS_SWITCH_ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
BSS_SWITCH_TEST="$(mktemp -d)"
trap 'rm -f "$BSS_SWITCH_TEST/node-ref.inc" "$BSS_SWITCH_TEST/node-switch.inc" "$BSS_SWITCH_TEST/test"; rm -rf "$BSS_SWITCH_TEST/test.dSYM"; rmdir "$BSS_SWITCH_TEST"' EXIT
case "${BSS_SWITCH_EXPECT_DEFECTS:-0}" in 0|1) ;; *) exit 2 ;; esac
awk '/^ieee80211_node_incref\(/ { selected=1; print "static inline void" }
     /^ieee80211_node_decref\(/ { selected=1; print "static inline u_int" }
     /^ieee80211_ref_node\(/ { selected=1; print "static inline struct ieee80211_node *" }
     selected { print } selected && /^}/ { selected=0 }' \
    "$BSS_SWITCH_ROOT/itl80211/openbsd/net80211/ieee80211_node.h" \
    > "$BSS_SWITCH_TEST/node-ref.inc"
awk '/^struct ieee80211_node_switch_bss_arg/ { structure=1 }
     structure { print } structure && /^};/ { structure=0 }
     /^ieee80211_node_(lateattach|switch_bss)\(/ { selected=1; print "void" }
     /^ieee80211_release_node\(/ { selected=1; print "void" }
     selected { print } selected && /^}/ { selected=0 }' \
    "$BSS_SWITCH_ROOT/itl80211/openbsd/net80211/ieee80211_node.c" \
    > "$BSS_SWITCH_TEST/node-switch.inc"
"${CXX:-clang++}" -std=c++17 -g -Wall -Wextra -Werror \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I "$BSS_SWITCH_TEST" "$BSS_SWITCH_ROOT/tests/reassoc_deferred_bss_test.cpp" \
    -o "$BSS_SWITCH_TEST/test"
"$BSS_SWITCH_TEST/test" 0
for scenario in 1 2 3 4; do
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
    printf 'Four current production defects reproduced; NOT a passing roaming implementation.\n'
fi
