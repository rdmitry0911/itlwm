#!/usr/bin/env bash
set -euo pipefail
ulimit -c 0
scan_gap_repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
scan_gap_tmp=$(mktemp -d)
cleanup() {
    rm -f "$scan_gap_tmp/types.inc" "$scan_gap_tmp/production.inc" "$scan_gap_tmp/test"
    if [ -d "$scan_gap_tmp/test.dSYM" ]; then rm -r "$scan_gap_tmp/test.dSYM"; fi
    rmdir "$scan_gap_tmp"
}
trap cleanup EXIT
awk '/^enum iwn_scan_lease_owner/ { selected=1 }
     selected { print }
     selected && /^struct iwn_scan_lease/ { last=1 }
     last && /^};/ { exit }' \
    "$scan_gap_repo/itlwm/hal_iwn/if_iwnvar.h" > "$scan_gap_tmp/types.inc"
awk '/^struct iwn_wcl_initial_scan_pending/ { selected=1 }
     selected { print } selected && /^};/ { selected=0 }' \
    "$scan_gap_repo/itlwm/hal_iwn/if_iwnvar.h" >> "$scan_gap_tmp/types.inc"
awk '/^struct iwn_scan_(lease_terminal|doorbell_context)/ { selected=1 }
     selected { print } selected && /^};/ { selected=0 }' \
    "$scan_gap_repo/itlwm/hal_iwn/ItlIwn.cpp" >> "$scan_gap_tmp/types.inc"
awk '/^iwn_scan_lease_(clear_locked|retire_after_hardware_stop)\(/ { selected=1; print "static void" }
     /^iwn_wcl_initial_scan_pending_clear_locked\(/ { selected=1; print "static void" }
     /^iwn_scan_lease_(live_locked|owner_is_wcl|owner_is_wcl_initial|reserve|rollback|arm_submission|mark_abort|claim_terminal|finish_terminal|initial_handoff_valid_locked|prepare_doorbell)\(/ { selected=1; print "static bool" }
     /^iwn_wcl_(background_source_current_locked|background_scan_queue|initial_scan_claim_generic_terminal|background_handoff_prearm|scan_plan_has_eligible_band)\(/ { selected=1; print "static bool" }
     /^iwn_wcl_scan_initial_band\(/ { selected=1; print "static int" }
     /^iwn_scan_lease_finish_doorbell\(/ { selected=1; print "static void" }
     /^iwn_scan_lease_begin_hardware_invalidation\(/ { selected=1; print "static enum iwn_scan_lease_owner" }
     /^iwn_scan_lease_take_join_cleanup\(/ { selected=1; print "static u_int64_t" }
     /^iwn_scan_restore_prearmed_background\(/ { selected=1; print "static void" }
     /^iwn_scan_schedule_fatal_recovery\(/ { selected=1; print "static void" }
     /^(beginWclBackgroundScan|beginWclBackgroundScanAfterRoam|abortWclBackgroundScan)\(/ { selected=1; print "IOReturn ItlIwn::" }
     /^(iwn_scan_lease_replay_task|invalidateWclBackgroundScan)\(/ { selected=1; print "void ItlIwn::" }
     /^iwn_scan_start\(/ { selected=1; print "int ItlIwn::" }
     selected { print } selected && /^}/ { selected=0 }' \
    "$scan_gap_repo/itlwm/hal_iwn/ItlIwn.cpp" > "$scan_gap_tmp/production.inc"
"${CXX:-clang++}" -std=c++17 -Wall -Wextra -Werror -g \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I "$scan_gap_repo" -I "$scan_gap_repo/tests" -I "$scan_gap_tmp" \
    "$scan_gap_repo/tests/iwn_wcl_scan_abort_gap_test.cpp" -o "$scan_gap_tmp/test"
"$scan_gap_tmp/test" "$@"
