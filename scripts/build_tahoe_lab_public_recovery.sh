#!/usr/bin/env bash
# Build only the bounded public-CoreWLAN recovery client for a Tahoe guest.
set -euo pipefail

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
SOURCE="$PROJECT_DIR/AirportItlwmLabPublicRecovery/airport_itlwm_lab_public_recovery.m"
OUTPUT_DIR="$PROJECT_DIR/Build/Debug/Tahoe"
OUTPUT="$OUTPUT_DIR/airport_itlwm_lab_public_recovery"

[ -f "$SOURCE" ] || {
    printf '%s\n' 'missing public recovery laboratory source' >&2
    exit 1
}

SDKROOT="$(xcrun --sdk macosx --show-sdk-path)"
mkdir -p "$OUTPUT_DIR"
xcrun --sdk macosx clang -fobjc-arc -fmodules -isysroot "$SDKROOT" \
    -mmacosx-version-min=15.0 -Wall -Wextra -Werror \
    -framework CoreWLAN -framework Foundation -framework IOKit \
    "$SOURCE" -o "$OUTPUT"

printf '%s\n' "$OUTPUT"
