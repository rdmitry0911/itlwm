#!/usr/bin/env bash
# Static + pure-unit regression gate for the exact IWN-owned Tahoe WCL scan
# layer.  IWM/IWX intentionally remain fail-closed until they own an equally
# strict firmware-terminal lease.
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
i80211_proto = (root / "itl80211/openbsd/net80211/ieee80211_proto.h").read_text()
i80211 = (root / "itl80211/openbsd/net80211/ieee80211.c").read_text()
iwn = (root / "itlwm/hal_iwn/ItlIwn.cpp").read_text()
iwm = (root / "itlwm/hal_iwm/ItlIwm.cpp").read_text()
iwm_mac = (root / "itlwm/hal_iwm/mac80211.cpp").read_text()
iwx = (root / "itlwm/hal_iwx/ItlIwx.cpp").read_text()
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
ordered(request, "physical WCL request",
        "ic->ic_state != IEEE80211_S_RUN",
        "instance->reserveWclPhysicalScan(",
        "fHalService->beginWclBackgroundScan",
        "instance->activateWclPhysicalScan(generation, backendGeneration)")
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
ordered(event, "tagged physical terminal claim",
        "IEEE80211_EVT_WCL_SCAN_TERMINAL",
        "claimWclPhysicalScanCompletion(terminal.generation",
        "queueWclPhysicalScanTerminalPublication")
require(event, "IEEE80211_EVT_WCL_SCAN_INVALIDATED",
        "hardware-reset invalidation fence")
require(event, "IEEE80211_EVT_WCL_SCAN_REOPENED",
        "confirmed radio-reset reopening fence")
scan_done = event[event.find("case IEEE80211_EVT_SCAN_DONE:"):]
forbid(scan_done[:scan_done.find("case IEEE80211_EVT_WCL_REASSOC_DONE:")],
       "claimWclPhysicalScanCompletion", "generic SCAN_DONE WCL claim")

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
        "enum class Phase", "Draining", "CompletionDisposition",
        "beginDraining", "reopenAfterRadioReset", "resumeAfterAbortFailure",
        "claimCompletion", "activeBackendGeneration",
        "terminalBackendGeneration", "pendingCompletion", "invalidate",
        "pendingCompletionForGeneration", "Pending"):
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
for token in ("beginWclBackgroundScan", "abortWclBackgroundScan",
              "invalidateWclBackgroundScan", "kIOReturnUnsupported"):
    require(hal_service, token, "fail-closed HAL WCL boundary")

for source, label in ((iwm, "IWM"), (iwx, "IWX"),
                      (iwm_mac, "IWM mac80211")):
    forbid(source, "WCL_SCAN", f"{label} premature WCL owner")
for source, label in ((iwm_var, "IWM var"), (iwx_var, "IWX var")):
    forbid(source, "WCL_SCAN", f"{label} premature WCL marker")

for token in ("IWN_SCAN_LEASE_WCL_BACKGROUND", "IWN_SCAN_LEASE_GENERIC_FOREGROUND",
              "IWN_SCAN_LEASE_GENERIC_BACKGROUND", "sc_scan_lease_lock",
              "terminal_claimed", "publication_invalidated",
              "sc_scan_lease_replay_task_admission_state"):
    require(iwn_var, token, "strict IWN physical scan lease")
for token in ("iwn_scan_lease_replay_task_admission_close",
              "iwn_scan_lease_replay_task_admission_drain",
              "iwn_scan_lease_schedule_replay_task"):
    require(iwn, token, "IWN replay task detach admission")
for token in ("beginWclBackgroundScan", "abortWclBackgroundScan",
              "iwn_scan_lease_reserve", "iwn_scan_lease_claim_terminal",
              "iwn_scan_lease_finish_terminal", "iwn_scan_lease_replay_task",
              "iwn_scan_lease_begin_hardware_invalidation"):
    require(iwn, token, "IWN exact WCL owner")
stop_scan = body(iwn, "case IWN_STOP_SCAN:", "IWN STOP_SCAN")
ordered(stop_scan, "IWN terminal ownership",
        "iwn_scan_continue", "iwn_scan_lease_claim_terminal",
        "ieee80211_end_scan(ifp)", "IEEE80211_EVT_WCL_SCAN_TERMINAL",
        "iwn_scan_lease_finish_terminal")
require(iwn, "ic_wcl_scan_suppress_scan_done_once",
        "IWN one-shot generic completion suppression")
require(iwn, "IEEE80211_EVT_WCL_SCAN_INVALIDATED",
        "IWN reset invalidation event")
require(iwn, "IEEE80211_EVT_WCL_SCAN_REOPENED",
        "IWN confirmed radio reset event")

for token in ("IEEE80211_EVT_WCL_SCAN_TERMINAL",
              "IEEE80211_EVT_WCL_SCAN_INVALIDATED",
              "IEEE80211_EVT_WCL_SCAN_REOPENED",
              "ieee80211_wcl_scan_terminal",
              "ic_wcl_scan_suppress_scan_done_once",
              "ic_newstate_preflight"):
    require(i80211_var, token, "net80211 exact WCL contract")
require(i80211_node, "__atomic_exchange_n",
        "one-shot generic SCAN_DONE consume")
ordered(i80211_proto, "preflight before epoch",
        "ic_newstate_preflight", "ieee80211_pae_assoc_epoch_note_newstate")
require(i80211, "ic_wcl_scan_active",
        "cache timer WCL auto-roam fence")

print("Tahoe WCL physical-scan lifecycle: PASS")
PY
