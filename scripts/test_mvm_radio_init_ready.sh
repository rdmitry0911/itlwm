#!/usr/bin/env bash
# Execute the full lower init body with explicit firmware/sleep doubles.
set -euo pipefail
ulimit -c 0
root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
family=${1:?iwm or iwx required}
scenario=${2:-all}
case "$family" in
    iwm) mixed=Iwm; upper=IWM; source=itlwm/hal_iwm/mac80211.cpp; hal=itlwm/hal_iwm/ItlIwm.cpp; definition=mvm_init; is_iwm=1 ;;
    iwx) mixed=Iwx; upper=IWX; source=itlwm/hal_iwx/ItlIwx.cpp; hal=$source; definition=mvm_init_internal; is_iwm=0 ;;
    *) exit 2 ;;
esac
init_test_dir="$(mktemp -d)"
trap 'rm -f "$init_test_dir/init.inc" "$init_test_dir/test"; rm -rf "$init_test_dir/test.dSYM"; rmdir "$init_test_dir"' EXIT
sed -e "s/$family/mvm/g" -e "s/$mixed/Mvm/g" -e "s/$upper/MVM/g" "$root/$hal" |
    awk '/^isRadioScanReady\(/ || /^isRadioReadyCurrent\(/ { selected=1; print "bool ItlMvm::" }
         selected { print } selected && /^}/ { selected=0 }' > "$init_test_dir/init.inc"
if [ -n "${MVM_RADIO_INIT_NEGATIVE_REF:-}" ]; then
    git -C "$root" show "$MVM_RADIO_INIT_NEGATIVE_REF:$source"
else
    sed -n '1,$p' "$root/$source"
fi | sed -e "s/$family/mvm/g" -e "s/$mixed/Mvm/g" -e "s/$upper/MVM/g" |
    awk -v definition="$definition" '
        $0 ~ ("^" definition "\\(") { selected=1; print "int ItlMvm::" }
        selected { print } selected && /^}/ { selected=0 }
    ' >> "$init_test_dir/init.inc"
test -s "$init_test_dir/init.inc"
"${CXX:-clang++}" -std=c++17 -Wall -Wextra -Werror -Wno-unused-function \
    -g -fsanitize=address,undefined -fno-omit-frame-pointer \
    -DMVM_INIT_IWM="$is_iwm" -I "$init_test_dir" -I "$root/include" \
    "$root/tests/mvm_radio_init_ready_test.cpp" -o "$init_test_dir/test"
if [ "$scenario" = all ]; then
    for init_case in normal lost-wake consumer-advances ready-on-timeout \
        early-consumer no-receipt hardware-failure scan-rejected \
        reset-replacement shutdown-replacement monitor receipt-validity; do
        "$init_test_dir/test" "$init_case"
    done
    "$init_test_dir/test" security-before-ready
else
    "$init_test_dir/test" "$scenario"
fi
