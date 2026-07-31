#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

python3 - "$PROJECT_DIR" <<'PY'
from pathlib import Path
import sys

root = Path(sys.argv[1])
iwn = (root / "itlwm/hal_iwn/ItlIwn.cpp").read_text()

limit = iwn[
    iwn.index("iwn_limit_dwell(struct iwn_softc *sc"):
    iwn.index("iwn_get_passive_dwell_time(")
]
for needle in (
    "apFirmwareStage == IWN_AP_STAGE_RUNNING",
    "IEEE80211_AID(le16toh(sc->rxon.associd)) != 0",
    "apFirmwareConfig.beaconInterval",
    "IWN_CHANNEL_TUNE_TIME * 2",
    "available / static_cast<int>(activeContexts)",
):
    assert needle in limit, f"missing DVM multi-context dwell rule: {needle}"

submit = iwn[
    iwn.index("iwn_scan_submit(struct iwn_softc *sc, uint16_t flags"):
    iwn.index("void ItlIwn::\niwn_scan_abort(")
]
home = submit.index("if (bgscan || apContextRunning)")
max_out = submit.index("hdr->max_out = htole32(200 * 1024)")
pause = submit.index("hdr->pause_scan = htole32")
channel_loop = submit.index("for (c  = &ic->ic_channels[1]")
assert home < max_out < pause < channel_loop, \
    "AP-active foreground scans must program home/away before channels"

extensions = submit.index("wcl_foreground_5ghz_extended_dwell")
final_limit = submit.rindex("dwell_passive = iwn_limit_dwell(sc, dwell_passive)")
validity = submit.index("if (dwell_passive <= dwell_active)", final_limit)
assert extensions < final_limit < validity, \
    "PAN dwell ceiling must dominate public-scan dwell extensions"

print("PASS: Tahoe IWN APSTA scan home/away contract")
PY
