#!/usr/bin/env bash
# Complete actual bodies; the caller supplies the kernel/IRQ/DMA doubles.
set -euo pipefail
root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
awk '
    /^(int|void|bool) ItlIwm::$/ { type=$0 }
    /^static bool$/ { type=$0 }
    /^iwm_cmdq_[[:alnum:]_]+\(/ || /^iwm_radio_abort_command_waits\(/ { selected=1; print type }
    selected { print } selected && /^}/ { selected=0 }
' "$root/itlwm/hal_iwm/phy.cpp"
if [ -n "${IWM_COMMAND_CANCELLATION_NEGATIVE_REF:-}" ]; then
    git -C "$root" show "$IWM_COMMAND_CANCELLATION_NEGATIVE_REF:itlwm/hal_iwm/phy.cpp"
else
    sed -n '1,$p' "$root/itlwm/hal_iwm/phy.cpp"
fi | awk '
    /^iwm_send_cmd\(/ { selected=1; print "int ItlIwm::" }
    /^iwm_cmd_done\(/ { selected=1; print "void ItlIwm::" }
    selected { print } selected && /^}/ { selected=0 }
'
