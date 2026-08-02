#!/bin/sh
set -eu

PROJECT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUTPUT_DIR=${1:-"$PROJECT_DIR/build"}
SOURCE="$PROJECT_DIR/AirportItlwmLabCoreWLANAP/airport_itlwm_lab_corewlan_ap.m"
OUTPUT="$OUTPUT_DIR/airport_itlwm_lab_corewlan_ap"

mkdir -p "$OUTPUT_DIR"
xcrun clang -fobjc-arc -framework Foundation -framework CoreWLAN \
    -framework SystemConfiguration \
    "$SOURCE" -o "$OUTPUT"
printf '%s\n' "$OUTPUT"
