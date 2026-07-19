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

igtk_install = re.compile(
    r"/\* install the IGTK \*/\s*"
    r"switch \(\(\*ic->ic_set_key\)\(ic, ni, k\)\) \{\s*"
    r"case 0:\s*"
    r"ni->ni_flags \|= IEEE80211_NODE_TXMGMTPROT;\s*"
    r"ic->ic_igtk_kid = kid;\s*"
    r"break;\s*"
    r"case EBUSY:.*?"
    r"default:\s*"
    r"reason = IEEE80211_REASON_AUTH_LEAVE;\s*"
    r"goto deauth;\s*"
    r"\}\s*"
    r"ni->ni_flags \|= IEEE80211_NODE_RXMGMTPROT;",
    re.DOTALL,
)

assert len(igtk_install.findall(pae)) == 2, "both IGTK install paths"

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

# AX211's API-68 payload is evidence, not an enabled Tahoe feature.  The
# port's task and splnet compatibility layers cannot serialize the command
# ring or pair firmware completion with the PAE Msg3 tail, so this driver
# must not advertise MFP or submit its protected-management command.
setkey = cpp_method_body(iwx_source, "int ItlIwx::\niwx_set_key(")
deletekey = cpp_method_body(iwx_source, "void ItlIwx::\niwx_delete_key(")
mfp_guard = (
    "if (k->k_cipher == IEEE80211_CIPHER_BIP ||\n"
    "        (ni != NULL && (ni->ni_flags & IEEE80211_NODE_MFP) != 0))\n"
    "        return EOPNOTSUPP;"
)
mfp_delete_guard = mfp_guard.replace("\n        return EOPNOTSUPP;", " {")
assert mfp_guard in setkey, "forced MFP/BIP key install fails closed"
assert mfp_delete_guard in deletekey, (
    "forced MFP/BIP teardown is software-only"
)
assert setkey.index(mfp_guard) < setkey.index(
    "if (k->k_cipher != IEEE80211_CIPHER_CCMP)"
), "MFP rejection precedes every normal key submission"
assert deletekey.index("ieee80211_delete_key(ic, ni, k);") < deletekey.index(
    "if (k->k_cipher != IEEE80211_CIPHER_CCMP)"
), "MFP teardown does not reach ADD_STA_KEY"

for forbidden in (
    "IWX_MGMT_MCAST_KEY",
    "IWX_STA_KEY_MFP",
    "iwx_set_sta_igtk_v2",
    "iwx_ax211_api68_igtk_v2_ok",
    "IwxMfpIgtkContracts",
    "ic->ic_caps |= IEEE80211_C_MFP;",
):
    assert forbidden not in iwx_source, forbidden

mfp_clear = "ic->ic_caps &= ~IEEE80211_C_MFP;"
last_normal_cap = "ic->ic_caps |= IEEE80211_C_SUPPORTS_VHT_EXT_NSS_BW;"
assert iwx_source.count(mfp_clear) == 1, "single explicit MFP quarantine"
assert iwx_source.index(last_normal_cap) < iwx_source.index(mfp_clear), (
    "MFP remains clear after the complete iwx capability set"
)

print("PASS: net80211 MFP RX-route and fail-closed quarantine contract")
PY
