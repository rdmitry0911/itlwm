#!/usr/bin/env bash
# Full production init worker, activation bridge, failure mailbox and wait.
set -euo pipefail
ulimit -c 0
root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
family=${1:?iwm or iwx required}
case "$family" in
    iwm) mixed=Iwm; upper=IWM; hal=itlwm/hal_iwm/ItlIwm.cpp; worker=itlwm/hal_iwm/mac80211.cpp; mode=0 ;;
    iwx) mixed=Iwx; upper=IWX; hal=itlwm/hal_iwx/ItlIwx.cpp; worker=$hal; mode=1 ;;
    *) exit 2 ;;
esac
radio_test_dir="$(mktemp -d)"
trap 'rm -f "$radio_test_dir/hal.inc" "$radio_test_dir/controller.inc" "$radio_test_dir/test"; rm -rf "$radio_test_dir/test.dSYM"; rmdir "$radio_test_dir"' EXIT
normalize() { sed -e "s/$family/mvm/g" -e "s/$mixed/Mvm/g" -e "s/$upper/MVM/g"; }
awk '
    /^enableForRadioPowerOn\(/ { selected=1; print "IOReturn ItlMvm::" }
    /^cancelRadioPowerOnRequest\(/ || /^reportRadioPowerOnFailure\(/ { selected=1; print "void ItlMvm::" }
    /^radioPowerOnRequestEpoch\(/ { selected=1; print "uint64_t ItlMvm::" }
    /^claimRadioPowerOnRetry\(/ { selected=1; print "uint8_t ItlMvm::" }
    selected { print } selected && /^}/ { selected=0 }' "$root/$hal" |
    normalize > "$radio_test_dir/hal.inc"
if [ -n "${MVM_POWER_FAILURE_NEGATIVE_REF:-}" ]; then
    git -C "$root" show "$MVM_POWER_FAILURE_NEGATIVE_REF:$worker"
else
    sed -n '1,$p' "$root/$worker"
fi | awk -v name="${family}_init_task" '
    $0 ~ "^" name "\\(" { selected=1; print "void ItlMvm::" }
    selected { print } selected && /^}/ { selected=0 }' |
    normalize >> "$radio_test_dir/hal.inc"
awk '
    /^armDeferredPowerOnAvailability\(/ { selected=1; print "uint64_t AirportItlwm::" }
    /^noteRadioPowerOnFailure\(/ { selected=1; print "void AirportItlwm::" }
    /^void AirportItlwm::dispatchRadioPowerOnFailure\(/ { selected=1 }
    /^waitForDeferredPowerOnAvailability\(/ { selected=1; print "IOReturn AirportItlwm::" }
    /^static void wclPhysicalScanTerminalInterruptAction\(/ { selected=1 }
    selected { print } selected && /^}/ { selected=0 }' \
    "$root/AirportItlwm/AirportItlwmV2.cpp" > "$radio_test_dir/controller.inc"
# The action's return type and name are split onto separate lines.
awk '/^static void$/ { previous=1; next }
    /^wclPhysicalScanTerminalInterruptAction\(/ && previous { selected=1; print "static void" }
    { previous=0 }
    selected { print } selected && /^}/ { selected=0 }' \
    "$root/AirportItlwm/AirportItlwmV2.cpp" >> "$radio_test_dir/controller.inc"
"${CXX:-clang++}" -std=c++17 -Wall -Wextra -Werror -Wno-unused-function \
    -Wno-unused-but-set-variable -g -fsanitize=address,undefined \
    -fno-omit-frame-pointer -DMVM_FAMILY_IWX="$mode" \
    -I "$root/include" -I "$radio_test_dir" \
    "$root/tests/mvm_radio_power_failure_test.cpp" -o "$radio_test_dir/test"
"$radio_test_dir/test"
