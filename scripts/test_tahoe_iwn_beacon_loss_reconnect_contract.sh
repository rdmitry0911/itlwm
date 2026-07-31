#!/usr/bin/env bash
# Regression gate for the IWN hard-AP-outage reconnect path.
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

python3 - "$root" <<'PY'
from pathlib import Path
import sys


root = Path(sys.argv[1])
var = (root / "itl80211/openbsd/net80211/ieee80211_var.h").read_text()
iwn = (root / "itlwm/hal_iwn/ItlIwn.cpp").read_text()
controller = (root / "AirportItlwm/AirportItlwmV2.cpp").read_text()


def fail(message: str) -> None:
    raise SystemExit(f"Tahoe IWN beacon-loss reconnect: {message}")


def body(text: str, marker: str, label: str) -> str:
    start = text.find(marker)
    if start < 0:
        fail(f"missing {label}")
    opening = text.find("{", start)
    if opening < 0:
        fail(f"missing body for {label}")
    depth = 0
    for index in range(opening, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[opening + 1:index]
    fail(f"unterminated {label}")


def ordered(text: str, label: str, *tokens: str) -> None:
    cursor = 0
    for token in tokens:
        position = text.find(token, cursor)
        if position < 0:
            fail(f"{label} missing ordered token: {token}")
        cursor = position + len(token)


if "#define IEEE80211_EVT_STA_BEACON_LOSS              21" not in var:
    fail("missing distinct beacon-loss event")

notif_start = iwn.find("case IWN_BEACON_MISSED:")
notif_end = iwn.find("case IWN_UC_READY:", notif_start)
if notif_start < 0 or notif_end < 0:
    fail("missing bounded IWN_BEACON_MISSED notification case")
notif = iwn[notif_start:notif_end]
ordered(
    notif,
    "firmware threshold to reconnect transition",
    "missed = letoh32(miss->consecutive)",
    "missed > ic->ic_bmissthres && !ic->ic_mgt_timer",
    "IEEE80211_EVT_STA_BEACON_LOSS",
    "ieee80211_new_state(ic, IEEE80211_S_SCAN, -1)",
)
if "IEEE80211_FC0_SUBTYPE_PROBE_REQ" in notif:
    fail("beacon-loss path still arms the minute-long directed-probe timeout")
if "IEEE80211_EVT_STA_DEAUTH" in notif:
    fail("beacon loss must not impersonate a received deauthentication")

handler = body(
    controller,
    "void AirportItlwm::\neventHandler(",
    "controller event handler",
)
loss_start = handler.find("case IEEE80211_EVT_STA_BEACON_LOSS:")
scan_start = handler.find("case IEEE80211_EVT_SCAN_DONE:", loss_start)
if loss_start < 0 or scan_start < 0:
    fail("missing bounded controller beacon-loss case")
loss = handler[loss_start:scan_start]
ordered(
    loss,
    "WCL beacon-loss publication",
    "clearTahoeWclAuthAssocCompletionLeaseGated",
    "postTahoeWclLinkDownIndGated",
    "(void *)(uintptr_t)1U",
    "return;",
)
if "APPLE80211_M_DEAUTH_RECEIVED" in loss:
    fail("beacon-loss controller case publishes DEAUTH_RECEIVED")

print("Tahoe IWN beacon-loss reconnect contract: PASS")
PY
