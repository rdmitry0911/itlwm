#!/usr/bin/env bash
# Regression gate for a received STA deauth/disassoc while RUN.  The peer has
# invalidated the selected BSS, so a fresh scan is required; re-authenticating
# that BSS can wedge IWN DVM firmware after an AP disappearance.
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

python3 - "$root" <<'PY'
from pathlib import Path
import sys


source = (Path(sys.argv[1]) / "itl80211/openbsd/net80211/ieee80211_input.c").read_text()


def fail(message: str) -> None:
    raise SystemExit(f"Tahoe STA link-loss scan: {message}")


def body(marker: str) -> str:
    start = source.find(marker)
    if start < 0:
        fail(f"missing marker: {marker}")
    opening = source.find("{", start)
    if opening < 0:
        fail(f"missing body: {marker}")
    depth = 0
    for position in range(opening, len(source)):
        if source[position] == "{":
            depth += 1
        elif source[position] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1:position]
    fail(f"unterminated body: {marker}")


def ordered(text: str, label: str, *tokens: str) -> None:
    cursor = 0
    for token in tokens:
        position = text.find(token, cursor)
        if position < 0:
            fail(f"{label} missing ordered token: {token}")
        cursor = position + len(token)


deauth = body("void\nieee80211_recv_deauth")
ordered(
    deauth,
    "deauth notification before lower leave",
    "IEEE80211_EVT_STA_DEAUTH",
    "if (!(roamscan || stay_auth))",
    "if (ic->ic_state == IEEE80211_S_RUN)",
    "ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);",
)
if "IEEE80211_FC0_SUBTYPE_DEAUTH" not in deauth:
    fail("deauth must retain the AUTH-state fallback")

disassoc = body("void\nieee80211_recv_disassoc")
ordered(
    disassoc,
    "disassoc clean leave",
    "if (!roamscan)",
    "if (ic->ic_state == IEEE80211_S_RUN)",
    "ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);",
)
if "IEEE80211_FC0_SUBTYPE_DISASSOC" not in disassoc:
    fail("disassoc must retain the ASSOC-state fallback")

print("Tahoe STA link-loss scan contract: PASS")
PY
