#!/usr/bin/env bash
# Pending regression: default mode requires the bug to be fixed. The explicit
# baseline mode verifies assertion failures, not functional qualification.
set -euo pipefail
ulimit -c 0
REASSOC_PROJECT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
REASSOC_TEST_DIR="$(mktemp -d)"
trap 'rm -f "$REASSOC_TEST_DIR/source.c" "$REASSOC_TEST_DIR/leaves.inc" "$REASSOC_TEST_DIR/failure.inc" "$REASSOC_TEST_DIR/test"; rm -rf "$REASSOC_TEST_DIR/test.dSYM"; rmdir "$REASSOC_TEST_DIR"' EXIT
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
    printf 'Both current production-helper defects reproduced; neither is fixed.\n'
else
    printf 'WCL reassoc failure retirement regression PASS\n'
fi
