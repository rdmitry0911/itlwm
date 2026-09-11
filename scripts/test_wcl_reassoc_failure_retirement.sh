#!/usr/bin/env bash
# Actual common-owner and controller-gate regression. Baseline mode compiles
# the unchanged old helper and requires its two independent assertion failures.
set -euo pipefail
ulimit -c 0
REASSOC_PROJECT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
REASSOC_TEST_DIR="$(mktemp -d)"
trap 'rm -f "$REASSOC_TEST_DIR/source.c" "$REASSOC_TEST_DIR/leaves.inc" "$REASSOC_TEST_DIR/failure.inc" "$REASSOC_TEST_DIR/controller.inc" "$REASSOC_TEST_DIR/test"; rm -rf "$REASSOC_TEST_DIR/test.dSYM"; rmdir "$REASSOC_TEST_DIR"' EXIT
case "${WCL_REASSOC_EXPECT_DEFECTS:-0}" in 0|1) ;; *) exit 2 ;; esac
if [ -n "${WCL_REASSOC_NEGATIVE_REF:-}" ]; then
    git -C "$REASSOC_PROJECT_DIR" show \
        "$WCL_REASSOC_NEGATIVE_REF:itl80211/openbsd/net80211/ieee80211.c" \
        > "$REASSOC_TEST_DIR/source.c"
else
    cp "$REASSOC_PROJECT_DIR/itl80211/openbsd/net80211/ieee80211.c" \
        "$REASSOC_TEST_DIR/source.c"
fi
awk '/^#define IEEE80211_WCL_REASSOC_OWNER_/ { print }
     /^#define IEEE80211_EVT_WCL_REASSOC_FAIL / { print }
     /^ieee80211_wcl_reassoc_leaf_is_post_send\(/ { selected=1; print "static inline int" }
     selected { print }
     selected && /^}/ { selected=0 }' \
    "$REASSOC_PROJECT_DIR/itl80211/openbsd/net80211/ieee80211_var.h" \
    > "$REASSOC_TEST_DIR/leaves.inc"
if [ -z "${WCL_REASSOC_NEGATIVE_REF:-}" ] && [ "${WCL_REASSOC_EXPECT_DEFECTS:-0}" = 0 ]; then
    awk '/^#define IEEE80211_WCL_REASSOC_MAX_/ { print }
         /^#define IEEE80211_EVT_WCL_REASSOC_DONE / { print }
         /^struct ieee80211_wcl_reassoc_(candidate|request|completion) \{/ { selected=1 }
         selected { print } selected && /^};/ { selected=0 }' \
        "$REASSOC_PROJECT_DIR/itl80211/openbsd/net80211/ieee80211_var.h" \
        >> "$REASSOC_TEST_DIR/leaves.inc"
    awk '/^ieee80211_wcl_reassoc_serial\(/ { selected=1; print "u_int64_t" }
         /^ieee80211_wcl_reassoc_(current|claim_completion|scan_completion_begin)\(/ { selected=1; print "int" }
         /^ieee80211_wcl_reassoc_clear_locked\(/ { selected=1; print "static void" }
         /^ieee80211_wcl_reassoc_take_completion\(/ { selected=1; print "static int" }
         /^ieee80211_wcl_reassoc_post_(failure|success)\(/ { selected=1; print "void" }
         /^ieee80211_wcl_reassoc_post_failure_owned\(/ { selected=1; print "u_int64_t" }
         selected { print } selected && /^}/ { selected=0 }' \
        "$REASSOC_TEST_DIR/source.c" > "$REASSOC_TEST_DIR/failure.inc"
    awk '/^ieee80211_(begin|cancel)_wcl_reassoc_bgscan\(/ { selected=1; print "int" }
         selected { print } selected && /^}/ { selected=0 }' \
        "$REASSOC_TEST_DIR/source.c" >> "$REASSOC_TEST_DIR/failure.inc"
    awk '/^static IOReturn postWclReassocCompletionGated\(/ { selected=1 }
         selected { print } selected && /^}/ { selected=0 }' \
        "$REASSOC_PROJECT_DIR/AirportItlwm/AirportItlwmV2.cpp" > "$REASSOC_TEST_DIR/controller.inc"
    "${CXX:-clang++}" -std=c++17 -g -Wall -Wextra -Werror \
        -fsanitize=address,undefined -fno-omit-frame-pointer \
        -I "$REASSOC_PROJECT_DIR" -I "$REASSOC_TEST_DIR" \
        "$REASSOC_PROJECT_DIR/tests/wcl_reassoc_owner_test.cpp" \
        -o "$REASSOC_TEST_DIR/test"
    "$REASSOC_TEST_DIR/test"
    exit 0
fi
awk '/^ieee80211_wcl_reassoc_post_failure\(/ { selected=1; print "void" }
     selected { print }
     selected && /^}/ { selected=0 }' "$REASSOC_TEST_DIR/source.c" \
    > "$REASSOC_TEST_DIR/failure.inc"
"${CXX:-clang++}" -std=c++17 -g -Wall -Wextra -Werror \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I "$REASSOC_PROJECT_DIR" -I "$REASSOC_TEST_DIR" \
    "$REASSOC_PROJECT_DIR/tests/wcl_reassoc_failure_retirement_test.cpp" \
    -o "$REASSOC_TEST_DIR/test"
"$REASSOC_TEST_DIR/test" 0
for scenario in 1 2; do
    result=0
    "$REASSOC_TEST_DIR/test" "$scenario" || result=$?
    printf 'scenario=%s exit=%s\n' "$scenario" "$result"
    if [ "${WCL_REASSOC_EXPECT_DEFECTS:-0}" = 1 ]; then
        test "$result" -eq 134
    else
        test "$result" -eq 0
    fi
done
if [ "${WCL_REASSOC_EXPECT_DEFECTS:-0}" = 1 ]; then
    printf 'Both selected baseline defects reproduced; this is negative evidence.\n'
else
    printf 'WCL reassoc failure retirement regression PASS\n'
fi
