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
    iwn.index("int ItlIwn::iwn_set_ap_sta_scan_priority(")
]
assert "apStaBssAssociated" in pan
assert "bssSlotWidth = beaconInterval / 2;" in pan
assert "panSlotWidth = beaconInterval - bssSlotWidth;" in pan

timing = iwn[
    iwn.index("int ItlIwn::iwn_send_ap_timing("):
    iwn.index("int ItlIwn::iwn_send_ap_edca(")
]
assert "apStaBssAssociated && bss != NULL && bss->ni_intval != 0" in timing
assert "memcpy(&command.tstamp, bss->ni_tstamp" in timing
assert "beaconInterval = bss->ni_intval;" in timing
assert "timestamp % intervalUsec" in timing
assert 'retainedBssTiming ? "retained-BSS" : "standalone"' in timing

assert "APSTA PAN slots bss=%u pan=%u priority=%u" in pan
assert '"scan=%u auth=%u bss_associated=%u stage=%u\\n"' in pan

print("PASS: Tahoe APSTA start retains an associated BSS and its timing across scan state")
PY
