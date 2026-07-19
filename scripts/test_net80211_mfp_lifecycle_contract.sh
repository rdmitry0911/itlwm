#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
pae_source="$root/itl80211/openbsd/net80211/ieee80211_pae_input.c"
node_source="$root/itl80211/openbsd/net80211/ieee80211_node.c"
input_source="$root/itl80211/openbsd/net80211/ieee80211_input.c"
iwx_source="$root/itlwm/hal_iwx/ItlIwx.cpp"

/usr/bin/python3 - "$pae_source" "$node_source" "$input_source" "$iwx_source" <<'PY'
import pathlib
import re
import sys


def function_body(text, name):
    match = re.search(
        r"\nvoid\n" + re.escape(name) + r"\([^)]*\)\n\{(?P<body>.*?)\n\}\n",
        text,
        re.DOTALL,
    )
    assert match, name
    return match.group("body")


def cpp_method_body(text, marker):
    start = text.index(marker)
    opening = text.index("{", start)
    depth = 0
    for pos in range(opening, len(text)):
        if text[pos] == "{":
            depth += 1
        elif text[pos] == "}":
            depth -= 1
            if depth == 0:
                return text[opening + 1:pos]
    raise AssertionError(marker)


pae = pathlib.Path(sys.argv[1]).read_text()
node = pathlib.Path(sys.argv[2]).read_text()
input_source = pathlib.Path(sys.argv[3]).read_text()
iwx_source = pathlib.Path(sys.argv[4]).read_text()

pae_wait = re.search(
    r"static int\n"
    r"ieee80211_pae_set_key\(struct ieee80211com \*ic, "
    r"struct ieee80211_node \*ni,\s*"
    r"struct ieee80211_key \*k\)\n"
    r"\{(?P<body>.*?)\n\}",
    pae,
    re.DOTALL,
)
assert pae_wait, "MFP-aware PAE key helper"
pae_wait_body = pae_wait.group("body")
assert "IEEE80211_NODE_MFP" in pae_wait_body
assert "ic->ic_set_key_wait != NULL" in pae_wait_body
assert "(*ic->ic_set_key_wait)(ic, ni, k)" in pae_wait_body
assert "return (*ic->ic_set_key)(ic, ni, k);" in pae_wait_body
assert pae.count("switch (ieee80211_pae_set_key(ic, ni, k))") == 7, (
    "every PAE key-install path uses the MFP-aware helper"
)
assert "(*ic->ic_set_key)(ic, ni, k)" not in pae[pae_wait.end():], (
    "PAE does not bypass the wait-aware helper"
)

msg3 = function_body(pae, "ieee80211_recv_4way_msg3")
mfp_initial_igtk = msg3.index("IEEE80211_NODE_MFP | IEEE80211_NODE_RSN_NEW_PTK")
assert "igtk == NULL" in msg3[mfp_initial_igtk:]
assert mfp_initial_igtk < msg3.index("ieee80211_send_4way_msg4"), (
    "initial MFP Msg3 without IGTK fails before completing the handshake"
)

for name in ("ieee80211_node_join_rsn", "ieee80211_node_leave_rsn"):
    body = function_body(node, name)
    expected = (
        "ni->ni_flags &= ~IEEE80211_NODE_TXRXPROT;",
        "ni->ni_flags &= ~IEEE80211_NODE_RXMGMTPROT;",
        "ni->ni_flags &= ~IEEE80211_NODE_TXMGMTPROT;",
        "ni->ni_flags &= ~IEEE80211_NODE_RSN_NEW_PTK;",
    )
    positions = [body.index(anchor) for anchor in expected]
    assert positions == sorted(positions), name

input_body = function_body(input_source, "ieee80211_inputm")
mgmt_start = input_body.index("case IEEE80211_FC0_TYPE_MGT:")
mgmt_end = input_body.index("case IEEE80211_FC0_TYPE_CTL:", mgmt_start)
mgmt = input_body[mgmt_start:mgmt_end]
rx_route = (
    "int is_multicast, is_protected;",
    "is_multicast = IEEE80211_IS_MULTICAST(wh->i_addr1);",
    "is_protected = (wh->i_fc[1] & IEEE80211_FC1_PROTECTED);",
    "if (rxi->rxi_flags & IEEE80211_RXI_HWDEC) {",
    "m = ieee80211_input_hwdecrypt(ic, ni, m, rxi);",
    "} else if (!is_multicast && !is_protected) {",
    "ic->ic_stats.is_rx_unencrypted++;",
    "m = ieee80211_decrypt(ic, m, ni);",
    "ic->ic_stats.is_rx_wepfail++;",
    "(rxi->rxi_flags & IEEE80211_RXI_HWDEC))) {",
    "ic->ic_stats.is_rx_nowep++;",
)
positions = [mgmt.index(anchor) for anchor in rx_route]
assert positions == sorted(positions), "MMPDU single-owner RX route"

for anchor in (
    "htole32(IWX_RX_MPDU_RES_STATUS_CRC_OK)",
    "htole32(IWX_RX_MPDU_RES_STATUS_OVERRUN_OK)",
    "le32toh(desc->status) &",
    "iwx_rx_hwdecrypt(sc, m, le32toh(desc->status), &rxi)",
    "iwx_rx_frame(sc, m, chanidx, le32toh(desc->status),",
):
    assert anchor in iwx_source, anchor
assert "le16toh(desc->status)" not in iwx_source, "RX status must remain __le32"

# The recovered AX211/API-68 firmware key carriers are retained, but MFP must
# remain fail-closed until Msg3 becomes an async, generation-checked
# continuation on the RX workloop. The old deferred PAE worker is a separate
# kernel thread while splnet() is a no-op, so enabling it would race deauth,
# roam, and EAPOL timers.
setkey = cpp_method_body(iwx_source, "int ItlIwx::\niwx_set_key(")
setkey_wait = cpp_method_body(iwx_source, "int ItlIwx::\niwx_set_key_wait(")
setkey_impl = cpp_method_body(iwx_source, "int ItlIwx::\niwx_set_key_impl(")
deletekey = cpp_method_body(iwx_source, "void ItlIwx::\niwx_delete_key(")
checked_cmd = cpp_method_body(
    iwx_source, "int ItlIwx::\niwx_send_cmd_pdu_checked("
)
igtk_cmd = cpp_method_body(iwx_source, "static int\niwx_set_sta_igtk_v2(")
security_enqueue = cpp_method_body(
    iwx_source, "bool ItlIwx::\niwx_security_rx_enqueue("
)
assert "return that->iwx_set_key_impl(ic, ni, k, false);" in setkey
assert "return EOPNOTSUPP;" in setkey_wait
assert "iwx_set_key_impl" not in setkey_wait
assert "const bool mfp" in setkey_impl
assert "if (mfp && !iwx_mfp_runtime_enabled(sc))" in setkey_impl
assert "IWX_STA_KEY_MFP" in setkey_impl
assert "iwx_send_cmd_pdu_status(sc, IWX_ADD_STA_KEY" in setkey_impl
assert "(status & IWX_ADD_STA_STATUS_MASK) != IWX_ADD_STA_SUCCESS" in setkey_impl
assert "iwx_set_sta_igtk_v2(sc, ni, k, false, true)" in setkey_impl
assert "iwx_set_sta_igtk_v2(sc, ni, k, true, false)" in deletekey
assert "IWX_CMD_WANT_RESP" in checked_cmd
assert "cmd.resp_pkt == NULL" in checked_cmd
assert "iwx_rx_packet_len(cmd.resp_pkt) < sizeof(cmd.resp_pkt->hdr)" in checked_cmd
assert "IWX_CMD_FAILED_MSK" in checked_cmd
assert checked_cmd.count("iwx_free_resp(sc, &cmd);") == 3
assert "iwx_send_cmd_pdu_checked(sc, IWX_MGMT_MCAST_KEY" in igtk_cmd
assert "IWX_CMD_ASYNC" in igtk_cmd

assert "iwx_security_rx_eapol_input" in iwx_source
assert "iwx_security_rx_recv_mgmt" not in iwx_source
assert "iwx_security_rx_task_dispatch" in iwx_source
assert "sc->sc_security_rx_worker = current_thread();" in iwx_source
assert "sc->sc_security_rx_worker == current_thread()" in iwx_source
assert "IWX_SECURITY_RXQ_LEN" in iwx_source
assert "iwx_add_task(sc, sc->sc_nswq, &sc->security_rx_task);" in iwx_source
assert "iwx_security_rx_purge(sc);" in iwx_source
assert "ic->ic_eapol_key_input = NULL;" in iwx_source
assert "ic->ic_set_key_wait = NULL;" in iwx_source
assert "ic->ic_eapol_key_input = iwx_security_rx_eapol_input;" not in iwx_source
assert "ic->ic_set_key_wait = iwx_set_key_wait;" not in iwx_source
assert security_enqueue.index("held_ni = ieee80211_ref_node(ni);") < \
    security_enqueue.index("IOLockLock(sc->sc_task_gate_lock);"), (
        "node reference must not take splnet under the lifecycle gate"
    )
assert security_enqueue.index("IOLockUnlock(sc->sc_task_gate_lock);") < \
    security_enqueue.index("ieee80211_release_node(&sc->sc_ic, held_ni);"), (
        "rejected deferred frame releases its node outside the lifecycle gate"
    )

assert "IWX_MGMT_MCAST_KEY:" in iwx_source, (
    "firmware IGTK completion reaches the q0 completion path"
)
assert "iwx_ax211_api68_igtk_v2_ok" in iwx_source
assert "IwxMfpIgtkContracts::hasExactAbiPrerequisites" in iwx_source
assert "static bool\niwx_mfp_runtime_enabled" in iwx_source
runtime_gate = cpp_method_body(iwx_source, "static bool\niwx_mfp_runtime_enabled(")
assert "return false;" in runtime_gate
assert "iwx_publish_mfp_capability(sc);" in iwx_source
publish = cpp_method_body(iwx_source, "static void\niwx_publish_mfp_capability(")
assert "ic->ic_caps &= ~IEEE80211_C_MFP;" in publish
assert "IEEE80211_C_MFP" not in publish.replace("ic->ic_caps &= ~IEEE80211_C_MFP;", "")

mfp_clear = "ic->ic_caps &= ~IEEE80211_C_MFP;"
last_normal_cap = "ic->ic_caps |= IEEE80211_C_SUPPORTS_VHT_EXT_NSS_BW;"
assert iwx_source.count(mfp_clear) == 2, "initial and runtime-quarantined MFP clear"
assert iwx_source.index(last_normal_cap) < iwx_source.rindex(mfp_clear), (
    "MFP starts clear before the runtime gate is refreshed after init ucode"
)

print("PASS: net80211 MFP lifecycle contracts and runtime quarantine")
PY
