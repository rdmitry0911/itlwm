#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

python3 - "$PROJECT_DIR" <<'PY'
from pathlib import Path
import sys

root = Path(sys.argv[1])
iwn = (root / "itlwm/hal_iwn/ItlIwn.cpp").read_text()
iwn_hpp = (root / "itlwm/hal_iwn/ItlIwn.hpp").read_text()


def body(source: str, signature: str, next_signature: str) -> str:
    start = source.index(signature)
    end = source.index(next_signature, start)
    return source[start:end]


for needle in (
    "IWN_AP_CLIENT_MATERIALIZATION_IDLE",
    "IWN_AP_CLIENT_MATERIALIZATION_ADD_NODE",
    "IWN_AP_CLIENT_MATERIALIZATION_WAKE_NODE",
    "IWN_AP_CLIENT_MATERIALIZATION_LINK_QUALITY",
):
    assert needle in iwn, f"missing AP client materialization state: {needle}"

assoc = body(
    iwn,
    "bool ItlIwn::iwn_handle_ap_assoc_req(",
    "void ItlIwn::iwn_publish_ap_station_event(",
)
for needle in (
    "apClientMaterializationStage !=",
    "IWN_AP_CLIENT_MATERIALIZATION_IDLE",
    "iwn_add_ap_client_node(request->i_addr2)",
    "IWN_AP_CLIENT_MATERIALIZATION_ADD_NODE",
    "iwn_update_ap_client_node()",
    "IWN_AP_CLIENT_MATERIALIZATION_WAKE_NODE",
):
    assert needle in assoc, f"missing association admission fence: {needle}"
assert "iwn_send_ap_client_link_quality()" not in assoc, \
    "Association RX must wait for the ADD_STA firmware reply"
assert "iwn_send_ap_mgmt_frame(response" not in assoc, \
    "Association Response must wait for station/LQ materialization"

events = body(
    iwn,
    "void ItlIwn::iwn_note_ap_firmware_event(",
    "IOReturn ItlIwn::startAPMode(",
)
add_reply = events.index("command == IWN_CMD_ADD_NODE")
status = events.index("addNodeStatus != 1", add_reply)
lq_stage = events.index(
    "IWN_AP_CLIENT_MATERIALIZATION_LINK_QUALITY", status
)
lq_queue = events.index("iwn_send_ap_client_link_quality()", lq_stage)
lq_reply = events.index("command == IWN_CMD_LINK_QUALITY", lq_queue)
assoc_queue = events.index("iwn_send_ap_assoc_success()", lq_reply)
assert add_reply < status < lq_stage < lq_queue < lq_reply < assoc_queue, \
    "AP client must materialize as ADD_STA reply -> LQ reply -> assoc response"

success = body(
    iwn,
    "int ItlIwn::iwn_send_ap_assoc_success()",
    "int ItlIwn::iwn_send_ap_sensitivity()",
)
for needle in (
    "iwn_send_ap_mgmt_frame(",
    "apClientAssociated = true;",
    "iwn_publish_ap_station_event(",
):
    assert needle in success, f"missing deferred association completion: {needle}"
assert success.index("iwn_send_ap_mgmt_frame(") < success.index(
    "apClientAssociated = true;"
), "host association state must follow successful response submission"

key_install = body(
    iwn,
    "int ItlIwn::iwn_install_ap_ccmp_key(",
    "int ItlIwn::iwn_send_ap_eapol_key(",
)
for needle in (
    "IEEE80211_ADDR_COPY(node.macaddr, apClientMac);",
    "IEEE80211_ADDR_COPY(node.macaddr, etherbroadcastaddr);",
    "node.htflags = htole32(IWN_PAN_STATION);",
    "node.control = IWN_NODE_UPDATE;",
    "node.flags = IWN_FLAG_SET_KEY;",
):
    assert needle in key_install, \
        f"SET_KEY must retain the full PAN station descriptor: {needle}"

eapol = body(
    iwn,
    "bool ItlIwn::iwn_handle_ap_eapol_key(",
    "int ItlIwn::iwn_send_ap_mgmt_frame(",
)
for needle in (
    "apPairwiseSoftwareKey.k_cipher = IEEE80211_CIPHER_CCMP;",
    "apPairwiseSoftwareKey.k_flags = IEEE80211_KEY_SWCRYPTO;",
    "ieee80211_ccmp_set_key(",
    "iwn_install_ap_ccmp_key(true, 0, apPtk.tk)",
):
    assert needle in eapol, \
        f"M4 must retain a software CCMP fallback beside SET_KEY: {needle}"
assert eapol.index("ieee80211_ccmp_set_key(") < eapol.index(
    "iwn_install_ap_ccmp_key(true, 0, apPtk.tk)"
), "software CCMP context must exist before hardware SET_KEY completes"

data = body(
    iwn,
    "bool ItlIwn::iwn_handle_ap_data(",
    "void ItlIwn::\ndetach(",
)
for needle in (
    "const bool hardwareDecrypted =",
    "mbuf_dup(packet, MBUF_DONTWAIT,",
    "ieee80211_ccmp_decrypt(",
    "mbuf_copydata(plain, 0, plainLength,",
    "AP protected RX software CCMP ",
):
    assert needle in data, \
        f"missing cold PAN software-CCMP fallback: {needle}"
assert data.index("if (!hardwareDecrypted)") < data.index(
    "ieee80211_ccmp_decrypt("
), "software CCMP must run only when firmware did not decrypt"

notif = body(
    iwn,
    "iwn_notif_intr(struct iwn_softc *sc)",
    "void ItlIwn::\niwn_wakeup_intr(",
)
for needle in (
    "completedAddNodeStatus = addNodeStatus;",
    "iwn_note_ap_firmware_event(",
    "completedAddNodeStatus);",
):
    assert needle in notif, f"missing firmware ADD_STA status handoff: {needle}"

assert "uint8_t apClientMaterializationStage;" in iwn_hpp
assert "int iwn_send_ap_assoc_success();" in iwn_hpp
assert "struct ieee80211_key apPairwiseSoftwareKey;" in iwn_hpp
assert "bool apSoftwareCcmpRxObserved;" in iwn_hpp

reset = body(
    iwn,
    "void ItlIwn::iwn_reset_ap_runtime_state()",
    "void ItlIwn::iwn_set_ap_scan_transition_blocked(",
)
assert "IWN_AP_CLIENT_MATERIALIZATION_IDLE" in reset
assert "ieee80211_ccmp_delete_key(" in reset
assert "apSoftwareCcmpRxObserved = false;" in reset

print("PASS: Tahoe IWN AP client materialization and cold CCMP fallback contract")
PY
