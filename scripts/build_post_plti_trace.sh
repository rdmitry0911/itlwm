#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
OUTPUT_DIR="$PROJECT_DIR/Build/Debug/Tahoe"
OUTPUT="$OUTPUT_DIR/airport_itlwm_post_plti_trace"

mkdir -p "$OUTPUT_DIR"
clang -std=c11 -Wall -Wextra -Werror \
    -I"$PROJECT_DIR/include" \
    "$PROJECT_DIR/AirportItlwmPostPltiTrace/airport_itlwm_post_plti_trace.c" \
    -framework IOKit \
    -framework CoreFoundation \
    -o "$OUTPUT"

echo "Built $OUTPUT"
