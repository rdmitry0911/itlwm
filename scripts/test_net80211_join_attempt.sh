#!/usr/bin/env bash
set -euo pipefail
ulimit -c 0
PROJECT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
JOIN_TEST_DIR="$(mktemp -d)"
trap 'rm -f "$JOIN_TEST_DIR/test-c" "$JOIN_TEST_DIR/test-cxx" "$JOIN_TEST_DIR/bridge" "$JOIN_TEST_DIR/join-bridge.inc" "$JOIN_TEST_DIR/join-no-candidate.inc" "$JOIN_TEST_DIR/join-controller.inc" "$JOIN_TEST_DIR/payload" "$JOIN_TEST_DIR/join-iwn-types.inc" "$JOIN_TEST_DIR/join-iwn-lease.inc" "$JOIN_TEST_DIR/iwn-scan"; rm -rf "$JOIN_TEST_DIR/test-c.dSYM" "$JOIN_TEST_DIR/test-cxx.dSYM" "$JOIN_TEST_DIR/bridge.dSYM" "$JOIN_TEST_DIR/payload.dSYM" "$JOIN_TEST_DIR/iwn-scan.dSYM"; rmdir "$JOIN_TEST_DIR"' EXIT
"${CC:-clang}" -std=c11 -Wall -Wextra -Werror -g \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I "$PROJECT_DIR" "$PROJECT_DIR/tests/net80211_join_attempt_test.c" \
    -o "$JOIN_TEST_DIR/test-c"
"$JOIN_TEST_DIR/test-c"
"${CXX:-clang++}" -x c++ -std=c++17 -Wall -Wextra -Werror \
    -Wno-missing-field-initializers -g \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I "$PROJECT_DIR" "$PROJECT_DIR/tests/net80211_join_attempt_test.c" \
    -o "$JOIN_TEST_DIR/test-cxx"
"$JOIN_TEST_DIR/test-cxx"
join_bridge_source() {
    if [ -n "${JOIN_BRIDGE_NEGATIVE_REF:-}" ]; then
        git -C "$PROJECT_DIR" show "$JOIN_BRIDGE_NEGATIVE_REF:itl80211/openbsd/net80211/ieee80211_proto.c"
    else
        cat "$PROJECT_DIR/itl80211/openbsd/net80211/ieee80211_proto.c"
    fi
}
join_bridge_source | awk '
    /^ieee80211_wcl_join_(begin|scan_generation)\(/ { selected=1; print "u_int64_t" }
    /^ieee80211_wcl_join_(cancel|cleanup_done)\(/ { selected=1; print "void" }
    /^ieee80211_wcl_join_(copy_current|generation_current|note_success|fail|failure_pending|scan_current|scan_failed|state_identity)\(/ { selected=1; print "int" }
    selected { print }
    selected && /^}/ { selected=0 }
' > "$JOIN_TEST_DIR/join-bridge.inc"
# Execute the actual no-candidate branch in addition to the whole bridge
# helpers. Candidate discovery and next physical scan remain test boundaries;
# this is deliberately not described as a complete end_scan/RF execution.
awk '/^    notfound:/ {
         selected=1;
         print "static void complete_no_candidate(ieee80211com *ic, bool bgscan, uint64_t join_generation)";
         print "{ auto *ifp = &ic->ic_if;";
         next;
     }
     selected && /^    }$/ { print "}"; exit }
     selected { print }' \
    "$PROJECT_DIR/itl80211/openbsd/net80211/ieee80211_node.c" > "$JOIN_TEST_DIR/join-no-candidate.inc"
"${CXX:-clang++}" -std=c++17 -Wall -Wextra -Werror -g \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I "$PROJECT_DIR" -I "$JOIN_TEST_DIR" \
    "$PROJECT_DIR/tests/net80211_join_request_bridge_test.cpp" \
    -o "$JOIN_TEST_DIR/bridge"
"$JOIN_TEST_DIR/bridge"
awk '/^enum iwn_scan_lease_owner/ { selected=1 }
     selected { print }
     selected && /^struct iwn_wcl_initial_scan_pending/ { last=1 }
     last && /^};/ { exit }' \
    "$PROJECT_DIR/itlwm/hal_iwn/if_iwnvar.h" > "$JOIN_TEST_DIR/join-iwn-types.inc"
awk '/^struct iwn_scan_lease_terminal/ { selected=1 }
     selected { print }
     selected && /^};/ { selected=0 }
     /^iwn_scan_lease_clear_locked\(/ { selected=1; print "static void"; print }
     /^iwn_scan_lease_(live_locked|owner_is_wcl|owner_is_wcl_initial|reserve|claim_terminal|begin_continuation|restore_continuation|finish_terminal)\(/ { selected=1; print "static bool"; print }
     /^iwn_scan_lease_take_join_cleanup\(/ { selected=1; print "static u_int64_t"; print }
     /^iwn_sae_engine_(request_join_retirement|finish_join_retirement|wake_join_retirement)\(/ { selected=1; print "static void"; print }
     /^iwn_sae_tx_finish_join_retirement\(/ { selected=1; print "static void"; print }
     /^iwn_wcl_join_failure_scan\(/ { selected=1; print "void ItlIwn::"; print }
     selected && /^}/ { selected=0 }' \
    "$PROJECT_DIR/itlwm/hal_iwn/ItlIwn.cpp" > "$JOIN_TEST_DIR/join-iwn-lease.inc"
"${CXX:-clang++}" -std=c++17 -Wall -Wextra -Werror -g \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I "$PROJECT_DIR" -I "$JOIN_TEST_DIR" "$PROJECT_DIR/tests/iwn_join_scan_receipt_test.cpp" \
    -o "$JOIN_TEST_DIR/iwn-scan"
"$JOIN_TEST_DIR/iwn-scan"
JOIN_COMPAT_FLAGS=()
if [ "$(uname -s)" != Darwin ]; then
    JOIN_COMPAT_FLAGS=(-I "$PROJECT_DIR/tests/compat")
fi
awk '/^static IOReturn postTahoeWclJoinFailureGated\(/ { selected=1 }
     selected { print } selected && /^}/ { selected=0 }' \
    "$PROJECT_DIR/AirportItlwm/AirportItlwmV2.cpp" > "$JOIN_TEST_DIR/join-controller.inc"
"${CXX:-clang++}" -std=c++17 -Wall -Wextra -Werror -g \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -DITLWM_STANDALONE_REAL_APPLE80211_IOCTL -D__IO80211_TARGET=260000 \
    -DTAHOE_PAYLOAD_BUILDERS_STANDALONE_TEST -I "$JOIN_TEST_DIR" \
    ${JOIN_COMPAT_FLAGS[@]+"${JOIN_COMPAT_FLAGS[@]}"} -I "$PROJECT_DIR" -I "$PROJECT_DIR/include" \
    -I "$PROJECT_DIR/itl80211/openbsd" \
    "$PROJECT_DIR/tests/tahoe_wcl_join_failure_test.cpp" \
    -o "$JOIN_TEST_DIR/payload"
"$JOIN_TEST_DIR/payload"
python3 "$PROJECT_DIR/tests/iwn_join_failure_wiring_test.py" "$PROJECT_DIR"
