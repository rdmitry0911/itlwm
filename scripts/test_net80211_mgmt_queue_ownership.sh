#!/usr/bin/env bash
# Execute complete production send/output/ref/queue bodies. Packet allocation,
# frame builders and physical completion are explicit fixture boundaries.
set -euo pipefail
ulimit -c 0
MGMT_QUEUE_ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
MGMT_QUEUE_TEST=$(mktemp -d)
trap 'rm -f "$MGMT_QUEUE_TEST/queue.inc" "$MGMT_QUEUE_TEST/ref.inc" "$MGMT_QUEUE_TEST/release.inc" "$MGMT_QUEUE_TEST/output.inc" "$MGMT_QUEUE_TEST/test"; rm -rf "$MGMT_QUEUE_TEST/test.dSYM"; rmdir "$MGMT_QUEUE_TEST"' EXIT
awk '/^ml_(init|enqueue)\(/ { selected=1; print "static inline void" }
     /^ml_dequeue\(/ { selected=1; print "static inline mbuf_t" }
     /^mq_enqueue\(/ { selected=1; print "static inline int" }
     selected { print } selected && /^}/ { selected=0 }' \
    "$MGMT_QUEUE_ROOT/itl80211/openbsd/sys/_mbuf.h" > "$MGMT_QUEUE_TEST/queue.inc"
awk '/^ieee80211_node_incref\(/ { selected=1; print "static inline void" }
     /^ieee80211_node_decref\(/ { selected=1; print "static inline u_int" }
     /^ieee80211_ref_node\(/ { selected=1; print "static inline struct ieee80211_node *" }
     selected { print } selected && /^}/ { selected=0 }' \
    "$MGMT_QUEUE_ROOT/itl80211/openbsd/net80211/ieee80211_node.h" > "$MGMT_QUEUE_TEST/ref.inc"
awk '/^ieee80211_release_node\(/ { selected=1; print "void" }
     selected { print } selected && /^}/ { selected=0 }' \
    "$MGMT_QUEUE_ROOT/itl80211/openbsd/net80211/ieee80211_node.c" > "$MGMT_QUEUE_TEST/release.inc"
awk '/^ieee80211_(mgmt_output|send_mgmt|send_bss_transition_response)\(/ { selected=1; print "int" }
     /^ieee80211_mgmt_frame_prepend\(/ { selected=1; print "static mbuf_t" }
     /^ieee80211_(get_deauth|protected_deauth_frame_build)\(/ { selected=1; print "mbuf_t" }
     /^ieee80211_tx_compressed_bar\(/ { selected=1; print "void" }
     selected { print } selected && /^}/ { selected=0 }' \
    "$MGMT_QUEUE_ROOT/itl80211/openbsd/net80211/ieee80211_output.c" > "$MGMT_QUEUE_TEST/output.inc"
"${CXX:-clang++}" -std=c++17 -g -Wall -Wextra -Werror \
    -fsanitize=address,undefined -fno-omit-frame-pointer -Wno-unused-parameter -I "$MGMT_QUEUE_TEST" \
    "$MGMT_QUEUE_ROOT/tests/net80211_mgmt_queue_ownership_test.cpp" -o "$MGMT_QUEUE_TEST/test"
case "${MGMT_QUEUE_EXPECT_DEFECTS:-0}" in
0) for scenario in {0..33}; do
       "$MGMT_QUEUE_TEST/test" "$scenario"
   done ;;
1) "$MGMT_QUEUE_TEST/test" 0
   result=0
   "$MGMT_QUEUE_TEST/test" 1 || result=$?
   test "$result" -eq 134
   printf 'Observed existing dropped-management ownership failure; NOT a passing driver gate.\n' ;;
*) exit 2 ;;
esac
