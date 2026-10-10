#!/usr/bin/env bash
# Execute the complete production terminal dispatcher, task lease, engine
# callback admission, fallback retirement and controller value-copy mailbox.
set -euo pipefail
ulimit -c 0
root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
terminal_test_dir="$(mktemp -d)"
trap 'rm -f "$terminal_test_dir/worker.inc" "$terminal_test_dir/controller.inc" "$terminal_test_dir/test"; rm -rf "$terminal_test_dir/test.dSYM"; rmdir "$terminal_test_dir"' EXIT
awk '
    /^iwx_sae_tx_ticket_(is_direct|cancelled_locked)\(/ { selected=1; print "static bool" }
    /^iwx_task_gate_(close|enter)\(/ { selected=1; print "bool ItlIwx::" }
    /^iwx_task_gate_(leave|drain)\(/ || /^iwx_add_task\(/ { selected=1; print "void ItlIwx::" }
    /^iwx_sae_tx_task_dispatch\(/ && ENVIRON["IWX_TERMINAL_NEGATIVE_REF"] == "" { selected=1; print "void ItlIwx::" }
    selected { print } selected && /^}/ { selected=0 }' \
    "$root/itlwm/hal_iwx/ItlIwx.cpp" > "$terminal_test_dir/worker.inc"
awk '
    /^iwx_sae_engine_callback_(enter|open)\(/ { selected=1; print "static bool" }
    /^iwx_sae_engine_callback_(leave|close|drain)\(/ || /^iwx_sae_engine_wake_join_retirement\(/ || /^iwx_sae_tx_finish_join_retirement\(/ { selected=1; print "static void" }
    selected { print } selected && /^}/ { selected=0 }' \
    "$root/itlwm/hal_iwx/IwxSaeEngine.inc" >> "$terminal_test_dir/worker.inc"
if [ -n "${IWX_TERMINAL_NEGATIVE_REF:-}" ]; then
    git -C "$root" show "$IWX_TERMINAL_NEGATIVE_REF:itlwm/hal_iwx/ItlIwx.cpp" |
        awk '/^iwx_sae_tx_task_dispatch\(/ { selected=1; print "void ItlIwx::" }
            selected { print } selected && /^}/ { selected=0 }' >> "$terminal_test_dir/worker.inc"
fi
awk '
    /^queueSaeTransportMailbox\(/ { selected=1; print "static void" }
    /^handleSaeAuthTransportEvent\(/ { selected=1; print "void AirportItlwm::" }
    selected { print } selected && /^}/ { selected=0 }' \
    "$root/AirportItlwm/AirportItlwmV2.cpp" > "$terminal_test_dir/controller.inc"
"${CXX:-clang++}" -std=c++17 -Wall -Wextra -Werror -Wno-unused-function \
    -g -pthread -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I "$root/include" -I "$terminal_test_dir" \
    "$root/tests/iwx_sae_terminal_lifetime_test.cpp" -o "$terminal_test_dir/test"
"$terminal_test_dir/test"
