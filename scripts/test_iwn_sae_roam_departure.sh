#!/usr/bin/env bash
set -euo pipefail
ulimit -c 0
DEPARTURE_ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
DEPARTURE_TEST_DIR=$(mktemp -d)
cleanup() {
    rm -f "$DEPARTURE_TEST_DIR/departure.inc" "$DEPARTURE_TEST_DIR/test"
    if [[ -d "$DEPARTURE_TEST_DIR/test.dSYM" ]]; then rm -r "$DEPARTURE_TEST_DIR/test.dSYM"; fi
    rmdir "$DEPARTURE_TEST_DIR"
}
trap cleanup EXIT
awk '
    /^iwn_sae_roam_source_current_locked\(/ { selected=1; print "static bool" }
    /^iwn_sae_roam_departure_(stop|fail)\(/ { selected=1; print "static void" }
    /^iwn_sae_roam_departure_start\(/ { selected=1; print "int ItlIwn::" }
    /^iwn_sae_roam_departure_commit\(/ { selected=1; print "bool ItlIwn::" }
    /^iwn_sae_roam_departure_terminal\(/ { selected=1; print "void ItlIwn::" }
    selected { print }
    selected && /^}$/ { selected=0; count++ }
    END { if (count!=6) exit 1 }
' "$DEPARTURE_ROOT/itlwm/hal_iwn/ItlIwn.cpp" > "$DEPARTURE_TEST_DIR/departure.inc"
"${CXX:-clang++}" -std=c++17 -g -Wall -Wextra -Werror -Wno-unused-parameter \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I "$DEPARTURE_ROOT" -I "$DEPARTURE_TEST_DIR" \
    "$DEPARTURE_ROOT/tests/iwn_sae_roam_departure_test.cpp" -o "$DEPARTURE_TEST_DIR/test"
"$DEPARTURE_TEST_DIR/test"
