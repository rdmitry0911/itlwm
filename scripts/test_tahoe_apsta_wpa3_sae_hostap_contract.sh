#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

python3 - "$PROJECT_DIR" <<'PY'
from pathlib import Path
import sys

root = Path(sys.argv[1])
owner = (root / "AirportItlwm/AirportItlwmAPSTAOwner.cpp").read_text()
iwn = (root / "itlwm/hal_iwn/ItlIwn.cpp").read_text()
iwn_hpp = (root / "itlwm/hal_iwn/ItlIwn.hpp").read_text()
sae = (
    root / "itl80211/openbsd/net80211/ieee80211_sae_engine.c"
).read_text()
sae_h = (
    root / "itl80211/openbsd/net80211/ieee80211_sae_engine.h"
).read_text()


def body(source: str, signature: str, next_signature: str) -> str:
    start = source.index(signature)
    end = source.index(next_signature, start)
    return source[start:end]


for needle in (
    "kAirportItlwmAPSTAAuthUpperWPA3SAE = 0x1000",
    "APPLE80211_AUTHTYPE_WPA3_SAE",
    "IEEE80211_ELEMID_RSN, 26",
    "0x00, 0x0f, 0xac, 0x08",
    "0xc0, 0x00",
    "0x00, 0x0f, 0xac, 0x06",
    "apsta_build_wpa3_sae_rsn_ie(",
):
    assert needle in owner, f"missing WPA3 HostAP owner carrier: {needle}"

for needle in (
    "struct ieee80211_sae_ap;",
    "ieee80211_sae_ap_begin_hnp(",
    "ieee80211_sae_ap_confirm(",
    "uint8_t *pmkid, size_t pmkid_capacity",
    "ieee80211_sae_ap_destroy(",
):
    assert needle in sae_h, f"missing opaque AP SAE API: {needle}"

for needle in (
    "sae_parse_commit(",
    "sae_prepare_commit(",
    "sae_process_commit(",
    "sae_check_confirm(",
    "sae_write_confirm(",
    "os_memcpy(pmk, ap->sae.pmk, SAE_PMK_LEN);",
    "os_memcpy(pmkid, ap->sae.pmkid, sizeof(ap->sae.pmkid));",
    "sae_clear_data(&current->sae);",
    "ieee80211_sae_secure_zero(current, sizeof(*current));",
):
    assert needle in sae, f"missing AP SAE responder contract: {needle}"

for needle in (
    "EAPOL_KEY_DESC_AKM_DEFINED",
    "IWN_AP_IGTK_KDE_TYPE = 9",
    "IWN_AP_IGTK_KEY_ID = 4",
    "IEEE80211_KDE_PMKID",
    "iwn_handle_ap_sae_auth(wh, len)",
    "ieee80211_sae_ap_begin_hnp(",
    "ieee80211_sae_ap_confirm(",
    "static const uint8_t saeSuite[]",
    "(capabilities & 0x00c0) == 0x00c0",
    "static const uint8_t bipCmac128Suite[]",
    "iwn_install_ap_ccmp_key(true, 0, apPtk.tk)",
):
    assert needle in iwn, f"missing IWN WPA3/SAE/PMF contract: {needle}"

sae_rx = body(
    iwn,
    "if (iwn_handle_ap_sae_auth(wh, len))",
    "if (iwn_handle_ap_open_auth(wh, len))",
)
assert "mbuf_freem(m);" in sae_rx, \
    "SAE management frames must be consumed before Open-System handling"

open_auth = body(
    iwn,
    "bool ItlIwn::iwn_handle_ap_open_auth(",
    "bool ItlIwn::iwn_handle_ap_assoc_req(",
)
for needle in (
    "if (iwn_ap_uses_sae())",
    "iwn_reset_ap_sae();",
    "apClientOpenAuthenticated = iwn_ap_uses_sae();",
    "IEEE80211_AUTH_OPEN_RESPONSE",
    "IEEE80211_STATUS_SUCCESS",
):
    assert needle in open_auth, f"missing SAE PMKSA Open auth path: {needle}"
assert "pure-SAE BSS never admits" not in open_auth

assoc = body(
    iwn,
    "bool ItlIwn::iwn_handle_ap_assoc_req(",
    "void ItlIwn::iwn_publish_ap_station_event(",
)
for needle in (
    "saePmkidList = optional;",
    "iwn_ap_sae_pmksa_matches(",
    "saeAuthenticated || saePmksaAuthenticated",
    "IWN_AP_STATUS_INVALID_PMKID",
    "iwn_send_ap_mgmt_frame(rejection, sizeof(rejection))",
    "memcpy(apPmk, apSaePmksaPmk, sizeof(apPmk));",
):
    assert needle in assoc, f"missing SAE PMKSA association contract: {needle}"

for needle in (
    "uint8_t apSaePmksaPmk[IEEE80211_PMK_LEN];",
    "uint8_t apSaePmksaPmkid[IEEE80211_PMKID_LEN];",
    "uint8_t apSaePmksaSta[IEEE80211_ADDR_LEN];",
    "uint8_t apSaePmksaBssid[IEEE80211_ADDR_LEN];",
    "bool apSaePmksaValid;",
    "bool apClientOpenAuthenticated;",
):
    assert needle in iwn_hpp, f"missing bounded SAE PMKSA state: {needle}"

reset = body(
    iwn,
    "void ItlIwn::iwn_reset_ap_runtime_state()",
    "void ItlIwn::iwn_purge_ap_ps_queue()",
)
assert "iwn_reset_ap_sae();" in reset
assert "iwn_clear_ap_sae_pmksa();" not in reset, \
    "radio reset must preserve the bounded SAE PMKSA"

stop = body(
    iwn,
    "IOReturn ItlIwn::stopAPMode()",
    "int ItlIwn::\niwn_match(",
)
assert "iwn_clear_ap_sae_pmksa();" in stop, \
    "explicit HostAP stop must scrub the SAE PMKSA"

print("PASS: Tahoe HostAP WPA3 SAE/PMF, PMKSA fallback, and sleep cache contract")
PY
