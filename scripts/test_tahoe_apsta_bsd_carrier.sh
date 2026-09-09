#!/usr/bin/env bash
set -euo pipefail
PROJECT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
TEST_DIR="$(mktemp -d)"
trap 'rm -f "$TEST_DIR/production.inc" "$TEST_DIR/test"; rmdir "$TEST_DIR"' EXIT
if [ -n "${APSTA_CARRIER_BASELINE:-}" ]; then
    git -C "$PROJECT_DIR" show "$APSTA_CARRIER_BASELINE:AirportItlwm/AirportItlwmSkywalkInterface.cpp"
else
    sed -n '/^IOReturn AirportItlwmAPSTASkywalkInterface::enable(UInt options)/,/^bool AirportItlwmAPSTASkywalkInterface::isCommandProhibited/p' \
        "$PROJECT_DIR/AirportItlwm/AirportItlwmSkywalkInterface.cpp"
fi | awk '
    /^IOReturn AirportItlwmAPSTASkywalkInterface::enable\(UInt options\)/ { selected=1 }
    /^bool AirportItlwmAPSTASkywalkInterface::isCommandProhibited/ { selected=0 }
    selected { print }
' > "$TEST_DIR/production.inc"
"${CXX:-clang++}" -std=c++17 -Wall -Wextra -Werror -g \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I "$TEST_DIR" "$PROJECT_DIR/tests/tahoe_apsta_bsd_carrier_test.cpp" \
    -o "$TEST_DIR/test"
"$TEST_DIR/test"
