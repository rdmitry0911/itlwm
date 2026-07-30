#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

python3 - "$PROJECT_DIR" <<'PY'
from pathlib import Path
import sys

root = Path(sys.argv[1])
layout = (root / "AirportItlwm/AirportItlwmAPSTAInterface.hpp").read_text()
owner = (root / "AirportItlwm/AirportItlwmAPSTAOwner.cpp").read_text()
owner_hpp = (root / "AirportItlwm/AirportItlwmAPSTAOwner.hpp").read_text()
controller = (root / "AirportItlwm/AirportItlwmV2.cpp").read_text()
iwn = (root / "itlwm/hal_iwn/ItlIwn.cpp").read_text()
hal = (root / "include/HAL/ItlHalService.hpp").read_text()
probe = (
    root / "AirportItlwmLabCoreWLANAP/airport_itlwm_lab_corewlan_ap.m"
).read_text()

required_layout = {
    "uint32_t authUpper0c;": "HostAP authentication carrier",
    "uint32_t channelNumber14;": "HostAP channel carrier",
    "uint32_t credentialLength44;": "HostAP credential length",
    "uint8_t  credential50[0x40];": "HostAP credential bytes",
}
for needle, meaning in required_layout.items():
    assert needle in layout, f"missing {meaning}"

for offset in ("0x0c", "0x14", "0x44", "0x50"):
    assert offset in layout, f"missing recovered HostAP offset {offset}"

required_owner = (
    "kAirportItlwmAPSTAAuthUpperWPA2PSK = 0x8",
    "IEEE80211_ELEMID_RSN, 20",
    "IEEE80211_CAPINFO_PRIVACY",
    "channel.channel.channel = in->channelNumber14;",
    "cfg.credentialLength = apCredentialLength;",
    "cfg.rsnIELength = rsnIELength;",
)
for needle in required_owner:
    assert needle in owner, f"missing owner WPA2 contract: {needle}"

required_hal = (
    "uint32_t authUpper;",
    "const uint8_t *credential;",
    "const uint8_t *rsnIE;",
)
for needle in required_hal:
    assert needle in hal, f"missing HAL WPA2 carrier: {needle}"

required_iwn = (
    "IEEE80211_ELEMID_RSN",
    "static const uint8_t ccmpSuite[]",
    "static const uint8_t pskSuite[]",
    "apFirmwareConfig.rsnIELength",
    "apFirmwareConfig.credential",
    "pbkdf2_sha1(",
    "ieee80211_derive_ptk(",
    "ieee80211_eapol_key_check_mic(",
    "ieee80211_eapol_key_encrypt(",
    "iwn_install_ap_ccmp_key(false, apGtkKid, apGtk)",
    "iwn_install_ap_ccmp_key(true, 0, apPtk.tk)",
    "IWN_AP_RSN_WAIT_M2",
    "IWN_AP_RSN_WAIT_M4",
    "IWN_AP_RSN_AUTHORIZED",
    "BE_READ_8(key->replaycnt) != apReplayCounter",
    "ethernetHeader.ether_type == htons(ETHERTYPE_PAE)",
    "frame.i_fc[1] |= IEEE80211_FC1_PROTECTED",
    "(rxFlags & IWN_RX_CIPHER_MASK) != IWN_RX_CIPHER_CCMP",
    "packetNumber <= apPairwiseRxPn[tid]",
)
for needle in required_iwn:
    assert needle in iwn, f"missing IWN RSN admission contract: {needle}"

assert "startHostAPModeWithSSID:securityType:channel:password:error:" in probe
assert "initWithBytes:argv[3] length:strlen(argv[3])" in probe

assert "memcpy(passphrase, apFirmwareCredential," in iwn
assert "explicit_bzero(passphrase, sizeof(passphrase));" in iwn
assert "AP HostAP key peer=" not in owner

required_power_lifecycle = (
    "void AirportItlwmAPSTAOwner::prepareForRadioReset()",
    "state.softapAssociatedStaCount00 == 0",
    "kAirportItlwmAPSTAHostApPowerOffConcurrencyFallbackState",
    "owner->setAPSTADatapathEnabled(false);",
    "radioResetResumePending = true;",
    "IOReturn AirportItlwmAPSTAOwner::resumeAfterRadioReset()",
    "kAirportItlwmAPSTAHostApPowerOnRestoreState",
)
for needle in required_power_lifecycle:
    assert needle in owner, f"missing APSTA sleep/wake lifecycle: {needle}"

assert "bool radioResetResumePending;" in owner_hpp
assert "fAPSTAOwner->prepareForRadioReset();" in controller
assert "AirportItlwm::resumeAPSTAAfterRadioResetGated(" in controller
assert "fAPSTAOwner->resumeAfterRadioReset();" in controller
assert "gate->runAction(resumeAPSTAAfterRadioResetGated);" in controller
scan_done = controller[controller.index("case IEEE80211_EVT_SCAN_DONE:"):
                       controller.index("case IEEE80211_EVT_WCL_REASSOC_DONE:")]
assert "resumeAfterRadioReset()" not in scan_done, \
       "lower SCAN_DONE callback must not synchronously submit DVM commands"

print("PASS: Tahoe HostAP WPA2 carrier/authenticator/CCMP contract")
PY
