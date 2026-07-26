#!/usr/bin/env bash
# Build the Linux-only bounded credential broker used by the lab sidecar.
set -euo pipefail

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
SOURCE="$PROJECT_DIR/AirportItlwmLabPublicRecovery/airport_itlwm_lab_credential_broker.c"
OUTPUT_DIR="$PROJECT_DIR/Build/Debug/Linux"
OUTPUT="$OUTPUT_DIR/airport_itlwm_lab_credential_broker"

[ -f "$SOURCE" ] || {
    printf '%s\n' 'missing laboratory credential broker source' >&2
    exit 1
}

mkdir -p "$OUTPUT_DIR"
cc -std=c11 -O2 -D_FORTIFY_SOURCE=2 -fstack-protector-strong \
    -Wall -Wextra -Werror -Wpedantic \
    "$SOURCE" -o "$OUTPUT"

printf '%s\n' "$OUTPUT"
