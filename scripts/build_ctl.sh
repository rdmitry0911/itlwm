#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
OUT_DIR="$PROJECT_DIR/Build/Debug/Tahoe"
OUT="$OUT_DIR/AirportItlwmCtl"

mkdir -p "$OUT_DIR"
xcrun clang -Wall -Wextra -Werror \
    -I"$PROJECT_DIR/include" \
    -framework IOKit -framework CoreFoundation \
    "$PROJECT_DIR/AirportItlwmCtl/main.c" \
    -o "$OUT"

echo "Built: $OUT"
