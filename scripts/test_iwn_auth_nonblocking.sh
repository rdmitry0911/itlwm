#!/usr/bin/env bash
# Required gate, intentionally red until the real AUTH admission stops
# busy-waiting. Kept outside the passing aggregate while implementation is open.
set -euo pipefail
ulimit -c 0
PROJECT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
IWN_AUTH_TEST_DIR="$(mktemp -d)"
cleanup() {
    rm -f "$IWN_AUTH_TEST_DIR/auth.inc" "$IWN_AUTH_TEST_DIR/test"
    if [[ -d "$IWN_AUTH_TEST_DIR/test.dSYM" ]]; then
        rm -r "$IWN_AUTH_TEST_DIR/test.dSYM"
    fi
    rmdir "$IWN_AUTH_TEST_DIR"
}
trap cleanup EXIT
awk '
    /^iwn_auth\(struct iwn_softc \*sc, int arg\)/ {
        selected=1; print "int ItlIwn::"
    }
    selected { print }
    selected && /^}$/ { found=1; exit }
    END { if (!found) exit 1 }
' "${IWN_AUTH_SOURCE:-$PROJECT_DIR/itlwm/hal_iwn/ItlIwn.cpp}" \
    > "$IWN_AUTH_TEST_DIR/auth.inc"
"${CXX:-clang++}" -std=c++17 -Wall -Wextra -Werror -g \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I "$IWN_AUTH_TEST_DIR" "$PROJECT_DIR/tests/iwn_auth_nonblocking_test.cpp" \
    -o "$IWN_AUTH_TEST_DIR/test"
"$IWN_AUTH_TEST_DIR/test" "${1:-roam}"
