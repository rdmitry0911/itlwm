#!/usr/bin/env bash
# Static + pure-unit regression gate for the exact IWN/IWM/IWX-owned Tahoe
# WCL scan layer.
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

bash "$root/scripts/test_payload_builders.sh"

python3 - "$root" <<'PY'
from pathlib import Path
import sys

root = Path(sys.argv[1])
sky = (root / "AirportItlwm/AirportItlwmSkywalkInterface.cpp").read_text()
v2 = (root / "AirportItlwm/AirportItlwmV2.cpp").read_text()
v2_hpp = (root / "AirportItlwm/AirportItlwmV2.hpp").read_text()
contracts = (root / "AirportItlwm/TahoeWclPhysicalScanContracts.hpp").read_text()
driver_controller = (root / "include/HAL/ItlDriverController.hpp").read_text()
hal_service = (root / "include/HAL/ItlHalService.hpp").read_text()
i80211_var = (root / "itl80211/openbsd/net80211/ieee80211_var.h").read_text()
i80211_node = (root / "itl80211/openbsd/net80211/ieee80211_node.c").read_text()
i80211_node_h = (root / "itl80211/openbsd/net80211/ieee80211_node.h").read_text()
i80211_proto = (root / "itl80211/openbsd/net80211/ieee80211_proto.h").read_text()
i80211 = (root / "itl80211/openbsd/net80211/ieee80211.c").read_text()
iwn = (root / "itlwm/hal_iwn/ItlIwn.cpp").read_text()
iwm = (root / "itlwm/hal_iwm/ItlIwm.cpp").read_text()
iwm_hpp = (root / "itlwm/hal_iwm/ItlIwm.hpp").read_text()
iwm_mac = (root / "itlwm/hal_iwm/mac80211.cpp").read_text()
iwm_scan = (root / "itlwm/hal_iwm/scan.cpp").read_text()
iwx = (root / "itlwm/hal_iwx/ItlIwx.cpp").read_text()
iwx_hpp = (root / "itlwm/hal_iwx/ItlIwx.hpp").read_text()
iwn_var = (root / "itlwm/hal_iwn/if_iwnvar.h").read_text()
iwm_var = (root / "itlwm/hal_iwm/if_iwmvar.h").read_text()
iwx_var = (root / "itlwm/hal_iwx/if_iwxvar.h").read_text()


def fail(message):
    raise SystemExit(f"Tahoe WCL physical-scan lifecycle: {message}")


def require(text, needle, label):
    if needle not in text:
        fail(f"missing {label}: {needle}")


def forbid(text, needle, label):
    if needle in text:
        fail(f"unexpected {label}: {needle}")


def body(text, marker, label):
    start = text.find(marker)
    if start < 0:
        fail(f"missing {label}")
    opening = text.find("{", start)
    if opening < 0:
        fail(f"missing body for {label}")
    depth = 0
    for pos in range(opening, len(text)):
        if text[pos] == "{":
            depth += 1
        elif text[pos] == "}":
            depth -= 1
            if depth == 0:
                return text[opening + 1:pos]
    fail(f"unterminated {label}")


def ordered(text, label, *needles):
    cursor = 0
    for needle in needles:
        pos = text.find(needle, cursor)
        if pos < 0:
            fail(f"{label} missing ordered token: {needle}")
        cursor = pos + len(needle)


request = body(sky, "setWCL_SCAN_REQ(apple80211ScanRequest *req)",
               "setWCL_SCAN_REQ")
ordered(request, "WCL associated/initial admission",
        "if (ic->ic_state == IEEE80211_S_RUN)",
        "else if (ic->ic_state == IEEE80211_S_SCAN)",
        "initialForeground = true",
        "instance->reserveWclPhysicalScan(",
        "fHalService->beginWclInitialScan",
        "fHalService->beginWclBackgroundScan",
        "instance->activateWclPhysicalScan(generation, backendGeneration)")
for token in ("ic->ic_opmode != IEEE80211_M_STA",
              "IEEE80211_F_AUTO_JOIN", "ic->ic_des_esslen != 0",
              "ieee80211_sae_wcl_request_scan_selection_held(ic)",
              "ieee80211_sae_wcl_request_scan_selection_owned(ic)"):
    require(request, token, "trusted S_SCAN initial admission")
if request.count("(ic->ic_flags & IEEE80211_F_BGSCAN) != 0") != 1:
    fail("only associated WCL admission may reject a live background scan")
require(request, "AppleBCMWLANScanAdapter::startScan",
        "reference WCL admission rationale")
forbid(request, "IEEE80211_F_BGSCAN |\n                             IEEE80211_F_DESBSSID",
       "BSSID-only radio-reset recovery must not reject the fresh census")
forbid(request, "~IEEE80211_F_DESBSSID",
       "WCL initial admission must preserve a BSSID pin")
ordered(request, "queued initial handoff",
        "if (initialForeground && beginResult == kIOReturnSuccess",
        "backendGeneration == 0",
        "instance->queueWclInitialPhysicalScan(generation)",
        "StartDisposition::TerminalPending",
        "completePendingWclPhysicalScanTerminal")
require(request, "instance->failWclPhysicalScanStart(generation)",
        "failed physical-start reconciliation")
require(request, "completePendingWclPhysicalScanTerminal",
        "post-submit terminal reconciliation")
forbid(request, "scheduleScanSource", "timer completion in WCL request")
forbid(request, "ieee80211_begin_cache_bgscan", "coalescing cache helper in WCL request")
forbid(request, "if (fScanResultWrapping)", "iterator-as-busy admission")

abort = body(sky, "setWCL_SCAN_ABORT(void *data)", "setWCL_SCAN_ABORT")
ordered(abort, "real WCL abort",
        "instance->markWclPhysicalScanAborting(&generation)",
        "fHalService->abortWclBackgroundScan(generation)")
require(abort, "resumeWclPhysicalScanAfterAbortFailure(generation)",
        "failed backend abort returns the ticket to the live terminal")
forbid(abort, "cancelScanSource", "timer cancellation in WCL abort")
forbid(abort, "APPLE80211_M_SCAN_DONE", "synthetic terminal in WCL abort")
forbid(abort, "ItlDriverController", "generic controller WCL abort bridge")

event = body(v2, "eventHandler(struct ieee80211com *ic, int msgCode, void *data)",
             "eventHandler")
ordered(event, "post-doorbell WCL initial start",
        "IEEE80211_EVT_WCL_SCAN_STARTED",
        "activateWclPhysicalScan(started.generation",
        "StartDisposition::TerminalPending",
        "queueWclPhysicalScanTerminalPublication")
ordered(event, "no-doorbell WCL initial rejection",
        "IEEE80211_EVT_WCL_SCAN_START_REJECTED",
        "rejectWclInitialPhysicalScanStart(rejected.generation")
ordered(event, "tagged physical terminal claim",
        "IEEE80211_EVT_WCL_SCAN_TERMINAL",
        "claimWclPhysicalScanCompletion(terminal.generation",
        "queueWclPhysicalScanTerminalPublication")
require(event, "IEEE80211_EVT_WCL_SCAN_INVALIDATED",
        "hardware-reset invalidation fence")
require(event, "IEEE80211_EVT_WCL_SCAN_REOPENED",
        "confirmed radio-reset reopening fence")
ordered(event, "radio-ready reopens both scan admission planes",
        "IEEE80211_EVT_WCL_SCAN_REOPENED",
        "TAILQ_EMPTY(&ic->ic_ess)",
        "ieee80211_deselect_ess(ic)",
        "ic->ic_flags |= IEEE80211_F_AUTO_JOIN",
        "reopenWclPhysicalScanAfterRadioReset()",
        "reopenStandardPhysicalScanAfterRadioReset()",
        "noteRadioScanReadyAndQueuePowerOnAvailability()")
reopened_start = event.find("if (msgCode == IEEE80211_EVT_WCL_SCAN_REOPENED)")
reopened_end = event.find("if (msgCode == IEEE80211_EVT_STANDARD_SCAN_INVALIDATED)",
                          reopened_start)
reopened = event[reopened_start:reopened_end]
require(reopened, "TAILQ_EMPTY(&ic->ic_ess)",
        "empty-ESS wake scan policy fence")
require(reopened, "ieee80211_deselect_ess(ic)",
        "stale pre-power-cycle desired-ESS teardown")
require(reopened, "ic->ic_flags |= IEEE80211_F_AUTO_JOIN",
        "wake WCL initial-scan admission restore")
require(reopened,
        "noteRadioScanReadyAndQueuePowerOnAvailability()",
        "backend-ready PowerOn availability")
deselect = body(i80211_node, "ieee80211_deselect_ess(struct ieee80211com *ic)",
                "canonical desired-ESS deselect")
ordered(deselect, "wake desired-ESS and security teardown",
        "memset(ic->ic_des_essid, 0, IEEE80211_NWID_LEN)",
        "ic->ic_des_esslen = 0",
        "ieee80211_disable_wep(ic)",
        "ieee80211_disable_rsn(ic)")
ordered(event, "PowerOn backend-ready edge precedes generic scan terminal",
        "IEEE80211_EVT_WCL_SCAN_REOPENED",
        "noteRadioScanReadyAndQueuePowerOnAvailability()",
        "case IEEE80211_EVT_SCAN_DONE:",
        "noteDeferredWakePowerChangedEdge(",
        "gate->runAction(postMessageGated,")
scan_done = event[event.find("case IEEE80211_EVT_SCAN_DONE:"):]
forbid(scan_done[:scan_done.find("case IEEE80211_EVT_WCL_REASSOC_DONE:")],
       "claimWclPhysicalScanCompletion", "generic SCAN_DONE WCL claim")
forbid(scan_done, "noteRadioScanReadyAndQueuePowerOnAvailability()",
       "scan terminal must not own PowerOn availability")
require(scan_done, "noteDeferredWakePowerChangedEdge(",
        "scan terminal owns its deferred wake rendezvous edge")

publisher = body(v2, "postWclPhysicalScanCompletionGated(",
                 "physical WCL completion publisher")
ordered(publisher, "WCL terminal publisher",
        "lifecycle.terminalQueued",
        "APPLE80211_M_WCL_SCAN_RESULT",
        "APPLE80211_M_WCL_SCAN_DONE",
        "finishWclPhysicalScanCompletion(generation, backendGeneration)")
require(publisher, "releaseWclPhysicalScanLifecycleUser",
        "terminal snapshot lifetime release")
forbid(publisher, "ieee80211_iterate_nodes",
       "late net80211 walk in physical WCL publisher")

scan_publisher = body(v2, "postWclScanResultsGated(",
                      "legacy WCL result publisher")
ordered(scan_publisher, "WCL publishes a locked value snapshot",
        "IOMalloc(snapshotBytes)",
        "ieee80211_iterate_nodes(ic, collectTahoeWclScanResultSnapshot",
        "IOFree(snapshots, snapshotBytes)",
        "APPLE80211_M_WCL_SCAN_DONE")
forbid(scan_publisher, "RB_FOREACH",
       "unlocked net80211 tree traversal in WCL publisher")
require(v2, "collectTahoeWclScanResultSnapshot",
        "splnet-bounded WCL value snapshot callback")
snapshot_terminal = body(v2, "/* The tagged lower terminal",
                         "tagged physical terminal snapshot")
ordered(snapshot_terminal, "terminal snapshot before async publication",
        "ieee80211_iterate_nodes(collector.ic,",
        "releaseWclPhysicalScanLifecycleUser")
forbid(snapshot_terminal, "IOMalloc", "allocation in terminal RX path")
require(v2, "allocateWclPhysicalScanSnapshot",
        "pre-submit bounded WCL result allocation")
require(v2, "setupWclPhysicalScanTerminalSource",
        "off-gate physical terminal source setup")
require(v2, "teardownWclPhysicalScanTerminalSource",
        "physical terminal source teardown")
start_begin = v2.find("bool AirportItlwm::start(IOService *provider)")
start_end = v2.find("void AirportItlwm::stop(IOService *provider)", start_begin)
if start_begin < 0 or start_end < 0:
    fail("missing controller start region")
start_region = v2[start_begin:start_end]
ordered(start_region, "Skywalk-first WCL terminal-source setup",
        "fNetIf->deferBSDAttach(false);",
        "setupWclPhysicalScanTerminalSource(this, _fWorkloop)",
        "markLifecycleLive()")

pending_terminal = body(sky, "completePendingWclPhysicalScanTerminal(",
                        "pending physical WCL terminal reconciler")
ordered(pending_terminal, "pending terminal exact cleanup",
        "pendingWclPhysicalScanCompletion(",
        "queueWclPhysicalScanTerminalPublication",
        "finishWclPhysicalScanCompletion(")
require(contracts, "pendingCompletionForGeneration",
        "post-submit terminal backend recovery")

fake = body(v2, "void AirportItlwm::fakeScanDone", "fakeScanDone")
require(fake, "postMessageGated", "legacy generic scan completion")
require(fake, "APPLE80211_M_SCAN_DONE", "legacy generic scan bulletin")
forbid(fake, "postWclScanResultsGated", "fake WCL result publication")

for token in (
        "enum class Phase", "Queued", "Draining", "CompletionDisposition",
        "beginDraining", "reopenAfterRadioReset", "resumeAfterAbortFailure",
        "claimCompletion", "activeBackendGeneration",
        "terminalBackendGeneration", "pendingCompletion", "invalidate",
        "pendingCompletionForGeneration", "queueInitialStart",
        "rejectInitialStart", "Pending"):
    require(contracts, token, "ticket reducer")
require(v2_hpp, "AirportItlwmWclPhysicalScanLifecycle",
        "per-controller WCL ticket lifecycle")
require(v2_hpp, "fWclPhysicalScanLifecycle",
        "per-controller WCL ticket storage")
for token in ("snapshotInProgress", "snapshotReady",
              "terminalPublicationRequested"):
    require(v2_hpp, token, "early-terminal snapshot fence")
claim = body(v2, "AirportItlwm::claimWclPhysicalScanCompletion(",
             "physical terminal claim")
ordered(claim, "terminal claim takes snapshot ownership before unlock",
        "TahoeWclPhysicalScanContracts::claimCompletion",
        "lifecycle.snapshotInProgress = true",
        "++lifecycle.users",
        "IOSimpleLockUnlockEnableInterrupt")
queue = body(v2, "AirportItlwm::queueWclPhysicalScanTerminalPublication(",
             "physical terminal queue")
ordered(queue, "early terminal queue waits for snapshot",
        "lifecycle.snapshotInProgress || lifecycle.snapshotReady",
        "lifecycle.terminalPublicationRequested = true",
        "armWclPhysicalScanTerminalDoorbellLocked")
snapshot_ready = body(v2, "/* The tagged lower terminal",
                      "terminal snapshot readiness")
ordered(snapshot_ready, "snapshot completion releases deferred terminal",
        "state.snapshotInProgress = false",
        "state.snapshotReady = true",
        "armWclPhysicalScanTerminalDoorbellLocked")
require(v2, "invalidateWclPhysicalScan();",
        "teardown/power invalidation")
enable_adapter = body(v2, "IOReturn AirportItlwm::enableAdapter",
                      "controller enableAdapter")
forbid(enable_adapter, "reopenWclPhysicalScanAfterRadioReset",
       "optimistic WCL reopen before lower radio-ready event")

forbid(driver_controller, "Wcl", "WCL ownership on generic driver controller")
for token in ("beginWclBackgroundScan", "beginWclInitialScan",
              "abortWclBackgroundScan", "invalidateWclBackgroundScan",
              "kIOReturnUnsupported"):
    require(hal_service, token, "fail-closed HAL WCL boundary")

for token in (
        "ItlIwxWclScanPhase",
        "InitialQueued",
        "InitialStarting",
        "InitialActive",
        "BackgroundStarting",
        "BackgroundActive",
        "beginWclInitialScan(",
        "beginWclBackgroundScan(",
        "abortWclBackgroundScan(",
        "invalidateWclBackgroundScan() override",
        "claimWclScanTerminal(",
        "publishWclScanTerminal(",
        "invalidateWclScanForReset()",
        "IOSimpleLock *wclScanLock",
        "wclScanBackendGeneration",
        "wclScanPublicationInvalidated",
        "wclScanNeedsReopen",
):
    require(iwx_hpp, token, "IWX exact WCL owner declaration")

iwx_initial = body(
    iwx,
    "beginWclInitialScan(uint64_t generation, uint32_t *outBackendGeneration)",
    "IWX beginWclInitialScan")
for token in (
        "ic->ic_event_handler == NULL",
        "ic->ic_state != IEEE80211_S_SCAN",
        "ic->ic_opmode != IEEE80211_M_STA",
        "(ic->ic_if.if_flags & IFF_RUNNING) == 0",
        "ic->ic_mgt_timer != 0",
        "ic->ic_des_esslen != 0",
):
    require(iwx_initial, token, "IWX initial admission fence")
forbid(iwx_initial, "(ic->ic_flags & IEEE80211_F_BGSCAN) != 0",
       "IWX WCL initial admission must use its exact physical owner")
require(iwx_initial, "AppleBCMWLANScanAdapter",
        "IWX reference WCL admission rationale")
ordered(iwx_initial, "IWX fresh initial physical scan",
        "wclScanUpperGeneration = generation",
        "if ((com.sc_flags & IWX_FLAG_SCANNING) != 0)",
        "wclScanPhase = ItlIwxWclScanPhase::InitialQueued",
        "wclScanPhase = ItlIwxWclScanPhase::InitialStarting",
        "ieee80211_begin_scan(&ic->ic_if)")
require(iwx_initial, "*outBackendGeneration = 0;",
        "IWX initial STARTED-only backend publication")

iwx_background = body(
    iwx,
    "beginWclBackgroundScan(uint64_t generation,",
    "IWX beginWclBackgroundScan")
ordered(iwx_background, "IWX background physical scan",
        "wclScanPhase = ItlIwxWclScanPhase::BackgroundStarting",
        "ieee80211_begin_cache_bgscan(&ic->ic_if)",
        "wclScanPhase == ItlIwxWclScanPhase::BackgroundActive",
        "*outBackendGeneration = wclScanBackendGeneration")

iwx_initial_started = body(
    iwx, "noteWclInitialScanCommandStarted()",
    "IWX initial post-submit owner")
ordered(iwx_initial_started, "IWX initial backend generation",
        "wclScanPhase == ItlIwxWclScanPhase::InitialStarting",
        "iwx_wcl_scan_next_backend_generation_locked(this)",
        "wclScanPhase = ItlIwxWclScanPhase::InitialActive",
        "iwx_wcl_scan_publish_started")

iwx_radio_ready = body(
    iwx, "noteWclScanRadioReady()", "IWX radio-ready fence")
ordered(iwx_radio_ready, "IWX reset reopening",
        "if (wclScanNeedsReopen)",
        "wclScanNeedsReopen = false",
        "IEEE80211_EVT_WCL_SCAN_REOPENED")

iwx_claim = body(
    iwx, "claimWclScanTerminal(ItlIwxWclScanTerminal *terminal)",
    "IWX terminal claim")
ordered(iwx_claim, "IWX initial handoff",
        "wclScanPhase == ItlIwxWclScanPhase::InitialQueued",
        "wclScanPhase = ItlIwxWclScanPhase::InitialStarting",
        "ItlIwxWclScanTerminalKind::ReplayInitial")
for token in (
        "ItlIwxWclScanPhase::InitialActive",
        "ItlIwxWclScanTerminalKind::Foreground",
        "ItlIwxWclScanPhase::BackgroundActive",
        "ItlIwxWclScanTerminalKind::Background",
        "terminal->upperGeneration = wclScanUpperGeneration",
        "terminal->backendGeneration = wclScanBackendGeneration",
        "terminal->publish = !wclScanPublicationInvalidated",
):
    require(iwx_claim, token, "IWX exact terminal ticket")

iwx_foreground_submit = body(
    iwx, "iwx_scan(struct iwx_softc *sc)",
    "IWX foreground command submit")
ordered(iwx_foreground_submit, "IWX post-submit readiness edge",
        "sc->sc_flags |= IWX_FLAG_SCANNING",
        "noteWclInitialScanCommandStarted()",
        "ic->ic_state = IEEE80211_S_SCAN",
        "noteWclScanRadioReady()",
        "wakeupOn(&ic->ic_state)")
require(iwx_foreground_submit, "noteWclInitialScanCommandRejected()",
        "IWX pre-submit initial rejection")

iwx_background_submit = body(
    iwx, "iwx_bgscan(struct ieee80211com *ic)",
    "IWX background command submit")
ordered(iwx_background_submit, "IWX background activation",
        "sc->sc_flags |= IWX_FLAG_BGSCAN",
        "that->noteWclBackgroundScanCommandStarted()")

iwx_terminal = body(
    iwx, "iwx_endscan(struct iwx_softc *sc)",
    "IWX firmware scan terminal")
ordered(iwx_terminal, "IWX queued initial handoff",
        "that->claimWclScanTerminal(&terminal)",
        "ItlIwxWclScanTerminalKind::ReplayInitial",
        "IEEE80211_SCAN_COMPLETION_WCL_HANDOFF",
        "ieee80211_begin_scan(&ic->ic_if)")
ordered(iwx_terminal, "IWX exact foreground terminal",
        "ItlIwxWclScanTerminalKind::Foreground",
        "IEEE80211_SCAN_COMPLETION_WCL_FOREGROUND")
ordered(iwx_terminal, "IWX tagged terminal publication",
        "ItlIwxWclScanTerminalKind::Background",
        "ic_wcl_scan_suppress_scan_done_once",
        "that->publishWclScanTerminal(",
        "IEEE80211_WCL_SCAN_TERMINAL_STATUS_COMPLETE")

iwx_abort = body(
    iwx, "abortWclBackgroundScan(uint64_t generation)",
    "IWX WCL abort")
ordered(iwx_abort, "IWX physical abort terminal",
        "iwx_scan_abort(&com)",
        "IEEE80211_SCAN_COMPLETION_WCL_FOREGROUND",
        "ic_wcl_scan_suppress_scan_done_once",
        "IEEE80211_WCL_SCAN_TERMINAL_STATUS_ABORTED")

iwx_reset = body(
    iwx, "void ItlIwx::\ninvalidateWclScanForReset()",
    "IWX reset invalidation")
for token in (
        "iwx_wcl_scan_publish_start_rejected",
        "IEEE80211_EVT_WCL_SCAN_INVALIDATED",
        "wclScanNeedsReopen = true",
):
    require(iwx_reset, token, "IWX reset closure")
for token in (
        "IEEE80211_EVT_WCL_SCAN_STARTED",
        "IEEE80211_EVT_WCL_SCAN_START_REJECTED",
        "IEEE80211_EVT_WCL_SCAN_TERMINAL",
):
    require(iwx, token, "IWX exact upper event mapping")
iwx_stop = body(iwx, "iwx_stop_internal(struct _ifnet *ifp,",
                "IWX hardware stop")
require(iwx_stop, "that->invalidateWclScanForReset();",
        "IWX stop reset invalidation")
for token in ("IWX_FLAG_SCANNING", "IWX_FLAG_BGSCAN",
              "IWX_SCAN_COMPLETE_UMAC",
              "IWX_SCAN_ITERATION_COMPLETE_UMAC"):
    require(iwx, token, "IWX physical firmware scan owner")

for token in (
        "ItlIwmWclScanPhase",
        "InitialQueued",
        "InitialStarting",
        "InitialActive",
        "BackgroundStarting",
        "BackgroundActive",
        "beginWclInitialScan(",
        "beginWclBackgroundScan(",
        "abortWclBackgroundScan(",
        "invalidateWclBackgroundScan() override",
        "claimWclScanTerminal(",
        "publishWclScanTerminal(",
        "invalidateWclScanForReset()",
        "IOSimpleLock *wclScanLock",
        "wclScanBackendGeneration",
        "wclScanPublicationInvalidated",
        "wclScanNeedsReopen",
):
    require(iwm_hpp, token, "IWM exact WCL owner declaration")

iwm_initial = body(
    iwm,
    "beginWclInitialScan(uint64_t generation, uint32_t *outBackendGeneration)",
    "IWM beginWclInitialScan")
for token in (
        "ic->ic_state != IEEE80211_S_SCAN",
        "ic->ic_opmode != IEEE80211_M_STA",
        "(ic->ic_if.if_flags & IFF_RUNNING) == 0",
        "ic->ic_mgt_timer != 0",
        "ic->ic_des_esslen != 0",
):
    require(iwm_initial, token, "IWM initial admission fence")
forbid(iwm_initial, "(ic->ic_flags & IEEE80211_F_BGSCAN) != 0",
       "IWM WCL admission must use its exact physical owner")
require(iwm_initial, "AppleBCMWLANScanAdapter",
        "IWM reference WCL admission rationale")
ordered(iwm_initial, "IWM fresh initial physical scan",
        "wclScanUpperGeneration = generation",
        "if ((com.sc_flags & IWM_FLAG_SCANNING) != 0)",
        "wclScanPhase = ItlIwmWclScanPhase::InitialQueued",
        "wclScanPhase = ItlIwmWclScanPhase::InitialStarting",
        "ieee80211_begin_scan(&ic->ic_if)")
require(iwm_initial, "*outBackendGeneration = 0;",
        "IWM initial STARTED-only backend publication")

iwm_background = body(
    iwm,
    "beginWclBackgroundScan(uint64_t generation,",
    "IWM beginWclBackgroundScan")
ordered(iwm_background, "IWM background physical scan",
        "wclScanPhase = ItlIwmWclScanPhase::BackgroundStarting",
        "ieee80211_begin_cache_bgscan(&ic->ic_if)",
        "wclScanPhase == ItlIwmWclScanPhase::BackgroundActive",
        "*outBackendGeneration = wclScanBackendGeneration")

iwm_initial_started = body(
    iwm, "noteWclInitialScanCommandStarted()",
    "IWM initial post-submit owner")
ordered(iwm_initial_started, "IWM initial backend generation",
        "wclScanPhase == ItlIwmWclScanPhase::InitialStarting",
        "iwm_wcl_scan_next_backend_generation_locked(this)",
        "wclScanPhase = ItlIwmWclScanPhase::InitialActive",
        "iwm_wcl_scan_publish_started")

iwm_radio_ready = body(
    iwm, "noteWclScanRadioReady()", "IWM radio-ready fence")
ordered(iwm_radio_ready, "IWM reset reopening",
        "if (wclScanNeedsReopen)",
        "wclScanNeedsReopen = false",
        "IEEE80211_EVT_WCL_SCAN_REOPENED")

iwm_claim = body(
    iwm, "claimWclScanTerminal(ItlIwmWclScanTerminal *terminal)",
    "IWM terminal claim")
ordered(iwm_claim, "IWM initial handoff",
        "wclScanPhase == ItlIwmWclScanPhase::InitialQueued",
        "wclScanPhase = ItlIwmWclScanPhase::InitialStarting",
        "ItlIwmWclScanTerminalKind::ReplayInitial")
for token in (
        "ItlIwmWclScanPhase::InitialActive",
        "ItlIwmWclScanTerminalKind::Foreground",
        "ItlIwmWclScanPhase::BackgroundActive",
        "ItlIwmWclScanTerminalKind::Background",
        "terminal->upperGeneration = wclScanUpperGeneration",
        "terminal->backendGeneration = wclScanBackendGeneration",
        "terminal->publish = !wclScanPublicationInvalidated",
):
    require(iwm_claim, token, "IWM exact terminal ticket")

iwm_foreground_submit = body(
    iwm_scan, "iwm_scan(struct iwm_softc *sc)",
    "IWM foreground command submit")
ordered(iwm_foreground_submit, "IWM post-submit readiness edge",
        "sc->sc_flags |= IWM_FLAG_SCANNING",
        "noteWclInitialScanCommandStarted()",
        "ic->ic_state = IEEE80211_S_SCAN",
        "noteWclScanRadioReady()",
        "wakeupOn(&ic->ic_state)")
require(iwm_foreground_submit, "noteWclInitialScanCommandRejected()",
        "IWM pre-submit initial rejection")

iwm_background_submit = body(
    iwm_scan, "iwm_bgscan(struct ieee80211com *ic)",
    "IWM background command submit")
ordered(iwm_background_submit, "IWM background activation",
        "sc->sc_flags |= IWM_FLAG_BGSCAN",
        "that->noteWclBackgroundScanCommandStarted()")

iwm_terminal = body(
    iwm_mac, "iwm_endscan(struct iwm_softc *sc)",
    "IWM firmware scan terminal")
ordered(iwm_terminal, "IWM queued initial handoff",
        "that->claimWclScanTerminal(&terminal)",
        "ItlIwmWclScanTerminalKind::ReplayInitial",
        "IEEE80211_SCAN_COMPLETION_WCL_HANDOFF",
        "ieee80211_begin_scan(&ic->ic_if)")
ordered(iwm_terminal, "IWM exact foreground terminal",
        "ItlIwmWclScanTerminalKind::Foreground",
        "IEEE80211_SCAN_COMPLETION_WCL_FOREGROUND")
ordered(iwm_terminal, "IWM tagged terminal publication",
        "ItlIwmWclScanTerminalKind::Background",
        "ic_wcl_scan_suppress_scan_done_once",
        "that->publishWclScanTerminal(",
        "IEEE80211_WCL_SCAN_TERMINAL_STATUS_COMPLETE")

iwm_abort = body(
    iwm, "abortWclBackgroundScan(uint64_t generation)",
    "IWM WCL abort")
ordered(iwm_abort, "IWM physical abort terminal",
        "iwm_scan_abort(&com)",
        "IEEE80211_SCAN_COMPLETION_WCL_FOREGROUND",
        "ic_wcl_scan_suppress_scan_done_once",
        "IEEE80211_WCL_SCAN_TERMINAL_STATUS_ABORTED")

iwm_reset = body(
    iwm, "invalidateWclScanForReset()", "IWM reset invalidation")
for token in (
        "iwm_wcl_scan_publish_start_rejected",
        "IEEE80211_EVT_WCL_SCAN_INVALIDATED",
        "wclScanNeedsReopen = true",
):
    require(iwm_reset, token, "IWM reset closure")
for token in (
        "IEEE80211_EVT_WCL_SCAN_STARTED",
        "IEEE80211_EVT_WCL_SCAN_START_REJECTED",
        "IEEE80211_EVT_WCL_SCAN_TERMINAL",
):
    require(iwm, token, "IWM exact upper event mapping")
iwm_stop = body(iwm_mac, "iwm_stop(struct _ifnet *ifp)",
                "IWM hardware stop")
require(iwm_stop, "that->invalidateWclScanForReset();",
        "IWM stop reset invalidation")

for token in ("IWN_SCAN_LEASE_WCL_BACKGROUND", "IWN_SCAN_LEASE_WCL_INITIAL",
              "IWN_SCAN_LEASE_GENERIC_FOREGROUND",
              "IWN_SCAN_LEASE_GENERIC_BACKGROUND", "sc_scan_lease_lock",
              "terminal_claimed", "publication_invalidated",
              "wcl_initial_handoff_serial", "wcl_initial_started",
              "iwn_wcl_initial_scan_pending", "terminal_handoff_ready",
              "command_started", "sc_scan_lease_replay_task_admission_state"):
    require(iwn_var, token, "strict IWN physical scan lease")
for token in ("iwn_scan_lease_replay_task_admission_close",
              "iwn_scan_lease_replay_task_admission_drain",
              "iwn_scan_lease_schedule_replay_task"):
    require(iwn, token, "IWN replay task detach admission")
for token in ("beginWclBackgroundScan", "beginWclInitialScan",
              "abortWclBackgroundScan", "iwn_wcl_initial_scan_queue",
              "iwn_scan_lease_initial_handoff_valid_locked",
              "iwn_scan_lease_reserve", "iwn_scan_lease_claim_terminal",
              "iwn_scan_lease_finish_terminal", "iwn_scan_lease_replay_task",
              "iwn_scan_lease_finish_doorbell",
              "iwn_scan_lease_begin_hardware_invalidation",
              "IEEE80211_EVT_WCL_SCAN_STARTED",
              "IEEE80211_EVT_WCL_SCAN_START_REJECTED"):
    require(iwn, token, "IWN exact WCL owner")

initial_begin = body(iwn, "beginWclInitialScan(uint64_t generation, uint32_t *outBackendGeneration)",
                     "IWN beginWclInitialScan")
ordered(initial_begin, "IWN initial WCL admission",
        "ic->ic_state != IEEE80211_S_SCAN",
        "iwn_wcl_initial_scan_queue(&com, generation, &queued)",
        "if (queued)",
        "IWN_SCAN_LEASE_WCL_INITIAL")
require(initial_begin, "*outBackendGeneration = 0;",
        "initial STARTED-only backend publication")
for token in ("ic->ic_des_esslen != 0",
              "ieee80211_sae_wcl_request_scan_selection_held(ic)",
              "ieee80211_sae_wcl_request_scan_selection_owned(ic)"):
    require(initial_begin, token, "IWN BSSID-only recovery admission fence")
forbid(initial_begin, "(ic->ic_flags & IEEE80211_F_BGSCAN) != 0",
       "IWN initial admission must use the exact lower scan lease")
forbid(initial_begin, "IEEE80211_F_BGSCAN |\n                         IEEE80211_F_DESBSSID",
       "IWN BSSID-only recovery must reach the WCL initial queue")
forbid(initial_begin, "~IEEE80211_F_DESBSSID",
       "IWN initial recovery must preserve a BSSID pin")

initial_queue = body(iwn, "iwn_wcl_initial_scan_queue(",
                     "IWN initial handoff queue")
ordered(initial_queue, "initial handoff attaches only to boot scan",
        "IWN_SCAN_LEASE_GENERIC_FOREGROUND",
        "sc->sc_scan_lease.command_submitted",
        "sc->sc_wcl_initial_scan_pending.queued = true")

handoff_valid = body(iwn, "iwn_scan_lease_initial_handoff_valid_locked(",
                     "IWN initial handoff token validator")
for token in ("wcl_initial_handoff_serial", "pending->queued",
              "pending->launching", "pending->terminal_handoff_ready",
              "pending->upper_generation", "pending->generic_serial"):
    require(handoff_valid, token, "initial handoff cancellation token")

reserve = body(iwn, "iwn_scan_lease_reserve(", "IWN scan lease reserve")
for token in ("required_initial_handoff_serial", "exact_initial_pending",
              "initial_pending && !exact_initial_pending",
              "sc->sc_scan_lease.wcl_initial_handoff_serial"):
    require(reserve, token, "IWN exact initial handoff reserve fence")

doorbell = body(iwn, "iwn_scan_lease_finish_doorbell(",
                "IWN scan post-WRPTR hook")
ordered(doorbell, "initial start is published only after WRPTR",
        "sc->sc_scan_lease.wcl_initial_handoff_serial = 0",
        "sc->sc_scan_lease.wcl_initial_started = true",
        "sc->sc_wcl_initial_scan_pending.command_started = true",
        "IEEE80211_EVT_WCL_SCAN_STARTED")

replay_task = body(iwn, "iwn_scan_lease_replay_task(void *arg)",
                   "IWN scan replay task")
ordered(replay_task, "initial replay consumes exact handoff",
        "initial_handoff_serial =",
        "sc->sc_wcl_initial_scan_pending.launching = true",
        "IWN_SCAN_LEASE_WCL_INITIAL",
        "initial_handoff_serial",
        "reject_initial = error != 0 && !command_started",
        "IEEE80211_EVT_WCL_SCAN_START_REJECTED")

invalidation = body(iwn, "static enum iwn_scan_lease_owner\niwn_scan_lease_begin_hardware_invalidation(",
                    "IWN scan hardware invalidation")
ordered(invalidation, "started initial reset fence",
        "const bool started_initial",
        "sc->sc_scan_lease.wcl_initial_started",
        "const bool pending_started",
        "sc->sc_wcl_initial_scan_pending.command_started",
        "*queued_initial_rejected_generation")
require(invalidation, "if (!pending_started)",
        "no-doorbell initial rejection on reset")

hw_stop = body(iwn, "iwn_hw_stop(struct iwn_softc *sc)", "IWN hardware stop")
ordered(hw_stop, "hardware reset publishes exactly fenced initial outcome",
        "iwn_scan_lease_begin_hardware_invalidation",
        "iwn_scan_lease_retire_after_hardware_stop",
        "IEEE80211_EVT_WCL_SCAN_INVALIDATED",
        "IEEE80211_EVT_WCL_SCAN_START_REJECTED")

iwn_start = body(iwn, "iwn_scan_start(struct iwn_softc *sc, uint16_t flags, int bgscan,",
                 "IWN controller scan start")
ordered(iwn_start, "WCL foreground scan is an S_SCAN operation",
        "bool wcl_foreground = owner == IWN_SCAN_LEASE_WCL_INITIAL",
        "ic->ic_state != IEEE80211_S_SCAN",
        "iwn_scan_submit(sc, flags, bgscan, serial",
        "controller_foreground, wcl_foreground")
for token in ("ic->ic_des_esslen != 0",
              "ieee80211_sae_wcl_request_scan_selection_held(ic)",
              "ieee80211_sae_wcl_request_scan_selection_owned(ic)"):
    require(iwn_start, token, "IWN submit BSSID-only recovery admission fence")
forbid(iwn_start, "(ic->ic_flags & IEEE80211_F_BGSCAN) != 0",
       "IWN WCL submit must use the exact lower scan lease")
forbid(iwn_start, "IEEE80211_F_BGSCAN |\n                          IEEE80211_F_DESBSSID",
       "IWN submit BSSID-only recovery must remain reachable")
forbid(iwn_start, "~IEEE80211_F_DESBSSID",
       "IWN submit recovery must preserve a BSSID pin")
iwn_submit = body(iwn, "iwn_scan_submit(struct iwn_softc *sc, uint16_t flags, int bgscan,",
                  "IWN controller scan submit")
ordered(iwn_submit, "foreground initial scan starts from a fresh census",
        "if (publish_wcl_initial_started)",
        "ieee80211_free_allnodes(ic, 1 /* fresh initial census */)",
        "iwn_prepare_controller_foreground_scan(ic)",
        "iwn_cmd_with_doorbell_hook")
stop_scan = body(iwn, "case IWN_STOP_SCAN:", "IWN STOP_SCAN")
ordered(stop_scan, "IWN initial handoff and terminal ownership",
        "iwn_scan_continue", "iwn_scan_lease_claim_terminal",
        "iwn_wcl_initial_scan_claim_generic_terminal",
        "IEEE80211_SCAN_COMPLETION_WCL_HANDOFF",
        "IEEE80211_SCAN_COMPLETION_WCL_FOREGROUND",
        "IEEE80211_EVT_WCL_SCAN_TERMINAL",
        "iwn_scan_lease_finish_terminal")
require(stop_scan, "ieee80211_end_scan_controlled(ifp,",
        "controlled non-generic scan terminal")
require(iwn, "ic_wcl_scan_suppress_scan_done_once",
        "IWN one-shot generic completion suppression")
require(iwn, "IEEE80211_EVT_WCL_SCAN_INVALIDATED",
        "IWN reset invalidation event")
require(iwn, "IEEE80211_EVT_WCL_SCAN_REOPENED",
        "IWN confirmed radio reset event")
iwn_init = body(iwn, "int ItlIwn::\niwn_init(struct _ifnet *ifp)",
                "IWN init")
ordered(iwn_init, "IWN lower-ready fence after first scan state",
        "ifp->if_flags |= IFF_RUNNING",
        "error = ieee80211_begin_scan_with_result(ifp)",
        "if (error != 0)",
        "goto fail;",
        "IEEE80211_EVT_WCL_SCAN_REOPENED")

for token in ("availabilityEpoch", "pendingPowerOnEpoch",
              "readyPowerOnEpoch", "powerOnPublishQueued",
              "powerOnWakeBulletinPending",
              "powerOnWakeScanTerminalObserved",
              "powerOnWakeAvailabilityAckObserved",
              "powerOnWakePublishQueued"):
    require(v2_hpp, token, "post-radio-ready availability epoch state")
require(v2_hpp, "kAirportItlwmPmDriverAvailabilityPendingBit",
        "deferred PowerOn lifecycle bit")
availability_publish = body(v2,
    "publishDeferredPowerAvailabilityGated(OSObject *target, void *arg0,",
    "serialized deferred availability publisher")
ordered(availability_publish, "deferred PowerOn epoch claim",
        "kAirportItlwmDeferredPowerAvailabilityPublishOn",
        "lifecycle.availabilityEpoch == expectedEpoch",
        "lifecycle.pendingPowerOnEpoch == expectedEpoch",
        "lifecycle.readyPowerOnEpoch == expectedEpoch",
        "lifecycle.powerOnPublishQueued",
        "postTahoeDriverAvailabilityTransition(")
ordered(availability_publish, "serialized PowerOff invalidates then publishes",
        "kAirportItlwmDeferredPowerAvailabilityPublishOff",
        "cancelDeferredPowerOnAvailabilityRaw()",
        "Transition::PowerOff")
require(availability_publish,
        "kAirportItlwmDeferredPowerAvailabilityCancel",
        "serialized cancellation action")
require(availability_publish,
        "kAirportItlwmDeferredPowerAvailabilityCancelEpoch",
        "generation-bound cancellation action")
ordered(availability_publish,
        "PowerOn availability leaves the later wake bulletin pending",
        "Transition::PowerOn",
        "gate->commandWakeup(waitEvent, /*oneThread=*/false)")
forbid(availability_publish[
           availability_publish.find(
               "kAirportItlwmDeferredPowerAvailabilityPublishOn"):],
       "publishWakeBulletin",
       "combined availability and wake publication")
ordered(availability_publish,
        "scan-terminal wake bulletin is epoch-bound and serialized",
        "kAirportItlwmDeferredPowerAvailabilityPublishWakePowerChanged",
        "lifecycle.availabilityEpoch == expectedEpoch",
        "lifecycle.pendingPowerOnEpoch == 0",
        "lifecycle.powerOnWakeBulletinPending",
        "lifecycle.powerOnWakeScanTerminalObserved",
        "lifecycle.powerOnWakeAvailabilityAckObserved",
        "lifecycle.powerOnWakePublishQueued",
        "APPLE80211_M_POWER_CHANGED")
availability_arm = body(v2,
                       "armDeferredPowerOnAvailability(bool wakeBulletinPending)",
                       "deferred PowerOn arm")
ordered(availability_arm, "pending availability bit armed under lock",
        "lifecycle.powerOnPublishQueued = false",
        "lifecycle.powerOnWakeBulletinPending = wakeBulletinPending",
        "lifecycle.powerOnWakeScanTerminalObserved = false",
        "lifecycle.powerOnWakeAvailabilityAckObserved = false",
        "lifecycle.powerOnWakePublishQueued = false",
        "OSBitOrAtomic(kAirportItlwmPmDriverAvailabilityPendingBit",
        "IOSimpleLockUnlockEnableInterrupt(lock, irq)")
availability_cancel = body(v2,
    "void AirportItlwm::cancelDeferredPowerOnAvailabilityRaw()",
    "deferred PowerOn invalidation")
ordered(availability_cancel, "pending availability bit cleared under lock",
        "lifecycle.powerOnPublishQueued = false",
        "lifecycle.powerOnWakeBulletinPending = false",
        "lifecycle.powerOnWakeScanTerminalObserved = false",
        "lifecycle.powerOnWakeAvailabilityAckObserved = false",
        "lifecycle.powerOnWakePublishQueued = false",
        "kAirportItlwmPmDriverAvailabilityPendingBit",
        "IOSimpleLockUnlockEnableInterrupt(lock, irq)")
availability_wait = body(v2,
    "waitForDeferredPowerOnAvailability(uint64_t expectedEpoch,",
    "synchronous lower-ready wait")
ordered(availability_wait, "generation-bound PowerOn wait",
        "clock_interval_to_deadline(timeoutMs, kMillisecondScale",
        "lifecycle.availabilityEpoch == expectedEpoch",
        "lifecycle.pendingPowerOnEpoch == 0",
        "kAirportItlwmPmDriverAvailabilityPendingBit",
        "gate->commandSleep(",
        "&lifecycle.availabilityEpoch, deadline, THREAD_ABORTSAFE)")
require(availability_wait, "sleepResult == THREAD_TIMED_OUT",
        "bounded lower-ready wait")
availability_note = body(v2,
    "bool AirportItlwm::noteRadioScanReadyAndQueuePowerOnAvailability()",
    "radio-ready PowerOn note")
ordered(availability_note, "radio-ready notification is gated",
        "lifecycle.readyPowerOnEpoch = lifecycle.pendingPowerOnEpoch",
        "lifecycle.powerOnPublishQueued = true",
        "gate->runAction(publishDeferredPowerAvailabilityGated",
        "kAirportItlwmDeferredPowerAvailabilityPublishOn",
        "return true;")
require(availability_note, "return false;",
        "ordinary reopened events do not republish availability")
forbid(availability_note, "postTahoeDriverAvailabilityTransition",
       "off-gate PowerOn publication")
wake_note = body(v2,
    "noteDeferredWakePowerChangedEdge(bool scanTerminalEdge)",
    "wake bulletin rendezvous note")
ordered(wake_note, "wake bulletin waits for both reference-derived edges",
        "lifecycle.powerOnWakeBulletinPending",
        "lifecycle.powerOnWakeScanTerminalObserved = true",
        "lifecycle.powerOnWakeAvailabilityAckObserved = true",
        "lifecycle.powerOnWakeScanTerminalObserved &&",
        "lifecycle.powerOnWakeAvailabilityAckObserved &&",
        "lifecycle.pendingPowerOnEpoch == 0",
        "lifecycle.readyPowerOnEpoch == 0",
        "lifecycle.powerOnWakePublishQueued = true",
        "source->interruptOccurred(0, 0, 0)",
        "releaseWclPhysicalScanLifecycleUser(lifecycle, lock)")
forbid(wake_note, "postMessage(",
       "off-gate POWER_CHANGED publication")
forbid(wake_note, "publishDeferredPowerAvailabilityGated(",
       "recursive WCL setter POWER_CHANGED publication")

wake_dispatch = body(v2,
    "dispatchDeferredWakePowerChanged(uint64_t expectedEpoch)",
    "deferred workloop wake bulletin dispatch")
ordered(wake_dispatch, "workloop wake bulletin dispatch",
        "workLoop->inGate()",
        "kAirportItlwmDeferredPowerAvailabilityPublishWakePowerChanged",
        "result == kIOReturnSuccess",
        "lifecycle.powerOnWakePublishQueued = false")

source_action = body(v2,
    "wclPhysicalScanTerminalInterruptAction(",
    "shared WCL workloop source action")
ordered(source_action, "workloop source observes and dispatches wake bulletin",
        "state.powerOnWakePublishQueued",
        "wakePowerChangedEpoch = state.availabilityEpoch",
        "dispatchDeferredWakePowerChanged(wakePowerChangedEpoch)")

bg_params = body(sky,
    "setWCL_CONFIG_BG_PARAMS(apple80211_bg_params *data)",
    "WCL_CONFIG_BG_PARAMS")
ordered(bg_params, "family availability acknowledgement rendezvous",
        "if (data == nullptr)",
        "instance != nullptr",
        "instance->noteDeferredWakePowerChangedEdge(",
        "/*scanTerminalEdge=*/false",
        "return kIOReturnUnsupported")

radio_power_entry = body(v2,
                         "int AirportItlwm::handlePowerStateChange(uint32_t newState,",
                         "radio power transition entry")
require(radio_power_entry, "gate->runAction(handlePowerStateChangeGated, &args)",
        "radio power state serialized by command gate")
radio_power = body(v2,
                   "int AirportItlwm::handlePowerStateChangeCore",
                   "radio power transition core")
ordered(radio_power, "PowerOn waits for the exact lower-ready epoch",
        "armDeferredPowerOnAvailability()",
        "enableAdapter(netif)",
        "waitForDeferredPowerOnAvailability(",
        "kAirportItlwmDeferredPowerAvailabilityCancelEpoch")
require(radio_power, "publishDeferredPowerOffAvailability();",
        "serialized PowerOff cancellation")
forbid(radio_power, "Transition::PowerOn",
       "optimistic radio PowerOn carrier")
power_setter = body(v2, "setPOWER(OSObject *object,",
                    "setPOWER")
require(power_setter, "return handlePowerStateChange(requestedState, NULL);",
        "Tahoe setPOWER propagates synchronous PowerOn status")
system_power = body(v2,
                    "void AirportItlwm::handleSystemPowerStateChange",
                    "system power transition")
ordered(system_power,
        "system PowerOn defers the ordered wake edge without blocking IOPM",
        "const uint64_t availabilityEpoch",
        "armDeferredPowerOnAvailability(",
        "/*wakeBulletinPending=*/true",
        "enableAdapter(netif)",
        "kAirportItlwmDeferredPowerAvailabilityCancelEpoch")
forbid(system_power, "waitForDeferredPowerOnAvailability(",
       "system IOPM callback blocking on Intel backend readiness")
require(system_power, "publishDeferredPowerOffAvailability();",
        "serialized system PowerOff cancellation")
forbid(system_power, "Transition::PowerOn",
       "optimistic system PowerOn carrier")
disable_adapter = body(v2, "void AirportItlwm::disableAdapter(IONetworkInterface *netif)",
                       "controller disableAdapter")
ordered(disable_adapter, "disable cancellation precedes PowerOff carrier",
        "publishDeferredPowerOffAvailability();",
        "disableAdapterCore(netif)")
disable_core = body(v2, "void AirportItlwm::disableAdapterCore(IONetworkInterface *netif)",
                    "controller disableAdapter core")
forbid(disable_core, "cancelDeferredPowerOnAvailability();",
       "untagged post-PowerOff cancellation")

for token in ("IEEE80211_EVT_WCL_SCAN_TERMINAL",
              "IEEE80211_EVT_WCL_SCAN_INVALIDATED",
              "IEEE80211_EVT_WCL_SCAN_REOPENED",
              "IEEE80211_EVT_WCL_SCAN_STARTED",
              "IEEE80211_EVT_WCL_SCAN_START_REJECTED",
              "ieee80211_wcl_scan_terminal",
              "ieee80211_wcl_scan_started",
              "ieee80211_wcl_scan_start_rejected",
              "ic_wcl_scan_suppress_scan_done_once",
              "ic_initial_scan_census_only",
              "ic_newstate_preflight"):
    require(i80211_var, token, "net80211 exact WCL contract")
require(i80211_node, "__atomic_exchange_n",
        "one-shot generic SCAN_DONE consume")
for token in ("IEEE80211_SCAN_COMPLETION_WCL_HANDOFF",
              "IEEE80211_SCAN_COMPLETION_WCL_FOREGROUND",
              "ieee80211_end_scan_controlled"):
    require(i80211_node_h, token, "controlled foreground terminal ABI")
controlled_end = body(i80211_node, "ieee80211_end_scan_controlled(struct _ifnet *ifp,",
                      "controlled net80211 scan terminal")
ordered(controlled_end, "controlled terminal suppresses generic completion and selection",
        "const int generic_terminal",
        "if (generic_terminal && ic->ic_event_handler",
        "IEEE80211_EVT_SCAN_DONE",
        "if (!generic_terminal)",
        "ieee80211_reset_scan(ifp)",
        "return;")
ordered(controlled_end, "initial hardware census publishes but never joins",
        "const int initial_scan_census_only",
        "IEEE80211_EVT_SCAN_DONE",
        "if (!generic_terminal)",
        "if (initial_scan_census_only)",
        "kAirportItlwmPostPltiTraceEventSelectionHeld",
        "return;")
require(i80211, "ic->ic_initial_scan_census_only = 0;",
        "initial census one-shot initialization")
ordered(iwn, "IWN checked hardware-enable census arm",
        "__atomic_store_n(&ic->ic_initial_scan_census_only, 1",
        "ieee80211_begin_scan_with_result(ifp)")
for source, label in ((iwm_mac, "IWM"), (iwx, "IWX")):
    ordered(source, f"{label} hardware-enable census arm",
            "__atomic_store_n(&ic->ic_initial_scan_census_only, 1",
            "ieee80211_begin_scan(ifp)")
for source, label in ((iwn, "IWN"), (iwm_mac, "IWM"), (iwx, "IWX")):
    ordered(source, f"{label} stop clears census owner",
            "__atomic_store_n(&ic->ic_initial_scan_census_only, 0",
            "__ATOMIC_RELEASE)")
ordered(i80211_proto, "preflight before epoch",
        "ic_newstate_preflight", "ieee80211_pae_assoc_epoch_note_newstate")
require(i80211, "ic_wcl_scan_active",
        "cache timer WCL auto-roam fence")

print("Tahoe WCL physical-scan lifecycle: PASS")
PY
