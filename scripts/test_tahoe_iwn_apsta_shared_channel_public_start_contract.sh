#!/bin/bash
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"

python3 - "$root" <<'PY'
from pathlib import Path
import sys

root = Path(sys.argv[1])
owner = (root / "AirportItlwm/AirportItlwmAPSTAOwner.cpp").read_text()
owner_hpp = (root / "AirportItlwm/AirportItlwmAPSTAOwner.hpp").read_text()
iwn = (root / "itlwm/hal_iwn/ItlIwn.cpp").read_text()
v2 = (root / "AirportItlwm/AirportItlwmV2.cpp").read_text()
v2_hpp = (root / "AirportItlwm/AirportItlwmV2.hpp").read_text()
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

# startAPMode() accepts an asynchronous DVM PAN transition before it reaches
# RUNNING.  The APSTA watchdog uses getAPCurrentChannel()==0 as evidence of a
# destructive lower reset, so the accepted in-flight profile must retain its
# channel through that short interval.  A real terminal failure resets the
# runtime and still reports zero.
current_channel = iwn[iwn.index("uint16_t ItlIwn::getAPCurrentChannel() const"):
                      iwn.index("bool ItlIwn::requiresAPSTASharedChannel() const")]
assert "apFirmwareTransitionActive is set only after the profile" in current_channel
assert "!apFirmwareTransitionActive || apFirmwareConfig.channel == 0" in current_channel
assert "return apFirmwareConfig.channel;" in current_channel
assert "apFirmwareStage != IWN_AP_STAGE_RUNNING" not in current_channel
assert "iwn_reset_ap_runtime_state()" in current_channel

# A public role-7 AP start must not destroy an already-associated STA before
# the lower HostAP carrier can share that same radio channel.  The reservation
# is armed only by the exact existing Internet Sharing interface-enable event,
# consumes only its observed generic RUN -> SCAN(-1) handoff, and disarms on
# every first attempt so it cannot become a general scan veto.
assert "bool consumePrimaryStaHandoffScan(struct ieee80211com *ic, int arg);" in owner_hpp
assert "bool armPrimaryStaHandoffScan(struct ieee80211com *ic);" in owner_hpp
assert "bool primaryStaHandoffScanArmed;" in owner_hpp
note = owner[owner.index("void AirportItlwmAPSTAOwner::noteInterfaceEnableDuringPendingHostAPStart()"):
             owner.index("bool AirportItlwmAPSTAOwner::consumePrimaryStaHandoffScan(")]
for token in (
    "initialHostAPAdmissionPending",
    "interfaceDrivenHostAPConfirmationPending = true;",
    "ic->ic_state == IEEE80211_S_RUN",
    "ic->ic_opmode == IEEE80211_M_STA",
    "ic->ic_bss != nullptr",
    "ic->ic_bss->ni_port_valid",
    "(void)armPrimaryStaHandoffScan(ic);",
):
    assert token in note, f"missing public APSTA handoff arm: {token}"
arm = owner[owner.index("bool AirportItlwmAPSTAOwner::armPrimaryStaHandoffScan("):
            owner.index("bool AirportItlwmAPSTAOwner::consumePrimaryStaHandoffScan(")]
for token in (
    "owner->fHalService->get80211Controller() == ic",
    "!lowerStopPending", "!isApRunning()",
    "ic->ic_state == IEEE80211_S_RUN",
    "ic->ic_opmode == IEEE80211_M_STA",
    "ic->ic_bss->ni_port_valid",
):
    assert token in arm, f"missing handoff live-BSS arm fence: {token}"
consume = owner[owner.index("bool AirportItlwmAPSTAOwner::consumePrimaryStaHandoffScan("):
                owner.index("bool AirportItlwmAPSTAOwner::matchesBSDName")]
clear = consume.index("primaryStaHandoffScanArmed = false;")
generic = consume.index("arg != -1")
live = consume.index("ic->ic_bss->ni_port_valid")
assert clear < generic < live
assert "return true;" in consume
stop = owner[owner.index("IOReturn AirportItlwmAPSTAOwner::setHostAPMode("):
             owner.index("IOReturn AirportItlwmAPSTAOwner::setCipherKey(")]
null_carrier = stop.index("if (in == nullptr || in->ssidLength1c == 0)")
assert stop.index("primaryStaHandoffScanArmed = false;", null_carrier) > null_carrier
assert "primaryStaHandoffScanArmed = false;" in owner[
    owner.index("void AirportItlwmAPSTAOwner::resetRuntimeState()"):
    owner.index("void AirportItlwmAPSTAOwner::setSoftAPPowerSaveState(")]

iwn_stop = iwn[iwn.index("IOReturn ItlIwn::stopAPMode()"):
               iwn.index("IOReturn ItlIwn::setAPMaxStations(")]
assert "apFirmwareStage == IWN_AP_STAGE_STOP_RXON" in iwn_stop
assert "apFirmwareStage == IWN_AP_STAGE_STOP_PAN_PARAMS" in iwn_stop
assert iwn_stop.count("return kIOReturnNotReady;") >= 2, \
    "IWN HostAP stop must remain pending until its firmware terminal"
assert "iwn_reset_ap_runtime_state();\n        return kIOReturnSuccess;" in iwn_stop, \
    "IWN HostAP stop may report terminal only after its AP runtime is gone"

assert "bool consumeAPSTAPrimaryStaHandoffScan(struct ieee80211com *ic, int arg);" in v2_hpp
assert "void noteAPSTASharedChannelFilteredWclReassoc(struct ieee80211com *ic);" in v2_hpp
assert "fAPSTAOwner->armPrimaryStaHandoffScan(ic)" in v2
bridge = v2[v2.index("extern \"C\" bool\nairportItlwmConsumeAPSTAPrimaryStaHandoffScan("):
            v2.index("void AirportItlwm::teardownAPSTAInterface()")]
assert "OSDynamicCast(AirportItlwm, controller)" in bridge
assert "airport->consumeAPSTAPrimaryStaHandoffScan(ic, arg)" in bridge
preflight = iwn[iwn.index("iwn_newstate_preflight(struct ieee80211com *ic"):
                iwn.index("void ItlIwn::\niwn_scan_lease_replay_task")]
handoff = preflight.index("airportItlwmConsumeAPSTAPrimaryStaHandoffScan(\n            that->getController(), ic, arg)")
rsn = preflight.index("iwn_rsn_join_scan_blocked(ic)")
scan_lease = preflight.index("iwn_scan_lease_defer_scan")
assert handoff < rsn < scan_lease, \
    "public APSTA handoff must stop RUN->SCAN before BSS teardown or scan ownership"
reassoc = (root / "AirportItlwm/AirportItlwmSkywalkInterface.cpp").read_text()
filtered = reassoc[reassoc.index("wcl_reassoc APSTA_FILTERED_EMPTY_RETAIN_CURRENT_BSS") - 1200:
                   reassoc.index("wcl_reassoc APSTA_FILTERED_EMPTY_RETAIN_CURRENT_BSS") + 500]
assert "instance->noteAPSTASharedChannelFilteredWclReassoc(ic);" in filtered
assert "off-channel roam candidates" in filtered

print("PASS: Tahoe IWN public AP start and CSA cannot create split-channel APSTA")
PY
