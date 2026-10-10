#!/usr/bin/env bash
# Complete APM/NIC/start_fw control flow; MMIO/PCI/RX/firmware are doubles.
set -euo pipefail
ulimit -c 0
IWX_NIC_INIT_ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
IWX_NIC_INIT_TEST="$(mktemp -d)"
trap 'rm -f "$IWX_NIC_INIT_TEST/registers.inc" "$IWX_NIC_INIT_TEST/init.inc" "$IWX_NIC_INIT_TEST/test"; rm -rf "$IWX_NIC_INIT_TEST/test.dSYM"; rmdir "$IWX_NIC_INIT_TEST"' EXIT
awk '
    /^#define[[:space:]]+IWX_(CSR_GIO_CHICKEN_BITS|CSR_DBG_HPET_MEM_REG|CSR_HW_IF_CONFIG_REG|CSR_GP_CNTRL|CSR_MAC_SHADOW_REG_CTRL|CSR_UCODE)/ { print }
    /^#define[[:space:]]+IWX_CSR_INT[[:space:]]/ { print }
' "$IWX_NIC_INIT_ROOT/itlwm/hal_iwx/if_iwxreg.h" > "$IWX_NIC_INIT_TEST/registers.inc"
awk '/^#define[[:space:]]+IWX_DEVICE_FAMILY_/ { print }' \
    "$IWX_NIC_INIT_ROOT/itlwm/hal_iwx/if_iwxvar.h" >> "$IWX_NIC_INIT_TEST/registers.inc"
if [ -n "${IWX_NIC_INIT_BASELINE:-}" ]; then
    git -C "$IWX_NIC_INIT_ROOT" show "$IWX_NIC_INIT_BASELINE:itlwm/hal_iwx/ItlIwx.cpp"
else
    sed -n '1,$p' "$IWX_NIC_INIT_ROOT/itlwm/hal_iwx/ItlIwx.cpp"
fi | awk '
    /^iwx_(apm_init|nic_init|start_fw)\(/ { selected=1; print "int ItlIwx::" }
    selected { print } selected && /^}/ { selected=0 }
' > "$IWX_NIC_INIT_TEST/init.inc"
"${CXX:-clang++}" -std=c++17 -g -Wall -Wextra -Werror \
    -Wno-unused-parameter -Wno-unused-function -fsanitize=address,undefined \
    -fno-omit-frame-pointer -I "$IWX_NIC_INIT_TEST" \
    "$IWX_NIC_INIT_ROOT/tests/iwx_nic_init_admission_test.cpp" -o "$IWX_NIC_INIT_TEST/test"
"$IWX_NIC_INIT_TEST/test" "${1:-all}" "${2:-ax210}"
