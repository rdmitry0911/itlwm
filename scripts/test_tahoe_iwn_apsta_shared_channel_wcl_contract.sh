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
snapshot = wcl.index("memcpy(cachedReassocRequest, data, sizeof(*data));")
empty_candidate = wcl.index("if (request.candidate_count == 0)")
retain = wcl.index("wcl_reassoc EMPTY_CANDIDATE_RETAIN_CURRENT_BSS")
pin_disarm = wcl.index("ieee80211_public_initial_bssid_pin_disarm(ic)")
lease_retire = wcl.index("getTahoeOwnerRegistry().association =")
assert snapshot < empty_candidate < retain

# The existing net80211 candidate predicate deliberately requires an explicit
# candidate before it can select a replacement BSS.  An empty carrier must
# therefore retain the healthy source BSS rather than starting a scan that
# can only terminate with NO_ELIGIBLE_TARGET; this is the shared-channel
# APSTA precondition exercised by the on-air CoreWLAN HostAP sequence.
assert "requires an explicit candidate" in wcl
assert "preserve the current BSS" in wcl

query = wcl.index("instance->getAPSTAPrimaryRoamSharedChannel()")
channel_filter = wcl.index("request.channel_spec[i] & 0xffU", query)
candidate_filter = wcl.index(
    "request.candidate[i].channel_spec & 0xffU", query)
filtered_empty = wcl.index("wcl_reassoc APSTA_FILTERED_EMPTY_RETAIN_CURRENT_BSS")
scan = wcl.index("ieee80211_begin_wcl_reassoc_bgscan", query)
assert empty_candidate < query < channel_filter < candidate_filter < filtered_empty
assert filtered_empty < pin_disarm < lease_retire < scan
assert wcl.index("return kIOReturnSuccess;", filtered_empty) < pin_disarm
assert "request.channel_spec[0] = requiredSharedChannel;" in wcl
assert "if (retainedChannels == 0)\n                return kIOReturnBusy;" in wcl
assert "wcl_reassoc APSTA_SHARED_CHANNEL" in wcl
assert "same no-target carrier as the literal-empty case" in wcl

print("PASS: Tahoe IWN APSTA retains a live BSS for literal and "
      "shared-channel-filtered empty WCL reassoc carriers")
PY
