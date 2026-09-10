#!/usr/bin/env bash
set -euo pipefail
PROJECT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
ROAM_TEST_DIR="$(mktemp -d)"
trap 'rm -f "$ROAM_TEST_DIR/production.inc" "$ROAM_TEST_DIR/constants.inc" "$ROAM_TEST_DIR/test"; rmdir "$ROAM_TEST_DIR"' EXIT
PROTO="$PROJECT_DIR/itl80211/openbsd/net80211/ieee80211_proto.c"
awk '/^#define[ \t]+IEEE80211_(F_RSNON|F_WEPON|CAPINFO_PRIVACY|NODE_MFP|RSNCAP_MFPC|PROTO_RSN)[ \t]/' \
    "$PROJECT_DIR/itl80211/openbsd/net80211/ieee80211_var.h" \
    "$PROJECT_DIR/itl80211/openbsd/net80211/ieee80211.h" \
    "$PROJECT_DIR/itl80211/openbsd/net80211/ieee80211_node.h" > "$ROAM_TEST_DIR/constants.inc"
awk '
    /^ieee80211_roam_link_source_epoch\(/ { selected=1; print "uint64_t" }
    /^ieee80211_roam_link_(begin|failed|note_terminal)\(/ { selected=1; print "void" }
    /^ieee80211_roam_link_progress\(/ { selected=1; print "int" }
    /^ieee80211_set_link_state\(/ { selected=1; print "void" }
    selected { print }
    selected && /^}/ { selected=0 }
' "$PROTO" > "$ROAM_TEST_DIR/production.inc"
if [ -n "${ROAM_CARRIER_BASELINE:-}" ]; then
    git -C "$PROJECT_DIR" show "$ROAM_CARRIER_BASELINE:itl80211/openbsd/net80211/ieee80211_node.c"
else
    sed -n '/^ieee80211_node_join_bss(/,/^struct ieee80211_node \*/p' \
        "$PROJECT_DIR/itl80211/openbsd/net80211/ieee80211_node.c"
fi | awk '
    /^ieee80211_node_join_bss\(/ { selected=1; print "void" }
    selected { print }
    selected && /^}/ { selected=0 }
' >> "$ROAM_TEST_DIR/production.inc"
"${CXX:-clang++}" -std=c++17 -Wall -Wextra -Werror -g \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I "$ROAM_TEST_DIR" "$PROJECT_DIR/tests/net80211_roam_carrier_test.cpp" \
    -o "$ROAM_TEST_DIR/test"
"$ROAM_TEST_DIR/test"

# Verify the tested decision/terminal functions remain wired to the real
# common bridge and all three hardware failure paths.
sed -n '/^ieee80211_newstate(/,/^ieee80211_set_link_state(/p' "$PROTO" |
    grep -Fq 'if (!ieee80211_roam_link_progress(ic, ostate, nstate))'
for backend in itlwm/hal_iwn/ItlIwn.cpp itlwm/hal_iwm/mac80211.cpp itlwm/hal_iwx/ItlIwx.cpp; do
    grep -Fq 'ieee80211_roam_link_failed(ic, roam_epoch);' "$PROJECT_DIR/$backend"
done
