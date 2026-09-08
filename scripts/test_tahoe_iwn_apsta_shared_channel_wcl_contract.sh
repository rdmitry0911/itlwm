#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

python3 - "$PROJECT_DIR" <<'PY'
from pathlib import Path
import sys

root = Path(sys.argv[1])
hal = (root / "include/HAL/ItlHalService.hpp").read_text()
iwn_hpp = (root / "itlwm/hal_iwn/ItlIwn.hpp").read_text()
iwn = (root / "itlwm/hal_iwn/ItlIwn.cpp").read_text()
sky = (root / "AirportItlwm/AirportItlwmSkywalkInterface.cpp").read_text()

assert "getAPSTARequiredSharedChannel() const { return 0; }" in hal
assert "getAPSTARequiredSharedChannel() const override;" in iwn_hpp

iwn_query = iwn[
    iwn.index("uint16_t ItlIwn::getAPSTARequiredSharedChannel() const"):
    iwn.index("bool ItlIwn::isPrimaryStaRecoveryScanPending() const")
]
assert "iwn_apsta_primary_channel(" in iwn_query
assert "primaryChannel != 0 ? primaryChannel : getAPCurrentChannel()" in iwn_query
assert "num_different_channels == 1" in iwn_query

wcl = sky[
    sky.index("IOReturn AirportItlwmSkywalkInterface::\nsetWCL_REASSOC("):
    sky.index("IOReturn AirportItlwmSkywalkInterface::\nsetWCL_LEGACY_ROAM_PROFILE_CONFIG(")
]
query = wcl.index("fHalService->getAPSTARequiredSharedChannel()")
channel_filter = wcl.index("request.channel_spec[i] & 0xffU", query)
candidate_filter = wcl.index(
    "request.candidate[i].channel_spec & 0xffU", query)
scan = wcl.index("ieee80211_begin_wcl_reassoc_bgscan", query)
assert query < channel_filter < candidate_filter < scan
assert "request.channel_spec[0] = requiredSharedChannel;" in wcl
assert "if (retainedChannels == 0)\n                return kIOReturnBusy;" in wcl
assert "wcl_reassoc APSTA_SHARED_CHANNEL" in wcl

print("PASS: Tahoe IWN APSTA WCL target remains on the shared DVM channel")
PY
