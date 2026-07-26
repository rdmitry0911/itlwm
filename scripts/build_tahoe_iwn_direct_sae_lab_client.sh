#!/usr/bin/env bash
# Build the standalone, lab-only direct-IWN-SAE UserClient helper.
# It has no production build mode: the separate UserClient type exists only
# in the IWN software-PMF laboratory artifact.
set -euo pipefail

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)"
SOURCE="$ROOT/AirportItlwmIwnDirectSaeLabClient/airport_itlwm_iwn_direct_sae_lab_client.c"
OUTPUT_DIR="$ROOT/Build/Debug/Tahoe-IwnSoftwarePmfLab"
OUTPUT="$OUTPUT_DIR/airport_itlwm_iwn_direct_sae_lab_client"

if [ "$#" -ne 1 ] || [ "$1" != "--iwn-software-pmf-lab" ]; then
    printf '%s\n' "usage: $0 --iwn-software-pmf-lab" >&2
    exit 2
fi
[ -f "$SOURCE" ] || {
    printf '%s\n' 'ERROR: direct-SAE lab client source is unavailable' >&2
    exit 1
}

SDKROOT="$(xcrun --sdk macosx --show-sdk-path)"
mkdir -p "$OUTPUT_DIR"
xcrun --sdk macosx clang -std=c11 -Wall -Wextra -Werror \
    -isysroot "$SDKROOT" -mmacosx-version-min=15.0 \
    -I"$ROOT/include" \
    "$SOURCE" \
    -framework IOKit -framework CoreFoundation \
    -o "$OUTPUT"
chmod 700 "$OUTPUT"
printf '%s\n' "Built $OUTPUT"
