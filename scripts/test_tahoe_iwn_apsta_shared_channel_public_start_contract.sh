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
infra = (root / "include/Airport/IO80211InfraInterface.h").read_text()
hal = (root / "include/HAL/ItlHalService.hpp").read_text()
iwm_hpp = (root / "itlwm/hal_iwm/ItlIwm.hpp").read_text()
iwx_hpp = (root / "itlwm/hal_iwx/ItlIwx.hpp").read_text()
proto_h = (root / "itl80211/openbsd/net80211/ieee80211_proto.h").read_text()

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
# Shared-channel selection is based solely on the HAL's authoritative primary
# channel. A later protected-STA witness is allowed immediately before the
# AP firmware handoff, but must not influence this channel choice.
assert "ic->ic_opmode" not in start[shared:start.index("ItlHalApConfig cfg")]
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

# The DVM AP start fences the primary STA output queue while its PAN RXON is
# transitioned.  The generic AP reset helper intentionally does not invoke
# if_start because it also serves destructive radio-reset paths.  The native
# WIPAN_PARAMS stop terminal is the non-destructive boundary: it must resume
# that existing queue after reset, without starting a new association or
# publishing a synthetic link/key event.
iwn_events = iwn[iwn.index("void ItlIwn::iwn_note_ap_firmware_event("):
                 iwn.index("static uint16_t\niwn_apsta_primary_channel(")]
stop_terminal = iwn_events[iwn_events.index(
    "if (apFirmwareStage == IWN_AP_STAGE_STOP_PAN_PARAMS)"):
    iwn_events.index("const int ridx =", iwn_events.index(
        "if (apFirmwareStage == IWN_AP_STAGE_STOP_PAN_PARAMS)"))]
reset = stop_terminal.index("iwn_reset_ap_runtime_state();")
resume = stop_terminal.index("iwn_set_ap_primary_tx_quiesced(false, true);")
assert reset < resume
assert "AP PAN stop terminal resumed primary STA output" in stop_terminal
for forbidden in (
    "ieee80211_new_state", "ieee80211_set_link_state", "handleKeyDone",
    "postMessage",
):
    assert forbidden not in stop_terminal, \
        f"AP PAN stop output recovery must not synthesize {forbidden}"

# DVM can leave a protected STA in logical RUN although the now-terminal PAN
# scheduler has returned the radio. The AP terminal must retire only an
# unconsumed protected WCL completion lease so the next real WCL carrier owns
# the exact candidate path. It must not force a generic scan or publish any
# connection completion itself.
assert "IEEE80211_NEWSTATE_ARG_APSTA_STOP_REJOIN" not in proto_h
lease = owner[owner.index("IOReturn AirportItlwmAPSTAOwner::driveLowerStopToTerminal()"):
              owner.index("void AirportItlwmAPSTAOwner::restoreRetainedPrimaryStaLinkAfterStop()")]
assert "lifecycle = kAirportItlwmAPSTAOwnerTerminal;" in lease
retire = lease.index("APSTA lower stop retired stale protected WCL lease")
restore = lease.index("restoreRetainedPrimaryStaLinkAfterStop();")
assert retire < restore
for required in (
    "ic->ic_state == IEEE80211_S_RUN",
    "(ic->ic_flags & IEEE80211_F_RSNON) != 0",
    "association.hasCarrier && !association.publicCarrier",
    "association.selectedFromCandidate",
    "association.authAssocCompletionArmed",
    "!association.joinTerminalObserved",
    "association = TahoeOwnerRegistry::AssociationOwner{};",
):
    assert required in lease
for forbidden in (
    "ieee80211_new_state", "ieee80211_set_link_state", "handleKeyDone",
    "postMessage",
):
    assert forbidden not in lease, \
        f"AP PAN lease retirement must not synthesize {forbidden}"
assert "ni_port_valid" not in lease, \
    "WCL can withdraw the port only after this lower terminal; it is not a lease fence"

# A retained STA BSS does not perform another four-way handshake after the
# asynchronous DVM PAN stop.  If Tahoe consumed a link-down during that
# transition, restore only through the normal net80211 bridge and only after
# the lower terminal, never by writing controller or Skywalk state directly.
assert "void restoreRetainedPrimaryStaLinkAfterStop();" in owner_hpp
terminal = owner[owner.index("IOReturn AirportItlwmAPSTAOwner::driveLowerStopToTerminal()"):
                 owner.index("void AirportItlwmAPSTAOwner::prepareEmptyAPForRadioReset()")]
restore_call = terminal.index("restoreRetainedPrimaryStaLinkAfterStop();")
terminal_state = terminal.index("lifecycle = kAirportItlwmAPSTAOwnerTerminal;")
assert terminal_state < restore_call
restore = owner[owner.index("void AirportItlwmAPSTAOwner::restoreRetainedPrimaryStaLinkAfterStop()"):
                owner.index("void AirportItlwmAPSTAOwner::prepareEmptyAPForRadioReset()")]
for token in (
    "ic->ic_opmode != IEEE80211_M_STA",
    "ic->ic_state != IEEE80211_S_RUN",
    "ic->ic_bss == nullptr",
    "!ic->ic_bss->ni_port_valid",
    "ieee80211_set_link_state(ic, LINK_STATE_UP);",
):
    assert token in restore, f"missing retained STA link restore fence: {token}"
assert "ifp->if_link_state == LINK_STATE_UP" not in restore, \
    "the AP-stop owner must not race net80211's compare-and-publish edge"
assert "ieee80211_set_link_state owns the compare-and-publish edge" in restore
bridge_up = restore.index("ieee80211_set_link_state(ic, LINK_STATE_UP);")
controller_down = restore.index(
    "(owner->currentStatus & kIONetworkLinkActive) == 0")
controller_up = restore.index(
    "owner->setLinkStatus(kIONetworkLinkValid | kIONetworkLinkActive,")
assert bridge_up < controller_down < controller_up
assert restore.count("setLinkStatus(") == 1
assert "AirportItlwm's existing publisher" in restore
assert "IORegistry" in restore
assert "fNetIf->" not in restore
assert "reportLinkStatus(" not in restore

# The inherited WCL link-reset consumes HostAP's transient down edge by
# clearing IO80211RSNDone.  Once IWN has reached the lower terminal, restore
# only that public key-complete property for an already-authorized protected
# retained STA.  A duplicate RSN handshake, WCL association edge, EAPOL, or
# direct IORegistry write would be unsafe here.
assert "void handleKeyDone(bool, bool);" in infra
assert "restoreRetainedPrimaryStaRsnStateGated" in v2_hpp
rsn_restore = v2[v2.index("restoreRetainedPrimaryStaRsnStateGated("):
                 v2.index("postWclScanResultsGated(")]
for token in (
    "ic->ic_opmode != IEEE80211_M_STA",
    "ic->ic_state != IEEE80211_S_RUN",
    "ic->ic_bss == nullptr",
    "!ic->ic_bss->ni_port_valid",
    "ic->ic_bss->ni_rsnakms == IEEE80211_AKM_NONE",
    "handleKeyDone(true, false);",
):
    assert token in rsn_restore, f"missing retained RSN restore fence: {token}"
for forbidden in (
    "postMessage", "setLinkState", "setLinkStatus", "EAPOL",
    "IEEE80211_EVT_STA_RSN_HANDSHAKE_DONE", "postRsnHandshakeDoneGated",
):
    assert forbidden not in rsn_restore, \
        f"retained RSN restore must not republish authentication: {forbidden}"
assert "ic->ic_bss->ni_rsnakms == IEEE80211_AKM_NONE" in restore
rsn_gate = restore.index("IOCommandGate *gate = owner->getCommandGate();")
rsn_action = restore.index(
    "gate->runAction(AirportItlwm::restoreRetainedPrimaryStaRsnStateGated,",
    rsn_gate)
assert controller_up < rsn_gate < rsn_action
assert "if (gate != nullptr &&" in restore
assert "nullptr) == kIOReturnSuccess" in restore[rsn_action:]
assert "APSTA lower stop restored retained primary RSN state" in restore
assert "postRsnHandshakeDoneGated" not in restore

# The standard primary HostAP lifecycle can ask IWN's controller to drop its
# carrier although its AP-stop firmware terminal explicitly preserves the
# primary STA RXON. Do not let that transient edge enter WCL: WCL would start
# a duplicate cached SAE join while net80211 is still RUN. The owner predicate
# must remain narrow enough that an actual leave/loss (which has left RUN) is
# still delivered normally.
assert "bool shouldRetainPrimaryStaCarrier() const;" in owner_hpp
carrier_start = owner.index(
    "bool AirportItlwmAPSTAOwner::shouldRetainPrimaryStaCarrier() const")
carrier = owner[carrier_start:owner.index(
    "bool AirportItlwmAPSTAOwner::matchesBSDName", carrier_start)]
for token in (
    "(!isApRunning() && !lowerStopPending)",
    "ic->ic_opmode == IEEE80211_M_STA",
    "ic->ic_state == IEEE80211_S_RUN",
    "ic->ic_bss != nullptr",
    "ic->ic_bss->ni_port_valid",
):
    assert token in carrier, f"missing retained-carrier fence: {token}"
link_status = v2[v2.index("bool AirportItlwm::\nsetLinkStatus("):
                 v2.index("IOReturn AirportItlwm::\nsetLinkStateGated(")]
preserve_marker = link_status.index(
    "APSTA preserving retained primary STA controller carrier")
preserve_start = link_status.rfind("#if __IO80211_TARGET", 0, preserve_marker)
preserve = link_status[preserve_start:link_status.index(
    "// Base status handling may itself consult", preserve_marker)]
for token in (
    "(status & kIONetworkLinkActive) == 0",
    "(status & kIONetworkLinkNoNetworkChange) == 0",
    "fAPSTAOwner != nullptr",
    "fAPSTAOwner->shouldRetainPrimaryStaCarrier()",
    "return true;",
):
    assert token in preserve, f"missing APSTA transient-carrier guard: {token}"

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
