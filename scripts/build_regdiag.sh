#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
OUTPUT_DIR="$PROJECT_DIR/Build/Debug/Tahoe"
OUTPUT="$OUTPUT_DIR/airport_itlwm_regdiag"

mkdir -p "$OUTPUT_DIR"
clang -std=c11 -Wall -Wextra \
    -I"$PROJECT_DIR/include" \
    "$PROJECT_DIR/AirportItlwmRegDiag/airport_itlwm_regdiag.c" \
    -framework IOKit \
    -framework CoreFoundation \
    -o "$OUTPUT"

echo "Built $OUTPUT"
