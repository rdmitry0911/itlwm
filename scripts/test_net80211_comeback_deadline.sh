#!/bin/bash
# Execute complete production deadline/watchdog/continuation functions.
set -euo pipefail
ulimit -c 0
root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
comeback_test_dir="$(mktemp -d)"
cleanup() {
    rm -f "$comeback_test_dir/retry.inc" "$comeback_test_dir/functions.inc" "$comeback_test_dir/watchdog.inc" "$comeback_test_dir/test"
    if [[ -d "$comeback_test_dir/test.dSYM" ]]; then rm -r "$comeback_test_dir/test.dSYM"; fi
    rmdir "$comeback_test_dir"
}
trap cleanup EXIT
awk '/^struct ieee80211_assoc_comeback_retry / { selected=1 }
     selected { print }
     selected && /^};/ { found=1; exit }
     END { if (!found) exit 1 }' \
    "$root/itl80211/openbsd/net80211/ieee80211_var.h" > "$comeback_test_dir/retry.inc"
extract_function() {
    awk -v name="$1" -v prefix="$2" '
        $0 ~ ("^" name "\\(") { selected=1; print prefix }
        selected { print }
        selected && /^}$/ { found=1; exit }
        END { if (!found) exit 1 }' "$root/itl80211/openbsd/net80211/ieee80211.c"
}
{
    extract_function ieee80211_assoc_comeback_set_deadline int
    extract_function ieee80211_assoc_comeback_retry_current 'static int'
    extract_function ieee80211_assoc_comeback_retry_ready int
    extract_function ieee80211_assoc_comeback_retry_abort int
    extract_function ieee80211_assoc_comeback_retry_complete int
} > "$comeback_test_dir/functions.inc"
awk '/^ieee80211_watchdog\(struct _ifnet/ { selected=1; print "void" }
     selected { print }
     selected && /^}$/ { found=1; exit }
     END { if (!found) exit 1 }' \
    "${COMEBACK_WATCHDOG_BASELINE_FILE:-$root/itl80211/openbsd/net80211/ieee80211.c}" > "$comeback_test_dir/watchdog.inc"
"${CXX:-clang++}" -std=c++17 -Wall -Wextra -Werror -g \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I "$root" -I "$comeback_test_dir" \
    "$root/tests/net80211_comeback_deadline_test.cpp" -o "$comeback_test_dir/test"
"$comeback_test_dir/test" "${1:-all}"
