#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

python3 - "$PROJECT_DIR" <<'PY'
from pathlib import Path
import sys

root = Path(sys.argv[1])
contract = (root / "AirportItlwm/TahoeOpModeContracts.hpp").read_text()
skywalk = (root / "AirportItlwm/AirportItlwmSkywalkInterface.cpp").read_text()
legacy = (root / "AirportItlwm/AirportSTAIOCTL.cpp").read_text()
probe = (root / "AirportItlwmLabCoreWLANAP/airport_itlwm_lab_corewlan_ap.m").read_text()

assert "kSoftAPMode = 0x08" in contract
assert "publishAPSTAMode" in contract

skywalk_get = skywalk[skywalk.index(
    "AirportItlwmSkywalkInterface::\ngetOP_MODE(struct apple80211_opmode_data *od)"):
    skywalk.index("getPOWER_DEBUG_INFO", skywalk.index(
        "AirportItlwmSkywalkInterface::\ngetOP_MODE(struct apple80211_opmode_data *od)"))]
for needle in (
    "instance->isHostApPrimaryCarrierConfirmed()",
    "instance->getAPSTA_OP_MODE(this, &apstaMode)",
    "publishAPSTAMode(od,",
    "publishAssociatedBssMode(od,",
):
    assert needle in skywalk_get, f"missing Tahoe primary APSTA OP_MODE path: {needle}"
assert skywalk_get.index("publishAPSTAMode(od,") < skywalk_get.index(
    "publishAssociatedBssMode(od,")

legacy_get = legacy[legacy.index(
    "AirportItlwm::\ngetOP_MODE(OSObject *object,"):
    legacy.index("AirportItlwm::\ngetRSSI", legacy.index(
        "AirportItlwm::\ngetOP_MODE(OSObject *object,"))]
for needle in (
    "isHostApPrimaryCarrierConfirmed()",
    "getAPSTA_OP_MODE(object, &apstaMode)",
    "publishAPSTAMode(od, apstaMode.mode04)",
    "publishAssociatedBssMode(od,",
):
    assert needle in legacy_get, f"missing legacy primary APSTA OP_MODE path: {needle}"

for needle in (
    'strcmp(value, "open") == 0',
    '*securityType = 2;',
    'strcmp(value, "wpa2") == 0',
    '*securityType = 0x80;',
    'strcmp(value, "wpa3") == 0',
    '*securityType = 0x1000;',
):
    assert needle in probe, f"missing exact CoreWLAN security mapping: {needle}"

for needle in (
    'strcmp(argv[2], "--stop-sharing") == 0',
    'start-sharing requires root or the private ',
    'stop-sharing requires root or the private ',
    '__stopNetworkRelayBridgeForInterfaceName:relayInterfaceName',
    'if (!relayReplyReceived || relayError != nil)',
    'NetworkRelay failed-start cleanup error=',
):
    assert needle in probe, f"missing bounded NetworkRelay lifecycle: {needle}"
assert probe.count("geteuid() != 0") == 4, \
    "private NetworkRelay and Internet Sharing mutations must reject non-root"
assert "actualPassword UTF8String" not in probe
assert "[password UTF8String]" not in probe

print("PASS: Tahoe primary OP_MODE publishes running APSTA for CoreWLAN stop")
PY
