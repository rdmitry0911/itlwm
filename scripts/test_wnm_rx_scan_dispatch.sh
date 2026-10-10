#!/usr/bin/env bash
# Complete RX/parser and timer dispatch bodies; lower commands and kernel
# scheduling are explicit doubles, not on-air BTM qualification.
set -euo pipefail
ulimit -c 0
root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
wnm_test_dir=$(mktemp -d)
trap 'rm -f "$wnm_test_dir/wnm-record.inc" "$wnm_test_dir/wnm-dispatch.inc" "$wnm_test_dir/test"; rm -rf "$wnm_test_dir/test.dSYM"; rmdir "$wnm_test_dir"' EXIT
awk '/^#define IEEE80211_WNM_HANDOFF_/ { print }
     /^struct ieee80211_wnm_bss_transition \{/ { selected=1 }
     selected { print } selected && /^};/ { selected=0 }' \
    "$root/itl80211/openbsd/net80211/ieee80211_var.h" > "$wnm_test_dir/wnm-record.inc"
{
    if [ -n "${BTM_SOURCE_NEGATIVE_REF:-}" ]; then
        git -C "$root" show "$BTM_SOURCE_NEGATIVE_REF:itl80211/openbsd/net80211/ieee80211_proto.c"
    else
        sed -n '1,$p' "$root/itl80211/openbsd/net80211/ieee80211_proto.c"
    fi | awk '/^ieee80211_(wnm_bss_transition_clear_locked|wnm_bss_transition_validate_scan_source_locked|wnm_bss_transition_clear|wnm_bss_transition_fresh_scan_started|wnm_bss_transition_scan_end)\(/ { selected=1; print "void" }
         /^ieee80211_wnm_bss_transition_request_generation\(/ { selected=1; print "u_int64_t" }
         /^ieee80211_(bssid_is_unicast_nonzero|wnm_bss_transition_arm|wnm_bss_transition_defer_fresh_scan|wnm_bss_transition_fresh_scan_pending|wnm_bss_transition_retry_fresh_scan|wnm_bss_transition_active|wnm_bss_transition_scan_start|wnm_bss_transition_candidate_disposition|wnm_bss_transition_confirm_candidate|wnm_bss_transition_copy_retarget|wnm_bss_transition_source_identity_current_locked|wnm_bss_transition_source_current_locked|wnm_bss_transition_handoff_current_locked)\(/ { selected=1; print "int" }
         selected { print } selected && /^}/ { selected=0 }'
    for wnm_source in ieee80211_input.c ieee80211.c; do
        if [ -n "${BTM_RX_NEGATIVE_REF:-}" ]; then
            git -C "$root" show "$BTM_RX_NEGATIVE_REF:itl80211/openbsd/net80211/$wnm_source"
        else
            sed -n '1,$p' "$root/itl80211/openbsd/net80211/$wnm_source"
        fi
    done | awk '
        /^ieee80211_begin_wnm_bgscan\(/ { selected=1; print "int" }
        /^ieee80211_(recv_wnm_bss_transition_req|wnm_bgscan_retry_timeout)\(/ { selected=1; print "void" }
        selected { print } selected && /^}/ { selected=0 }
    '
} > "$wnm_test_dir/wnm-dispatch.inc"
"${CXX:-clang++}" -std=c++17 -Wall -Wextra -Werror \
    -Wno-misleading-indentation -g \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I "$wnm_test_dir" "$root/tests/wnm_rx_scan_dispatch_test.cpp" \
    -o "$wnm_test_dir/test"
"$wnm_test_dir/test" "${1:-all}"
