#!/usr/bin/env bash
# Static safety contract for the identity-free LabAP passive observer.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SCRIPT="$ROOT/scripts/tahoe_labap_passive_observe.sh"

fail() {
    printf 'FAIL: %s\n' "$*" >&2
    exit 1
}

require() {
    grep -Fq -- "$1" "$SCRIPT" || fail "missing: $1"
}

forbid() {
    if grep -Fq -- "$1" <<<"$CODE"; then
        fail "forbidden side-effect surface: $1"
    fi
}

test -f "$SCRIPT" || fail "passive observer missing"
bash -n "$SCRIPT" || fail "passive observer is not valid bash"
CODE="$(awk '!/^[[:space:]]*#/' "$SCRIPT")"

for token in \
    'SAMPLES=3' \
    '--samples 3..5' \
    'STA_IF="sta0"' \
    'TEST_SSID="LabAP"' \
    'LABAP_FREQ_24_CH9=2452' \
    'LABAP_FREQ_24_CH13=2472' \
    'LABAP_FREQ_5_CH149=5745' \
    'LABAP_FREQ_5_CH153=5765' \
    'LABAP_FREQ_5_CH177=5885' \
    '-v freq24_ch9="$LABAP_FREQ_24_CH9"' \
    '-v freq24_ch13="$LABAP_FREQ_24_CH13"' \
    '-v freq5_ch149="$LABAP_FREQ_5_CH149"' \
    '-v freq5_ch153="$LABAP_FREQ_5_CH153"' \
    '-v freq5_ch177="$LABAP_FREQ_5_CH177"' \
    '"$LABAP_FREQ_24_CH9"|"$LABAP_FREQ_24_CH13"' \
    '"$LABAP_FREQ_5_CH149"|"$LABAP_FREQ_5_CH153"|"$LABAP_FREQ_5_CH177"' \
    'EXPECTED_IW_VERSION="iw version 6.7"' \
    'scan flush passive' \
    'scan_text=""' \
    '2>/dev/null' \
    'LABAP_PASSIVE_OBSERVE sample=' \
    'external_bss_count=' \
    'external_band_count=' \
    'cross_band_pair_count=' \
    'intersects_previous=' \
    'stable_pair_across_all=' \
    'pair_intersection' \
    'tahoe_labap_topology_parser.awk'; do
    require "$token"
done

# The runner is observation-only: no AP/lifecycle/profile/network mutation,
# no persistent output, and no identity-bearing user-facing print field.
for token in \
    'hostapd' \
    'nmcli' \
    'iw reg set' \
    'ip addr' \
    'ip route' \
    'sysctl -w' \
    'wpa_supplicant' \
    'networksetup' \
    '--activate' \
    '--withdraw' \
    '--rollback' \
    'passphrase' \
    'credential'; do
    forbid "$token"
done

if grep -E '(^|[[:space:]])([0-9]+)?>([^&/]|$)' <<<"$CODE" >/dev/null; then
    fail "observer contains a persistent-output redirection"
fi
if grep -E 'printf.*(bssid|BSSID|ssid|SSID|OBS_PAIRS)' <<<"$CODE" >/dev/null; then
    fail "observer can print an identity-bearing field"
fi

printf 'PASS: Tahoe LabAP passive observer safety contract\n'
