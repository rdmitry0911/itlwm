#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

python3 - "$PROJECT_DIR" <<'PY'
from pathlib import Path
import sys

root = Path(sys.argv[1])
iwn = (root / "itlwm/hal_iwn/ItlIwn.cpp").read_text()
bridge = (root / "include/ClientKit/AirportItlwmScanHomeAwayBridge.h").read_text()

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
policy = submit.index("airportItlwmGetScanHomeAwayTime(&configuredHomeAwayMs)")
default_max_out = submit.index("configuredHomeAwayMs : 200U")
default_pause = submit.index("configuredHomeAwayMs : 100U")
max_out = submit.index("hdr->max_out = htole32(maxOutMs * 1024U)")
pause = submit.index("hdr->pause_scan = htole32")
channel_loop = submit.index("for (c  = &ic->ic_channels[1]")
assert home < policy < default_max_out < default_pause < max_out < pause < channel_loop, \
    "AP-active foreground scans must program home/away before channels"
assert "if (pauseMs != 0)" in submit, \
    "an explicit zero WCL policy must leave DVM home/away fields disabled"
for needle in (
    "airportItlwmSetScanHomeAwayTime",
    "airportItlwmGetScanHomeAwayTime",
    "avoids changing the shared ieee80211com ABI layout",
):
    assert needle in bridge, f"missing layout-neutral home/away bridge rule: {needle}"

extensions = submit.index("wcl_foreground_5ghz_extended_dwell")
final_limit = submit.rindex("iwn_bound_scan_dwell(iwn_limit_dwell(sc, UINT16_MAX)")
publication = submit.index("chan->passive = htole16(dwell_passive)", final_limit)
assert extensions < final_limit < publication, \
    "STA/PAN and off-channel ceilings must dominate public-scan dwell extensions"
assert "le32toh(hdr->max_out), &dwell_active, &dwell_passive" in submit
assert "dwell_passive = dwell_active + 1" not in submit

print("PASS: Tahoe IWN APSTA scan home/away contract")
PY
