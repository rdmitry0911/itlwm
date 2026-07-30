#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
SOURCE="$PROJECT_DIR/AirportItlwmLabAPProbe/airport_itlwm_lab_ap_probe.c"
OUTPUT_DIR="$PROJECT_DIR/Build/Debug/Tahoe-OptOut"
OUTPUT="$OUTPUT_DIR/airport_itlwm_lab_ap_probe"

mkdir -p "$OUTPUT_DIR"
xcrun --sdk macosx clang -isysroot "$(xcrun --sdk macosx --show-sdk-path)" \
    -mmacosx-version-min=15.0 -Wall -Wextra -Werror \
    "$SOURCE" -o "$OUTPUT"
printf '%s\n' "$OUTPUT"
