#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
SOURCE="$PROJECT_DIR/AirportItlwmLabVisibility/airport_itlwm_lab_visibility.m"
BUILD="$PROJECT_DIR/scripts/build_tahoe_lab_visibility.sh"

fail() {
    echo "FAIL: $*" >&2
    exit 1
}

[ -f "$SOURCE" ] || fail "missing source"
[ -x "$BUILD" ] || fail "missing executable build script"

rg -F 'AIRPORT_ITLWM_LAB_TARGET_SSID_SHA256' "$SOURCE" >/dev/null ||
    fail "target must use a fixed-width SHA-256 digest"
rg -F 'CC_SHA256(data.bytes' "$SOURCE" >/dev/null ||
    fail "candidate names must be compared in process through a digest"
rg -F '[interface scanForNetworksWithName:nil error:&scan_error]' "$SOURCE" >/dev/null ||
    fail "must use exactly the public undirected CoreWLAN scan"
rg -F '[network ssid]' "$SOURCE" >/dev/null ||
    fail "must compare the target in process"
rg -F '[network bssid]' "$SOURCE" >/dev/null ||
    fail "must count distinct BSSes in process"
rg -F 'multi_ap_visible = distinct_bss >= 2' "$SOURCE" >/dev/null ||
    fail "multi-AP visibility must fail closed"
rg -F 'multi_band_visible = represented_bands >= 2' "$SOURCE" >/dev/null ||
    fail "multi-band visibility must fail closed"

if rg -n 'NSLog|localizedDescription|description\]|%@|printf\([^;]*(network_name|network_bss|target_digest|endpoint_name)' "$SOURCE" >/dev/null; then
    fail "source can render an identity or diagnostic string"
fi
if rg -n 'associate|setPower|setInterface|IORegistryEntrySet|networksetup|system\(' "$SOURCE" >/dev/null; then
    fail "visibility probe must not alter association or interface state"
fi
rg -F 'outcome, endpoint_binding, target_present, target_records,' "$SOURCE" >/dev/null ||
    fail "output arguments must remain categorical or aggregate only"

printf 'PASS: Tahoe laboratory visibility client contract\n'
