#!/usr/bin/env bash
# Source contract for basic IWM association compatibility after the 802.11v layer.
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

python3 - "$root" <<'PY'
from pathlib import Path
import sys


root = Path(sys.argv[1])
var_h = (root / "itl80211/openbsd/net80211/ieee80211_var.h").read_text()
output_c = (root / "itl80211/openbsd/net80211/ieee80211_output.c").read_text()
input_c = (root / "itl80211/openbsd/net80211/ieee80211_input.c").read_text()
iwn = (root / "itlwm/hal_iwn/ItlIwn.cpp").read_text()
iwx = (root / "itlwm/hal_iwx/ItlIwx.cpp").read_text()
iwm = (root / "itlwm/hal_iwm/mac80211.cpp").read_text()


def fail(message: str) -> None:
    raise SystemExit(f"IWM association compatibility contract: {message}")


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


require(var_h, "IEEE80211_C_WNM_BSS_TRANSITION 0x00200000",
        "explicit BTM backend capability")
require(output_c,
        "((ic->ic_caps & IEEE80211_C_WNM_BSS_TRANSITION) ? 2 + 3 : 0)",
        "conditional association-frame reservation")
ordered(output_c, "conditional BTM association IE",
        "if (ic->ic_caps & IEEE80211_C_WNM_BSS_TRANSITION)",
        "frm = ieee80211_add_wnm_extcaps(frm);")
require(iwn, "IEEE80211_C_WNM_BSS_TRANSITION",
        "runtime-proven IWN BTM capability")
require(iwx, "IEEE80211_C_WNM_BSS_TRANSITION",
        "IWX BTM capability")

iwm_caps_start = iwm.find("ic->ic_caps =")
iwm_caps_end = iwm.find("ic->ic_htcaps =", iwm_caps_start)
if iwm_caps_start < 0 or iwm_caps_end < 0:
    fail("cannot isolate IWM capability assignment")
if "IEEE80211_C_WNM_BSS_TRANSITION" in iwm[iwm_caps_start:iwm_caps_end]:
    fail("IWM must retain the pre-802.11v association frame until proven")

for source, token, label in (
        (output_c, "ieee80211_mgmt_output: enqueue ASSOC",
         "generic association enqueue evidence"),
        (iwm, "_iwm_start_task: dequeue ASSOC",
         "IWM association dequeue evidence"),
        (iwm, "iwm_tx: ASSOC doorbell",
         "IWM association doorbell evidence"),
        (iwm, "iwm_rx_tx_cmd_single: ASSOC completion",
         "IWM association completion evidence"),
        (input_c, "ieee80211_recv_assoc_resp: ASSOC RX",
         "net80211 association-response evidence"),
):
    require(source, token, label)

print("PASS: IWM keeps its proven association frame while IWN/IWX retain BTM and every IWM ASSOC TX/RX boundary is observable")
PY
