#!/usr/bin/env bash
# Full disable/activate caller boundary. Stop is a spy, not hardware proof.
set -euo pipefail
ulimit -c 0
root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
quiesce_dir="$(mktemp -d)"
trap 'rm -f "$quiesce_dir/quiesce.inc" "$quiesce_dir/test"; rm -rf "$quiesce_dir/test.dSYM"; rmdir "$quiesce_dir"' EXIT
if [ -n "${IWX_QUIESCE_NEGATIVE_REF:-}" ]; then
    git -C "$root" show "$IWX_QUIESCE_NEGATIVE_REF:itlwm/hal_iwx/ItlIwx.cpp"
else
    sed -n '1,$p' "$root/itlwm/hal_iwx/ItlIwx.cpp"
fi | awk '
    /^IOReturn ItlIwx::disable\(/ { selected=1 }
    /^iwx_activate\(/ { selected=1; print "int ItlIwx::" }
    selected { print } selected && /^}/ { selected=0 }
' > "$quiesce_dir/quiesce.inc"
"${CXX:-clang++}" -std=c++17 -Wall -Wextra -Werror -Wno-unused-function \
    -Wno-unused-parameter -g -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I "$quiesce_dir" "$root/tests/iwx_radio_quiesce_test.cpp" -o "$quiesce_dir/test"
if [ "${1:-all}" = all ]; then
    for quiesce_case in running primary-down-running early-off early-off-primary-down; do
        "$quiesce_dir/test" "$quiesce_case"
    done
else
    "$quiesce_dir/test" "$1"
fi
