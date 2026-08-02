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

msg3 = body(
    iwn,
    "int ItlIwn::iwn_send_ap_4way_msg3()",
    "void ItlIwn::iwn_begin_ap_4way()",
)
for needle in (
    "#ifdef IEEE80211_STA_ONLY",
    "return ENOTSUP;",
    "ieee80211_eapol_key_encrypt(&com.sc_ic, key, apPtk.kek);",
    "#endif",
):
    assert needle in msg3, \
        f"missing STA-only/AP-only EAPOL encryption boundary: {needle}"
assert msg3.index("return ENOTSUP;") < msg3.index(
    "ieee80211_eapol_key_encrypt(&com.sc_ic, key, apPtk.kek);"
), "STA-only build must fail closed before the AP-only EAPOL dependency"

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
    "iwn_prepare_ap_client_reauthentication(\n        iwn_ap_uses_sae())",
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
    "uint8_t saePmksaPmk[IEEE80211_PMK_LEN];",
    "uint8_t saePmksaPmkid[IEEE80211_PMKID_LEN];",
    "uint8_t saePmksaSta[IEEE80211_ADDR_LEN];",
    "uint8_t saePmksaBssid[IEEE80211_ADDR_LEN];",
    "bool saePmksaValid;",
    "bool openAuthenticated;",
):
    assert needle in iwn_hpp, f"missing bounded SAE PMKSA state: {needle}"

reset = body(
    iwn,
    "void ItlIwn::iwn_reset_ap_runtime_state()",
    "void ItlIwn::iwn_purge_ap_ps_queue()",
)
assert "iwn_reset_ap_client(&apClients[index], true, true)" in reset
assert "iwn_clear_ap_sae_pmksa();" not in reset, \
    "radio reset must preserve the bounded SAE PMKSA"
for needle in (
    "const bool cached = preserveSaePmksa && client->saePmksaValid;",
    "memcpy(client->saePmksaPmk, cachedPmk,",
    "client->saePmksaValid = true;",
):
    assert needle in iwn, f"missing reset-time SAE PMKSA preservation: {needle}"

allocate = body(
    iwn,
    "struct IwnApClientRuntime *ItlIwn::iwn_allocate_ap_client(",
    "int ItlIwn::iwn_submit_next_ap_client_materialization()",
)
for needle in (
    "!candidate->inUse && candidate->saePmksaValid",
    "candidate->saePmksaSta, station",
    "candidate->saePmksaBssid, apFirmwareConfig.bssid",
    "!apClients[index].saePmksaValid",
    "client->saePmksaValid = true;",
):
    assert needle in allocate, f"missing bounded PMKSA cache admission: {needle}"

start = body(
    iwn,
    "IOReturn ItlIwn::startAPMode(",
    "IOReturn ItlIwn::stopAPMode()",
)
assert "for (size_t index = 0; index < kItlApFirmwareMaxClients; index++)" in start
assert "client->saePmksaBssid, apFirmwareConfig.bssid" in start
assert "iwn_clear_ap_sae_pmksa();" in start, \
    "profile change must scrub every mismatched SAE PMKSA entry"

stop = body(
    iwn,
    "IOReturn ItlIwn::stopAPMode()",
    "int ItlIwn::\niwn_match(",
)
assert "for (size_t index = 0; index < kItlApFirmwareMaxClients; index++)" in stop
assert "iwn_clear_ap_sae_pmksa();" in stop, \
    "explicit HostAP stop must scrub the SAE PMKSA"

print("PASS: Tahoe HostAP WPA3 SAE/PMF, PMKSA fallback, and sleep cache contract")
PY
