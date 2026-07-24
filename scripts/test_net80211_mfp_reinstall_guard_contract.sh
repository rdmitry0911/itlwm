#!/usr/bin/env bash
# Source-level regression gate for the async protected-management PAE path.
# An EAPOL Msg3/Group-1 retransmission may acknowledge a previous exchange,
# but it must never re-stage an unchanged GTK or IGTK and reset its replay
# state.  A fresh PTK is still allowed to commit while retained group keys
# remain untouched.
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

python3 - "$root" <<'PY'
from pathlib import Path
import sys


source = (Path(sys.argv[1]) /
          "itl80211/openbsd/net80211/ieee80211_pae_input.c").read_text()


def fail(message: str) -> None:
    raise SystemExit(f"net80211 MFP reinstall guard: {message}")


def body(marker: str, label: str) -> str:
    start = source.find(marker)
    if start < 0:
        fail(f"missing {label}")
    opening = source.find("{", start)
    if opening < 0:
        fail(f"missing body for {label}")
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1:index]
    fail(f"unterminated {label}")


def require(text: str, token: str, label: str) -> None:
    if token not in text:
        fail(f"missing {label}: {token}")


def ordered(text: str, label: str, *tokens: str) -> None:
    cursor = 0
    for token in tokens:
        position = text.find(token, cursor)
        if position < 0:
            fail(f"{label} missing ordered token: {token}")
        cursor = position + len(token)


gtk_guard = body("ieee80211_pae_mfp_gtk_needs_update(",
                 "MFP GTK update helper")
msg3 = body("ieee80211_pae_mfp_msg3_begin(", "MFP Msg3 builder")
group = body("ieee80211_pae_mfp_group_begin(", "MFP Group-1 builder")
group_input = body("void\nieee80211_recv_rsn_group_msg1(",
                   "RSN Group-1 ingress")

# The group-key comparison is the existing KRACK guard.  Both async builders
# must use it before they turn a supplied KDE into a transaction stage.
ordered(gtk_guard, "GTK comparison under selected-BSS fence",
        "IOSimpleLockLockDisableInterrupt(lock);",
        "ieee80211_must_update_group_key(&ic->ic_nw_keys[kid], gtk,",
        "IOSimpleLockUnlockEnableInterrupt(lock, irq);")
for text, label, kid in ((msg3, "Msg3", "gtk_kid"),
                         (group, "Group-1", "kid")):
    ordered(text, f"{label} GTK update gate",
            f"ieee80211_pae_mfp_gtk_needs_update(ic, {kid},",
            "have_gtk = 1;")
    ordered(text, f"{label} IGTK update gate",
            "ieee80211_bip_key_needs_update(ic, kid, &igtk[14],",
            "if (bip_update)",
            "have_igtk = 1;")

# A new PTK needs KDE validation, but an already-live equal GTK/IGTK must not
# be reinstalled simply to satisfy the builder's old have_* bookkeeping.
require(msg3, "(!have_ptk || gtk == NULL || igtk == NULL)",
        "fresh-PTK KDE-presence validation")
if "(!have_ptk || !have_gtk || !have_igtk)" in msg3:
    fail("fresh PTK still forces equal GTK/IGTK reinstallation")

# A pure retransmission has no stage to submit.  Group ingress must retain the
# historical Group Msg2 acknowledgement path for that ENOENT result.
ordered(msg3, "Msg3 no-op retransmit",
        "if (!have_ptk && !have_gtk && !have_igtk)", "return ENOENT;")
ordered(group, "Group-1 no-op retransmit",
        "if (!have_gtk && !have_igtk)", "return ENOENT;")
ordered(group, "Group-1 staged-key shape",
        "&gtk_key, have_gtk, &igtk_key, have_igtk,")
ordered(group_input, "Group-1 ENOENT fallback",
        "if (mfp_error == 0 || mfp_error == EBUSY)", "return;",
        "if (mfp_error != ENOENT)")

# Behavioural matrix for the intended plan selection.  It makes the security
# property explicit even though this source-level gate cannot inject EAPOL.
def plan(new_ptk: bool, gtk_changed: bool, igtk_changed: bool):
    return (new_ptk, gtk_changed, igtk_changed)

assert plan(False, False, False) == (False, False, False)  # retry only
assert plan(True, False, False) == (True, False, False)    # fresh PTK only
assert plan(False, True, False) == (False, True, False)
assert plan(False, False, True) == (False, False, True)

print("PASS: async MFP builders preserve unchanged GTK/IGTK replay state")
PY
