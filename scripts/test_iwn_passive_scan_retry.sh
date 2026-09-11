#!/usr/bin/env bash
set -euo pipefail
ulimit -c 0
IWN_ABORT_ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
IWN_ABORT_TEST="$(mktemp -d)"
trap 'rm -f "$IWN_ABORT_TEST/types.inc" "$IWN_ABORT_TEST/owner.inc" "$IWN_ABORT_TEST/sender.inc" "$IWN_ABORT_TEST/source.cpp" "$IWN_ABORT_TEST/retry-wrapper.inc" "$IWN_ABORT_TEST/retry-receipt-type.inc" "$IWN_ABORT_TEST/retry-receipt-case.inc" "$IWN_ABORT_TEST/test"; rm -rf "$IWN_ABORT_TEST/test.dSYM"; rmdir "$IWN_ABORT_TEST"' EXIT
awk '/^enum iwn_scan_lease_owner/ { selected=1 }
     selected { print }
     selected && /^struct iwn_scan_lease/ { last=1 }
     last && /^};/ { exit }' \
    "$IWN_ABORT_ROOT/itlwm/hal_iwn/if_iwnvar.h" > "$IWN_ABORT_TEST/types.inc"
awk '/^struct iwn_wcl_initial_scan_pending/ { selected=1 }
     selected { print } selected && /^};/ { selected=0 }' \
    "$IWN_ABORT_ROOT/itlwm/hal_iwn/if_iwnvar.h" >> "$IWN_ABORT_TEST/types.inc"
awk '/^struct iwn_scan_(abort_)?doorbell_context/ { selected=1 }
     /^struct iwn_scan_lease_terminal/ { selected=1 }
     selected { print } selected && /^};/ { selected=0 }' \
    "$IWN_ABORT_ROOT/itlwm/hal_iwn/ItlIwn.cpp" >> "$IWN_ABORT_TEST/types.inc"
awk '/^iwn_scan_lease_(live_locked|owner_is_wcl|owner_is_wcl_initial|mark_abort|claim_terminal)\(/ { selected=1; print "static bool" }
     /^iwn_scan_lease_abort_submission_failed\(/ { selected=1; print "static void" }
     /^iwn_scan_abort_prepare_doorbell\(/ { selected=1; print "static bool" }
     /^iwn_scan_abort_finish_doorbell\(/ { selected=1; print "static void" }
     /^iwn_scan_lease_prepare_doorbell\(/ { selected=1; print "static bool" }
     /^iwn_scan_lease_finish_doorbell\(/ { selected=1; print "static void" }
     /^iwn_scan_lease_(begin_passive_retry|restore_continuation)\(/ { selected=1; print "static bool" }
     /^iwn_scan_lease_passive_retry_channel\(/ { selected=1; print "static u_int8_t" }
     /^iwn_scan_lease_note_passive_result\(/ { selected=1; print "static void" }
     /^iwn_scan_abort_command\(/ { selected=1; print "int ItlIwn::" }
     /^iwn_wnm_bgscan_abort\(/ { selected=1; print "int ItlIwn::" }
     selected { print } selected && /^}/ { selected=0 }' \
    "$IWN_ABORT_ROOT/itlwm/hal_iwn/ItlIwn.cpp" > "$IWN_ABORT_TEST/owner.inc"
if [ -n "${IWN_ABORT_NEGATIVE_REF:-}" ]; then
    git -C "$IWN_ABORT_ROOT" show "$IWN_ABORT_NEGATIVE_REF:itlwm/hal_iwn/ItlIwn.cpp" > "$IWN_ABORT_TEST/source.cpp"
    IWN_ABORT_TEST_FLAG=-DIWN_ABORT_BASELINE
else
    cp "$IWN_ABORT_ROOT/itlwm/hal_iwn/ItlIwn.cpp" "$IWN_ABORT_TEST/source.cpp"
    IWN_ABORT_TEST_FLAG=-DIWN_ABORT_CURRENT
fi
# Keep the current complete sender for its zero-length-copy guard, but replace
# the complete old abort caller in baseline mode. Its untagged call is unchanged.
awk '/^iwn_cmd(_with_doorbell_hook)?\(/ { selected=1; print "int ItlIwn::" }
     selected { print } selected && /^}/ { selected=0 }' \
    "$IWN_ABORT_ROOT/itlwm/hal_iwn/ItlIwn.cpp" > "$IWN_ABORT_TEST/sender.inc"
awk '/^iwn_scan_abort\(/ { selected=1; print "void ItlIwn::" }
     selected { print } selected && /^}/ { selected=0 }' \
    "$IWN_ABORT_TEST/source.cpp" >> "$IWN_ABORT_TEST/sender.inc"
awk '/^iwn_scan_retry_passive_2ghz\(/ { selected=1; print "static bool" }
     selected { print } selected && /^}/ { selected=0 }' \
    "$IWN_ABORT_ROOT/itlwm/hal_iwn/ItlIwn.cpp" > "$IWN_ABORT_TEST/retry-wrapper.inc"
awk '/^struct iwn_scan_results/ { selected=1 }
     selected { print } selected && /^} __packed;/ { selected=0 }' \
    "$IWN_ABORT_ROOT/itlwm/hal_iwn/if_iwnreg.h" > "$IWN_ABORT_TEST/retry-receipt-type.inc"
awk '/^        case IWN_SCAN_RESULTS:/ { selected=1 }
     /^        case IWN_STOP_SCAN:/ { selected=0 }
     selected { print }' \
    "$IWN_ABORT_ROOT/itlwm/hal_iwn/ItlIwn.cpp" > "$IWN_ABORT_TEST/retry-receipt-case.inc"
"${CXX:-clang++}" -std=c++17 -Wall -Wextra -Werror -Wno-sign-compare -g \
    -fsanitize=address,undefined -fno-omit-frame-pointer "$IWN_ABORT_TEST_FLAG" \
    -I "$IWN_ABORT_ROOT" -I "$IWN_ABORT_TEST" \
    "$IWN_ABORT_ROOT/tests/iwn_passive_scan_retry_test.cpp" -o "$IWN_ABORT_TEST/test"
"$IWN_ABORT_TEST/test"
