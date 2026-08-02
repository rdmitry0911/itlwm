#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

python3 - "$PROJECT_DIR" <<'PY'
from pathlib import Path
import sys

root = Path(sys.argv[1])
probe = (
    root / "AirportItlwmLabCoreWLANAP/airport_itlwm_lab_corewlan_ap.m"
).read_text()
build = (root / "scripts/build_tahoe_lab_corewlan_ap.sh").read_text()

for needle in (
    'CFSTR("com.apple.nat.plist")',
    'CFSTR("NAT")',
    'nat[@"Enabled"] = @((int)enabled);',
    'nat[@"PrimaryService"] = primaryService;',
    'nat[@"SharingDevices"] = @[ sharingInterface ];',
    'SCPreferencesCommitChanges(preferences)',
    'SCPreferencesApplyChanges(preferences)',
):
    assert needle in probe, f"missing reference Internet Sharing preference: {needle}"

for needle in (
    'CFSTR("com.apple.airport.preferences.plist")',
    'CFSTR("InternetSharing")',
    '@"SSID": ssid',
    '@"SSIDString": ssidString',
    '@"SecurityType": securitySchema',
    '@"Channel": @(channel)',
    'dlsym(\n            RTLD_DEFAULT, "schemaStringForSecurityType")',
    'dlsym(\n                RTLD_DEFAULT, "CWSystemKeychainSetHostAPModePassword")',
):
    assert needle in probe, f"missing reference CoreWLAN AP schema: {needle}"

assert 'CFSTR("APModeConfiguration")' not in probe, (
    "Tahoe 25C56 reads the top-level InternetSharing key, not the guessed "
    "APModeConfiguration key"
)

for needle in (
    'strcmp(argv[2], "--configure-default") == 0',
    'strcmp(argv[2], "--enable-internet-sharing") == 0',
    'strcmp(argv[2], "--disable-internet-sharing") == 0',
    'sel_registerName("startHostAPMode:")',
    'InternetSharingPreference checks CFNumberGetTypeID(), not CFBoolean.',
):
    assert needle in probe, f"missing standard Internet Sharing lifecycle: {needle}"

assert "-framework SystemConfiguration" in build
assert "actualPassword UTF8String" not in probe
assert "[password UTF8String]" not in probe

print("PASS: Tahoe standard Internet Sharing open/WPA2/WPA3 producer contract")
PY
