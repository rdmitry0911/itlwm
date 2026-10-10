#!/usr/bin/env bash
# Execute complete production radio-power admission and controller transitions.
set -euo pipefail
ulimit -c 0
root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
family=${1:?iwm or iwx required}
case "$family" in
    iwm) mixed=Iwm; upper=IWM; hal=itlwm/hal_iwm/ItlIwm.cpp; rf=itlwm/hal_iwm/hw.cpp ;;
    iwx) mixed=Iwx; upper=IWX; hal=itlwm/hal_iwx/ItlIwx.cpp; rf=$hal ;;
    *) exit 2 ;;
esac
power_test_dir="$(mktemp -d)"
trap 'rm -f "$power_test_dir/hal.inc" "$power_test_dir/controller.inc" "$power_test_dir/test"; rm -rf "$power_test_dir/test.dSYM"; rmdir "$power_test_dir"' EXIT
read_source() {
    if [ -n "${MVM_POWER_NEGATIVE_REF:-}" ]; then
        git -C "$root" show "$MVM_POWER_NEGATIVE_REF:$1"
    else
        sed -n '1,$p' "$root/$1"
    fi
}
normalize() { sed -e "s/$family/mvm/g" -e "s/$mixed/Mvm/g" -e "s/$upper/MVM/g"; }
read_source "$rf" | awk -v name="${family}_check_rfkill" '
    $0 ~ "^" name "\\(" { selected=1; print "int ItlMvm::" }
    selected { print } selected && /^}/ { selected=0 }' | normalize > "$power_test_dir/hal.inc"
sed -n '1,$p' "$root/$hal" | awk '
    /^enableForRadioPowerOn\(/ { selected=1; print "IOReturn ItlMvm::" }
    /^cancelRadioPowerOnRequest\(/ || /^reportRadioPowerOnFailure\(/ { selected=1; print "void ItlMvm::" }
    /^radioPowerOnRequestEpoch\(/ { selected=1; print "uint64_t ItlMvm::" }
    selected { print } selected && /^}/ { selected=0 }' | normalize >> "$power_test_dir/hal.inc"
read_source "$hal" | awk '
    /^checkRadioPowerOnAdmission\(/ { selected=1; print "IOReturn ItlMvm::" }
    selected { print } selected && /^}/ { selected=0 }' | normalize >> "$power_test_dir/hal.inc"
if ! grep -q '^checkRadioPowerOnAdmission(' "$power_test_dir/hal.inc"; then
    # The historical boundary had no radio admission check. This keeps the
    # old complete controller compilable for a behavioral negative control.
    printf '\nIOReturn ItlMvm::checkRadioPowerOnAdmission() { return kIOReturnSuccess; }\n' >> "$power_test_dir/hal.inc"
fi
if [ -n "${MVM_POWER_NEGATIVE_ENABLE_REF:-}" ]; then
    git -C "$root" show "$MVM_POWER_NEGATIVE_ENABLE_REF:AirportItlwm/AirportItlwmV2.cpp"
else
    read_source AirportItlwm/AirportItlwmV2.cpp
fi | awk '
    /^IOReturn AirportItlwm::enableAdapter\(/ { selected=1 }
    selected { print } selected && /^}/ { selected=0 }' |
    sed 's/enableAdapter(IONetworkInterface \*netif)/enableAdapter(IONetworkInterface *netif, uint64_t radioPowerOnEpoch)/' > "$power_test_dir/controller.inc"
awk '/^bool AirportItlwm::retireFailedRadioPowerOn\(/ { selected=1 }
    selected { print } selected && /^}/ { selected=0 }' \
    "$root/AirportItlwm/AirportItlwmV2.cpp" >> "$power_test_dir/controller.inc"
read_source AirportItlwm/AirportItlwmV2.cpp | awk '
    /^int AirportItlwm::handlePowerStateChangeCore\(/ { selected=1 }
    selected { print } selected && /^}/ { selected=0 }' >> "$power_test_dir/controller.inc"
sed -n '1,$p' "$root/AirportItlwm/AirportItlwmV2.cpp" | awk '
    /^void AirportItlwm::performTahoeBootChipImage\(/ { selected=1 }
    /^getPOWER\(/ || /^setPOWER\(/ { selected=1; print "IOReturn AirportItlwm::" }
    selected { print } selected && /^}/ { selected=0 }' >> "$power_test_dir/controller.inc"
"${CXX:-clang++}" -std=c++17 -Wall -Wextra -Werror -Wno-unused-parameter -g \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I "$root/include" -I "$power_test_dir" "$root/tests/mvm_radio_power_admission_test.cpp" \
    -o "$power_test_dir/test"
"$power_test_dir/test"
