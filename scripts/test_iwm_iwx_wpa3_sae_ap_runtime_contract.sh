#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
python3 - "$ROOT" <<'PY'
from pathlib import Path
import sys

root = Path(sys.argv[1])
runtime = (root / "include/HAL/ItlApFirmwareRuntime.hpp").read_text()
framing = (root / "include/HAL/ItlApOpenRuntime.hpp").read_text()
iwm = (root / "itlwm/hal_iwm/mac80211.cpp").read_text()
iwx = (root / "itlwm/hal_iwx/ItlIwx.cpp").read_text()

def require(text, needle, label):
    if needle not in text:
        raise SystemExit(f"missing {label}: {needle}")

for needle, label in (
    ("struct ieee80211_sae_ap *sae", "owned SAE object"),
    ("uint8_t pmk[IEEE80211_PMK_LEN]", "driver-owned PMK"),
    ("struct ieee80211_ptk ptk", "shared PTK"),
    ("ieee80211_sae_ap_destroy(&runtime->sae)", "SAE lifetime teardown"),
    ("explicit_bzero(runtime->pmk", "PMK scrub"),
):
    require(runtime, needle, label)

for needle, label in (
    ("itl_ap_wpa3_rsn_ie_supported", "SAE/PMF RSN validator"),
    ("config->authUpper == 0x1000", "Tahoe WPA3 carrier"),
    ("(LE_READ_2(cursor) & 0x00c0) != 0x00c0", "MFPC+MFPR gate"),
    ("bipCmac128Suite", "BIP-CMAC-128 gate"),
    ("ieee80211_sae_ap_begin_hnp", "SAE Commit responder"),
    ("ieee80211_sae_ap_confirm", "SAE Confirm responder"),
    ("ieee80211_sae_ap_is_accepted", "accepted-SAE association gate"),
    ("EAPOL_KEY_DESC_AKM_DEFINED", "SAE EAPOL descriptor"),
    ("ieee80211_derive_ptk(IEEE80211_AKM_SAE", "SAE PTK derivation"),
    ("ieee80211_eapol_key_check_mic", "M2/M4 MIC validation"),
    ("ieee80211_eapol_key_encrypt", "M3 key wrapping"),
    ("IEEE80211_KDE_GTK", "M3 GTK KDE"),
    ("*cursor++ = 9", "M3 IGTK KDE"),
    ("kItlApLocalEapolInstallPairwise", "M4 install edge"),
    ("runtime->clientAuthorized", "controlled-port gate"),
    ("!hardwareDecrypted", "PMF protected disconnect gate"),
):
    require(framing, needle, label)

for backend, name, prefix in ((iwm, "IWM", "IWM"), (iwx, "IWX", "IWX")):
    for needle, label in (
        ("itl_ap_local_sae_build_m1", "M1 start"),
        ("itl_ap_local_sae_handle_eapol", "local EAPOL owner"),
        ("kItlHalApKeyGroup", "GTK installation"),
        ("kItlHalApKeyPairwise", "PTK installation"),
        ("kItlHalApStationAuthorize", "port authorization"),
        (f"{prefix}_STA_KEY_MFP", "firmware MFP key flag"),
        ("itl_ap_firmware_sae_reset", "disconnect SAE scrub"),
    ):
        require(backend, needle, f"{name} {label}")

if "itl_ap_wpa3_config_supported(config);" not in framing:
    raise SystemExit("WPA3 was not admitted through the shared AP profile gate")

print("PASS: paired IWM/IWX driver-resident WPA3 SAE/4-way/PMF contract")
PY
