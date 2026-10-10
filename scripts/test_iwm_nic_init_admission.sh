#!/usr/bin/env bash
# Complete APM, NIC-init and firmware-start bodies. MMIO, PCI, downstream
# ring setup and firmware delivery are explicit boundaries, not radio success.
set -euo pipefail
ulimit -c 0
NIC_INIT_ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
NIC_INIT_TEST="$(mktemp -d)"
trap 'rm -f "$NIC_INIT_TEST/registers.inc" "$NIC_INIT_TEST/init.inc" "$NIC_INIT_TEST/test"; rm -rf "$NIC_INIT_TEST/test.dSYM"; rmdir "$NIC_INIT_TEST"' EXIT
awk '
    /^#define[[:space:]]+IWM_(CSR_GIO_CHICKEN_BITS|CSR_DBG_HPET_MEM_REG|CSR_HW_IF_CONFIG_REG|CSR_GP_CNTRL|CSR_MAC_SHADOW_REG_CTRL|CSR_UCODE|APMG_|OSC_CLK|PRPH_BASE)/ { print }
    /^#define[[:space:]]+IWM_CSR_INT[[:space:]]/ { print }
' "$NIC_INIT_ROOT/itlwm/hal_iwm/if_iwmreg.h" > "$NIC_INIT_TEST/registers.inc"
awk '/^#define[[:space:]]+IWM_DEVICE_FAMILY_/ { print }' \
    "$NIC_INIT_ROOT/itlwm/hal_iwm/if_iwmvar.h" >> "$NIC_INIT_TEST/registers.inc"
source_text() {
    if [ -n "${NIC_INIT_BASELINE:-}" ]; then
        git -C "$NIC_INIT_ROOT" show "$NIC_INIT_BASELINE:$1"
    else
        sed -n '1,$p' "$NIC_INIT_ROOT/$1"
    fi
}
for nic_init_source in itlwm/hal_iwm/hw.cpp itlwm/hal_iwm/fw.cpp; do
    source_text "$nic_init_source" | awk '
        /^iwm_(apm_init|nic_init|start_fw)\(/ { selected=1; print "int ItlIwm::" }
        selected { print } selected && /^}/ { selected=0 }
    ' >> "$NIC_INIT_TEST/init.inc"
done
"${CXX:-clang++}" -std=c++17 -g -Wall -Wextra -Werror \
    -Wno-unused-parameter -Wno-unused-function -fsanitize=address,undefined \
    -fno-omit-frame-pointer -I "$NIC_INIT_TEST" \
    "$NIC_INIT_ROOT/tests/iwm_nic_init_admission_test.cpp" -o "$NIC_INIT_TEST/test"
"$NIC_INIT_TEST/test" "${1:-all}" "${2:-9000}"
