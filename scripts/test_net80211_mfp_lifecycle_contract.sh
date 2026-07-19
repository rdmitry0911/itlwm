#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
pae_source="$root/itl80211/openbsd/net80211/ieee80211_pae_input.c"
node_source="$root/itl80211/openbsd/net80211/ieee80211_node.c"

/usr/bin/python3 - "$pae_source" "$node_source" <<'PY'
import pathlib
import re
import sys


def function_body(text, name):
    match = re.search(
        r"\nvoid\n" + re.escape(name) + r"\([^\n]*\)\n\{(?P<body>.*?)\n\}\n",
        text,
        re.DOTALL,
    )
    assert match, name
    return match.group("body")


pae = pathlib.Path(sys.argv[1]).read_text()
node = pathlib.Path(sys.argv[2]).read_text()

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

print("PASS: net80211 MFP IGTK lifecycle contract")
PY
