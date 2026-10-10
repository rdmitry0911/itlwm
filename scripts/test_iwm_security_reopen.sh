#!/usr/bin/env bash
# Full production PMF/TX/engine reopen and engine close/callback fences.
set -euo pipefail
ulimit -c 0
root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
scenario=${1:-all}
security_test_dir="$(mktemp -d)"
trap 'rm -f "$security_test_dir/security.inc" "$security_test_dir/test"; rm -rf "$security_test_dir/test.dSYM"; rmdir "$security_test_dir"' EXIT
historical=0
if [ -n "${IWM_SECURITY_REOPEN_NEGATIVE_REF:-}" ]; then historical=1; fi
security_source() {
    if [ "$historical" = 1 ]; then
        git -C "$root" show "$IWM_SECURITY_REOPEN_NEGATIVE_REF:$1"
    else
        sed -n '1,$p' "$root/$1"
    fi
}
security_source itlwm/hal_iwm/ItlIwm.cpp | awk '
    /^iwm_sae_tx_lifecycle_is_open\(/ { selected=1; print "static bool" }
    /^iwm_sae_tx_generation_advance_locked\(/ { selected=1; print "static void" }
    /^iwm_sae_tx_reopen\(/ { selected=1; print "void ItlIwm::" }
    selected { print } selected && /^}/ { selected=0 }
' > "$security_test_dir/security.inc"
security_source itlwm/hal_iwm/IwmMfpPae.inc | awk '
    /^iwm_mfp_pae_generation_advance_locked\(/ { selected=1; print "static void" }
    /^iwm_mfp_pae_reopen\(/ { selected=1; print "void ItlIwm::" }
    selected { print } selected && /^}/ { selected=0 }
' >> "$security_test_dir/security.inc"
security_source itlwm/hal_iwm/IwmSaeEngine.inc | awk '
    /^iwm_sae_engine_callback_(leave|close|drain)\(/ { selected=1; print "static void" }
    /^iwm_sae_engine_callback_open\(/ { selected=1; print "static bool" }
    /^iwm_sae_task_lifecycle_is_open\(/ { selected=1; print "static bool" }
    /^iwm_sae_engine_generation_advance_locked\(/ { selected=1; print "static u_int64_t" }
    /^iwm_sae_engine_publish_hooks\(/ { selected=1; print "static bool" }
    /^iwm_sae_engine_(stop_begin|reopen)\(/ { selected=1; print "void ItlIwm::" }
    selected { print } selected && /^}/ { selected=0 }
' >> "$security_test_dir/security.inc"
# Owner helpers stay current in negatives; only complete historical
# reopen/publication bodies are substituted, with explicit signature adapters.
awk '/^iwm_radio_(init_begin|init_current|init_current_locked|stop_begin)\(/ { selected=1; print "bool ItlIwm::" }
     /^iwm_radio_(init_end|stop_end)\(/ { selected=1; print "void ItlIwm::" }
     selected { print } selected && /^}/ { selected=0 }' \
    "$root/itlwm/hal_iwm/mac80211.cpp" >> "$security_test_dir/security.inc"
"${CXX:-clang++}" -std=c++17 -Wall -Wextra -Werror -Wno-unused-function \
    -g -fsanitize=address,undefined -fno-omit-frame-pointer \
    -DIWM_SECURITY_REOPEN_HISTORICAL="$historical" \
    -I "$security_test_dir" "$root/tests/iwm_security_reopen_test.cpp" \
    -o "$security_test_dir/test"
if [ "$scenario" = all ]; then
    for reopen_case in normal publication-owner-lock detach pending-retirement runtime-disabled \
        stop-before-claim stop-during-drain stop-after-claim \
        shutdown-mfp shutdown-tx shutdown-engine \
        stale-mfp stale-tx stale-engine no-init-mfp no-init-tx no-init-engine \
        shutdown-publication shutdown-after-hook-snapshot; do
        "$security_test_dir/test" "$reopen_case"
    done
else
    "$security_test_dir/test" "$scenario"
fi
