#!/usr/bin/env bash
set -euo pipefail
PROJECT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
TEST_DIR="$(mktemp -d)"
trap 'rm -f "$TEST_DIR/production.inc" "$TEST_DIR/test"; rmdir "$TEST_DIR"' EXIT
if [ -n "${JOIN_BSS_TX_BASELINE:-}" ]; then
    git -C "$PROJECT_DIR" show "$JOIN_BSS_TX_BASELINE:itl80211/openbsd/net80211/ieee80211_node.c"
else
    sed -n '/^ieee80211_node_join_bss(/,/^struct ieee80211_node \*/p' \
        "$PROJECT_DIR/itl80211/openbsd/net80211/ieee80211_node.c"
fi | awk '
    /^ieee80211_node_join_bss\(/ { selected=1; print "void" }
    selected { print }
    selected && /^}/ { selected=0 }
' > "$TEST_DIR/production.inc"
"${CXX:-clang++}" -std=c++17 -Wall -Wextra -Werror -g \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I "$TEST_DIR" "$PROJECT_DIR/tests/net80211_join_bss_tx_teardown_test.cpp" \
    -o "$TEST_DIR/test"
"$TEST_DIR/test"
