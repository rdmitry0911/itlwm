#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
SOURCE="$PROJECT_DIR/AirportItlwmLabVisibility/airport_itlwm_lab_visibility.m"
OUTPUT_DIR="$PROJECT_DIR/Build/Debug/Tahoe"
OUTPUT="$OUTPUT_DIR/airport_itlwm_lab_visibility"

if [ ! -f "$SOURCE" ]; then
    echo "missing laboratory visibility source" >&2
    exit 1
fi

SDKROOT="$(xcrun --sdk macosx --show-sdk-path)"
mkdir -p "$OUTPUT_DIR"
xcrun --sdk macosx clang -fobjc-arc -fmodules -isysroot "$SDKROOT" \
    -mmacosx-version-min=15.0 -Wall -Wextra -Werror \
    -framework CoreWLAN -framework Foundation -framework IOKit \
    "$SOURCE" -o "$OUTPUT"

echo "$OUTPUT"
