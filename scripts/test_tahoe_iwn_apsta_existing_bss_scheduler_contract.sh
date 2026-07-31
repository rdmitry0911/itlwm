#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

python3 - "$PROJECT_DIR" <<'PY'
from pathlib import Path
import sys

root = Path(sys.argv[1])
iwn = (root / "itlwm/hal_iwn/ItlIwn.cpp").read_text()

start = iwn[
    iwn.index("IOReturn ItlIwn::startAPMode("):
    iwn.index("IOReturn ItlIwn::stopAPMode()")
]
scan = start.index("iwn_quiesce_scan_for_ap_transition()")
rxon_filter = start.index("le32toh(com.rxon.filter) & IWN_FILTER_BSS")
rxon_aid = start.index("IEEE80211_AID(le16toh(com.rxon.associd))")
association = start.index("apStaBssAssociated =")
quiesce = start.index("iwn_set_ap_primary_tx_quiesced(true, false)")

assert scan < rxon_filter < association < quiesce
assert scan < rxon_aid < association < quiesce
assert "com.sc_ic.ic_state == IEEE80211_S_RUN || bssRxonAssociated" in start

pan = iwn[
    iwn.index("int ItlIwn::iwn_send_ap_pan_params("):
    iwn.index("int ItlIwn::iwn_set_ap_sta_pan_priority(")
]
assert "apStaBssAssociated" in pan
assert "bssSlotWidth = beaconInterval / 2;" in pan
assert "panSlotWidth = beaconInterval - bssSlotWidth;" in pan

print("PASS: Tahoe APSTA start retains an associated BSS across scan state")
PY
