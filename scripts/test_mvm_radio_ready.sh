#!/usr/bin/env bash
# Actual ready producer/receipt validator, upper mailbox/action and carrier.
set -euo pipefail
ulimit -c 0
root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
family=${1:?iwm or iwx required}
case "$family" in
    iwm) mixed=Iwm; upper=IWM; hal=itlwm/hal_iwm/ItlIwm.cpp ;;
    iwx) mixed=Iwx; upper=IWX; hal=itlwm/hal_iwx/ItlIwx.cpp ;;
    *) exit 2 ;;
esac
ready_test_dir="$(mktemp -d)"
trap 'rm -f "$ready_test_dir/hal.inc" "$ready_test_dir/controller.inc" "$ready_test_dir/test"; rm -rf "$ready_test_dir/test.dSYM"; rmdir "$ready_test_dir"' EXIT
normalize() { sed -e "s/$family/mvm/g" -e "s/$mixed/Mvm/g" -e "s/$upper/MVM/g"; }
awk '
    /^enableForRadioPowerOn\(/ { selected=1; print "IOReturn ItlMvm::" }
    /^cancelRadioPowerOnRequest\(/ { selected=1; print "void ItlMvm::" }
    /^radioPowerOnRequestEpoch\(/ { selected=1; print "uint64_t ItlMvm::" }
    /^isRadioReadyCurrent\(/ { selected=1; print "bool ItlMvm::" }
    selected { print } selected && /^}/ { selected=0 }' "$root/$hal" |
    normalize > "$ready_test_dir/hal.inc"
if [ -n "${MVM_RADIO_READY_NEGATIVE_PRODUCER_REF:-}" ]; then
    git -C "$root" show "$MVM_RADIO_READY_NEGATIVE_PRODUCER_REF:$hal"
else
    sed -n '1,$p' "$root/$hal"
fi | awk '/^noteWclScanRadioReady\(/ { selected=1; print "void ItlMvm::" }
    selected { print } selected && /^}/ { selected=0 }' |
    normalize >> "$ready_test_dir/hal.inc"
awk '
    /^armDeferredPowerOnAvailability\(/ { selected=1; print "uint64_t AirportItlwm::" }
    /^void AirportItlwm::cancelDeferredPowerOnAvailabilityRaw\(/ { selected=1 }
    /^cancelDeferredPowerOnAvailabilityEpochRaw\(/ { selected=1; print "bool AirportItlwm::" }
    /^publishDeferredPowerAvailabilityGated\(/ { selected=1; print "IOReturn AirportItlwm::" }
    /^noteRadioReady\(/ { selected=1; print "void AirportItlwm::" }
    /^void AirportItlwm::dispatchRadioReady\(/ { selected=1 }
    selected { print } selected && /^}/ { selected=0 }' \
    "$root/AirportItlwm/AirportItlwmV2.cpp" > "$ready_test_dir/controller.inc"
legacy_mode=0
if [ -n "${MVM_RADIO_READY_NEGATIVE_UPPER_REF:-}" ]; then
    legacy_mode=1
fi
if [ -n "${MVM_RADIO_READY_NEGATIVE_UPPER_REF:-}" ]; then
    git -C "$root" show "$MVM_RADIO_READY_NEGATIVE_UPPER_REF:AirportItlwm/AirportItlwmV2.cpp"
else
    sed -n '1,$p' "$root/AirportItlwm/AirportItlwmV2.cpp"
fi | awk '/^bool AirportItlwm::noteRadioScanReadyAndQueuePowerOnAvailability\(/ { selected=1 }
    selected { print } selected && /^}/ { selected=0 }' >> "$ready_test_dir/controller.inc"
awk '/^static void wclPhysicalScanTerminalInterruptAction\(/ { selected=1 }
    /^static void$/ { previous=1; next }
    /^wclPhysicalScanTerminalInterruptAction\(/ && previous { selected=1; print "static void" }
    { previous=0 }
    selected { print } selected && /^}/ { selected=0 }' \
    "$root/AirportItlwm/AirportItlwmV2.cpp" >> "$ready_test_dir/controller.inc"
"${CXX:-clang++}" -std=c++17 -Wall -Wextra -Werror -Wno-unused-function \
    -Wno-unused-variable -g -fsanitize=address,undefined -fno-omit-frame-pointer \
    -DMVM_RADIO_READY_LEGACY_NEGATIVE="$legacy_mode" \
    -I "$root" -I "$root/include" -I "$ready_test_dir" \
    "$root/tests/mvm_radio_ready_test.cpp" -o "$ready_test_dir/test"
"$ready_test_dir/test"
