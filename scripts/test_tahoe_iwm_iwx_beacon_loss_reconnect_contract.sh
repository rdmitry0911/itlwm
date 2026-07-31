#!/usr/bin/env bash
# Regression gate for modern Intel hard-AP-outage reconnect paths.
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

python3 - "$root" <<'PY'
from pathlib import Path
import sys


root = Path(sys.argv[1])
families = {
    "IWM": (
        root / "itlwm/hal_iwm/mac80211.cpp",
        "iwm_rx_bmiss(",
        "\nstatic int\niwm_rate2ridx(",
    ),
    "IWX": (
        root / "itlwm/hal_iwx/ItlIwx.cpp",
        "iwx_rx_bmiss(",
        "\nint ItlIwx::\niwx_binding_cmd(",
    ),
}


def fail(message: str) -> None:
    raise SystemExit(f"Tahoe IWM/IWX beacon-loss reconnect: {message}")


def ordered(text: str, label: str, *tokens: str) -> None:
    cursor = 0
    for token in tokens:
        position = text.find(token, cursor)
        if position < 0:
            fail(f"{label} missing ordered token: {token}")
        cursor = position + len(token)


for family, (path, start_marker, end_marker) in families.items():
    text = path.read_text()
    start = text.find(start_marker)
    end = text.find(end_marker, start)
    if start < 0 or end < 0:
        fail(f"missing bounded {family} missed-beacon handler")
    handler = text[start:end]
    ordered(
        handler,
        f"{family} firmware threshold to reconnect transition",
        "consec_missed_beacons_since_last_rx",
        "missed > ic->ic_bmissthres && ic->ic_mgt_timer == 0",
        "IEEE80211_EVT_STA_BEACON_LOSS",
        "ieee80211_new_state(ic, IEEE80211_S_SCAN, -1)",
    )
    if "IEEE80211_FC0_SUBTYPE_PROBE_REQ" in handler:
        fail(f"{family} still arms the minute-long directed-probe timeout")
    if "IEEE80211_EVT_STA_DEAUTH" in handler:
        fail(f"{family} beacon loss impersonates received deauthentication")

print("Tahoe IWM/IWX beacon-loss reconnect contract: PASS")
PY
