#!/bin/bash
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"

python3 - "$root" <<'PY'
from pathlib import Path
import sys

root = Path(sys.argv[1])
owner = (root / "AirportItlwm/AirportItlwmAPSTAOwner.cpp").read_text()
iwn = (root / "itlwm/hal_iwn/ItlIwn.cpp").read_text()
hal = (root / "include/HAL/ItlHalService.hpp").read_text()
iwm_hpp = (root / "itlwm/hal_iwm/ItlIwm.hpp").read_text()
iwx_hpp = (root / "itlwm/hal_iwx/ItlIwx.hpp").read_text()

start = owner[owner.index("IOReturn AirportItlwmAPSTAOwner::startLowerIfReady()"):
              owner.index("IOReturn AirportItlwmAPSTAOwner::stopLower()")]
shared = start.index("requiresAPSTASharedChannel()")
primary = start.index("getAPSTARequiredSharedChannel()", shared)
align = start.index("apChannel = primaryChannel", primary)
config = start.index("cfg.channel = apChannel", align)
beacon = start.index("apsta_build_beacon(", config)
lower = start.index("startAPMode(&cfg)", beacon)
assert shared < primary < align < config < beacon < lower
assert "APSTA public start shared channel follows primary" in start
assert "ic->ic_opmode" not in start
assert "num_different_channels == 1" in iwn[
    iwn.index("bool ItlIwn::requiresAPSTASharedChannel() const"):
    iwn.index("bool ItlIwn::isPrimaryStaRecoveryScanPending() const")]
assert "requiresAPSTASharedChannel() const { return false; }" in hal
assert "requiresAPSTASharedChannel" not in iwm_hpp
assert "requiresAPSTASharedChannel" not in iwx_hpp

iwn_start = iwn[iwn.index("IOReturn ItlIwn::startAPMode("):
                iwn.index("IOReturn ItlIwn::stopAPMode()")]
helper = iwn[iwn.index("iwn_apsta_primary_channel(struct iwn_softc *sc)"):
             iwn.index("IOReturn ItlIwn::startAPMode(")]
assert "ic->ic_opmode != IEEE80211_M_STA" not in helper
steady = helper.index("ic->ic_state == IEEE80211_S_RUN")
steady_channel = helper.index(
    "ieee80211_chan2ieee(ic, ic->ic_bss->ni_chan)", steady)
rxon_filter = helper.index("IWN_FILTER_BSS", steady_channel)
rxon_aid = helper.index("IEEE80211_AID(le16toh(sc->rxon.associd))", rxon_filter)
rxon_channel = helper.index("return rxonChannel;", rxon_aid)
assert steady < steady_channel < rxon_filter < rxon_aid < rxon_channel
assert "an unassociated discovery RXON must not pin" in helper

query = iwn_start.index("iwn_apsta_primary_channel(&com)")
guard = iwn_start.index("config->channel != primaryChannel", query)
reject = iwn_start.index("return kIOReturnBusy;", guard)
quiesce = iwn_start.index("iwn_quiesce_scan_for_ap_transition()", reject)
assert query < guard < reject < quiesce
assert "rejecting off-channel AP start" in iwn_start

csa = iwn[iwn.index("IOReturn ItlIwn::triggerAPCSA("):
          iwn.index("uint16_t ItlIwn::getAPCurrentChannel() const")]
csa_query = csa.index("iwn_apsta_primary_channel(&com)")
csa_guard = csa.index("csa->channel != primaryChannel", csa_query)
csa_reject = csa.index("return kIOReturnBusy;", csa_guard)
csa_same = csa.index("csa->channel == apFirmwareConfig.channel", csa_reject)
assert csa_query < csa_guard < csa_reject < csa_same
assert "rejecting off-channel AP CSA" in csa

print("PASS: Tahoe IWN public AP start and CSA cannot create split-channel APSTA")
PY
