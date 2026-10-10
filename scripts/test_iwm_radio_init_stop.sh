#!/usr/bin/env bash
# Full production init/stop/disable/activate bodies, explicit hardware doubles.
set -euo pipefail
ulimit -c 0
root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
init_stop_dir="$(mktemp -d)"
trap 'rm -f "$init_stop_dir/lifecycle.inc" "$init_stop_dir/test"; rm -rf "$init_stop_dir/test.dSYM"; rmdir "$init_stop_dir"' EXIT
init_stop_ref=${IWM_RADIO_INIT_STOP_NEGATIVE_REF:-}
source_text() {
    if [ -n "$init_stop_ref" ]; then
        git -C "$root" show "$init_stop_ref:$1"
    else
        sed -n '1,$p' "$root/$1"
    fi
}
source_text itlwm/hal_iwm/ItlIwm.cpp | awk '
    /^disable\(/ || /^enable\(/ { selected=1; print "IOReturn ItlIwm::" }
    /^scanCommandResetEpoch\(/ { selected=1; print "uint64_t ItlIwm::" }
    /^reopenScanCommands\(/ || /^isRadioScanReady\(/ || /^isRadioReadyCurrent\(/ { selected=1; print "bool ItlIwm::" }
    selected { print } selected && /^}/ { selected=0 }
' > "$init_stop_dir/lifecycle.inc"
source_text itlwm/hal_iwm/mac80211.cpp | awk '
    /^iwm_init\(/ || /^iwm_activate\(/ { selected=1; print "int ItlIwm::" }
    /^iwm_stop(_internal)?\(/ { selected=1; print "void ItlIwm::" }
    /^iwm_radio_(init_begin|init_current|init_current_locked|stop_begin)\(/ { selected=1; print "bool ItlIwm::" }
    /^iwm_radio_(init_end|stop_drain|stop_end)\(/ { selected=1; print "void ItlIwm::" }
    selected { print } selected && /^}/ { selected=0 }
' >> "$init_stop_dir/lifecycle.inc"
if [ -n "$init_stop_ref" ]; then
    # Keep historical complete bodies compilable without supplying the new
    # owner to that old body; gate-only scenarios are not historical controls.
    awk '/^iwm_radio_init_begin\(/ || /^iwm_radio_init_current(_locked)?\(/ || /^iwm_radio_stop_begin\(/ { selected=1; print "bool ItlIwm::" }
        /^iwm_radio_init_end\(/ || /^iwm_radio_stop_drain\(/ || /^iwm_radio_stop_end\(/ { selected=1; print "void ItlIwm::" }
        selected { print } selected && /^}/ { selected=0 }' \
        "$root/itlwm/hal_iwm/mac80211.cpp" >> "$init_stop_dir/lifecycle.inc"
fi
"${CXX:-clang++}" -std=c++17 -Wall -Wextra -Werror -Wno-unused-function -Wno-unused-parameter \
    -g -pthread -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I "$init_stop_dir" -I "$root/include" \
    "$root/tests/iwm_radio_init_stop_test.cpp" -o "$init_stop_dir/test"
if [ "${1:-all}" = all ]; then
    for init_stop_case in early-off early-off-on normal timeout hardware-failure overlapping-off admission; do
        "$init_stop_dir/test" "$init_stop_case"
    done
else
    "$init_stop_dir/test" "$1"
fi
