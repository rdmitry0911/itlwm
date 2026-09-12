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
awk '/^iwn_scan_lease_clear_locked\(/ { selected=1; print "static void" }
     /^iwn_scan_lease_(live_locked|owner_is_wcl|reserve|mark_abort|finish_terminal)\(/ { selected=1; print "static bool" }
     /^beginWclBackgroundScan\(/ { selected=1; print "IOReturn ItlIwn::" }
     selected { print } selected && /^}/ { selected=0 }' \
    "$scan_gap_repo/itlwm/hal_iwn/ItlIwn.cpp" > "$scan_gap_tmp/production.inc"
"${CXX:-clang++}" -std=c++17 -Wall -Wextra -Werror -g \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I "$scan_gap_repo/tests" -I "$scan_gap_tmp" \
    "$scan_gap_repo/tests/iwn_wcl_scan_abort_gap_test.cpp" -o "$scan_gap_tmp/test"
"$scan_gap_tmp/test" "$@"
