#!/usr/bin/env bash
# Contract for Tahoe normal scans: FAST is cache-only, while every ordinary
# IWN request owns a generation-tagged physical lease through its terminal.
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

python3 - "$root" <<'PY'
from pathlib import Path
import sys

root = Path(sys.argv[1])
sky = (root / "AirportItlwm/AirportItlwmSkywalkInterface.cpp").read_text()
v2 = (root / "AirportItlwm/AirportItlwmV2.cpp").read_text()
v2h = (root / "AirportItlwm/AirportItlwmV2.hpp").read_text()
iwn = (root / "itlwm/hal_iwn/ItlIwn.cpp").read_text()
iwnh = (root / "itlwm/hal_iwn/ItlIwn.hpp").read_text()
iwnvar = (root / "itlwm/hal_iwn/if_iwnvar.h").read_text()
hal = (root / "include/HAL/ItlHalService.hpp").read_text()
node = (root / "itl80211/openbsd/net80211/ieee80211_node.c").read_text()
nodeh = (root / "itl80211/openbsd/net80211/ieee80211_node.h").read_text()
var = (root / "itl80211/openbsd/net80211/ieee80211_var.h").read_text()
fsm = (root / "AirportItlwm/TahoeStandardScanContracts.hpp").read_text()

def fail(message):
    raise SystemExit(f"Tahoe standard-scan lifecycle contract: {message}")

def require(text, token, label):
    if token not in text:
        fail(f"missing {label}: {token}")

def forbid(text, token, label):
    if token in text:
        fail(f"unexpected {label}: {token}")

def ordered(text, label, *tokens):
    cursor = 0
    for token in tokens:
        found = text.find(token, cursor)
        if found < 0:
            fail(f"{label} missing ordered token: {token}")
        cursor = found + len(token)

def body(text, marker, label):
    start = text.find(marker)
    if start < 0:
        fail(f"missing {label}")
    opening = text.find("{", start)
    if opening < 0:
        fail(f"missing body for {label}")
    depth = 0
    for position in range(opening, len(text)):
        if text[position] == "{":
            depth += 1
        elif text[position] == "}":
            depth -= 1
            if depth == 0:
                return text[opening + 1:position]
    fail(f"unterminated {label}")

setter = body(sky, "IOReturn AirportItlwmSkywalkInterface::\nsetSCAN_REQ",
              "standard SCAN_REQ setter")
for token in (
        "APPLE80211_SCAN_TYPE_FAST",
        "scheduleScanSource(100)",
        "reserveStandardPhysicalScan",
        "fHalService->beginStandardScan",
        "if (backendGeneration != 0)",
        "activateStandardPhysicalScan",
        "failStandardPhysicalScanStart",
        "finishPendingStandardPhysicalScanTerminal",
        "ieee80211_begin_cache_bgscan(&ic->ic_ac.ac_if);",
        "ieee80211_begin_scan(&ic->ic_ac.ac_if);",
):
    require(setter, token, "normal scan setter")
if setter.count("scheduleScanSource(100)") != 1:
    fail("FAST cache timer must be armed exactly once")
fast = setter.find("if (sd->scan_type == APPLE80211_SCAN_TYPE_FAST)")
physical = setter.find("reserveStandardPhysicalScan")
if fast < 0 or physical < 0 or fast >= physical:
    fail("FAST and physical requests are not split")
forbid(setter, "cancelScanSource", "unsafe timer cancellation")
forbid(setter, "postMessage(", "synthetic physical completion")
forbid(setter, "postMessageGated", "gated synthetic physical completion")
ordered(setter, "exact lower normal-scan path",
        "reserveStandardPhysicalScan", "fHalService->beginStandardScan",
        "if (backendGeneration != 0)", "activateStandardPhysicalScan")
nonzero_backend = setter[setter.find("if (backendGeneration != 0)"):]
ordered(nonzero_backend, "submitted lower lease stays fenced on error",
        "activateStandardPhysicalScan", "return beginResult;")
fallback = setter[setter.find("beginResult == kIOReturnUnsupported"):]
ordered(fallback, "fallback releases exact reservation first",
        "failStandardPhysicalScanStart(generation)",
        "ieee80211_begin_cache_bgscan(&ic->ic_ac.ac_if);")

for token in (
        "struct AirportItlwmStandardScanLifecycle",
        "cachedTerminalPending",
        "cachedTerminalPublishing",
        "TahoeStandardScanContracts::State physicalState",
        "fStandardScanLifecycle",
):
    require(v2h, token, "controller normal-scan state")
for token in (
        "bool AirportItlwm::scheduleScanSource",
        "bool AirportItlwm::beginCachedScanTerminal",
        "IOReturn AirportItlwm::reserveStandardPhysicalScan",
        "TahoeStandardScanContracts::reserve",
        "TahoeStandardScanContracts::claimTerminal",
        "TahoeStandardScanContracts::beginDraining",
        "TahoeStandardScanContracts::reopenAfterRadioReset",
        "fWclPhysicalScanLifecycle.admissionLock",
):
    require(v2, token, "shared physical admission")
schedule = body(v2, "bool AirportItlwm::scheduleScanSource", "FAST timer fence")
for token in (
        "cachedTerminalPending",
        "TahoeStandardScanContracts::idle(&standard.physicalState)",
        "wcl.state.phase == TahoeWclPhysicalScanContracts::Phase::Idle",
        "source->setTimeoutMS(timeoutMs)",
):
    require(schedule, token, "FAST timer fence")
reserve = body(v2, "IOReturn AirportItlwm::reserveStandardPhysicalScan",
               "normal physical reservation")
for token in (
        "!standard.cachedTerminalPending",
        "!standard.cachedTerminalPublishing",
        "TahoeStandardScanContracts::idle(&standard.physicalState)",
        "wcl.state.phase == TahoeWclPhysicalScanContracts::Phase::Idle",
        "TahoeStandardScanContracts::reserve(&standard.physicalState",
):
    require(reserve, token, "normal/WCL exclusion")
wcl_reserve = body(v2, "IOReturn AirportItlwm::reserveWclPhysicalScan",
                   "WCL reservation")
for token in (
        "standard.cachedTerminalPending",
        "standard.cachedTerminalPublishing",
        "!TahoeStandardScanContracts::idle(&standard.physicalState)",
):
    require(wcl_reserve, token, "WCL/normal exclusion")
radio_invalidation = body(v2, "void AirportItlwm::invalidateWclPhysicalScan()",
                          "radio invalidation")
ordered(radio_invalidation, "FAST revoke precedes radio drain",
        "cancelCachedTerminal = standard.cachedTerminalPending;",
        "standard.cachedTerminalPending = false;",
        "TahoeStandardScanContracts::beginDraining",
        "source->cancelTimeout();")
forbid(radio_invalidation, "standard.cachedTerminalPublishing = false;",
       "publishing FAST terminal bypassing its release fence")
handler = body(v2, "void AirportItlwm::\neventHandler", "controller event handler")
ordered(handler, "normal terminal handling",
        "IEEE80211_EVT_STANDARD_SCAN_INVALIDATED",
        "IEEE80211_EVT_STANDARD_SCAN_TERMINAL",
        "claimStandardPhysicalScanTerminal")
fake = body(v2, "void AirportItlwm::fakeScanDone", "FAST terminal")
ordered(fake, "FAST publishing fence release",
        "beginCachedScanTerminal(sender)", "postMessageGated",
        "finishCachedScanTerminal()")
forbid(v2, "cancelScanSource", "removed timer cancellation helper")

for token in (
        "enum class Phase",
        "Starting",
        "Active",
        "Completing",
        "Draining",
        "activeGeneration",
        "activeBackendGeneration",
        "claimTerminal",
        "finishPendingTerminal",
        "reopenAfterRadioReset",
):
    require(fsm, token, "generation-tagged normal-scan reducer")
forbid(fsm, "postMessage", "framework publication in reducer")

for token in (
        "virtual IOReturn beginStandardScan(uint64_t generation, bool background,",
        "return kIOReturnUnsupported;",
):
    require(hal, token, "optional exact lower API")
for token in (
        "beginStandardScan(uint64_t generation, bool background,",
):
    require(iwnh, token, "IWN normal-scan override")
require(iwnvar, "IWN_SCAN_LEASE_STANDARD_CONTROLLER", "IWN lease owner")
begin = body(iwn, "IOReturn ItlIwn::\nbeginStandardScan", "IWN normal begin")
ordered(begin, "IWN lower scan submission",
        "iwn_scan_start(&com", "IWN_SCAN_LEASE_STANDARD_CONTROLLER",
        "&backend_generation")
scan_start = body(iwn, "int ItlIwn::\niwn_scan_start", "IWN scan start")
for token in (
        "bool standard = owner == IWN_SCAN_LEASE_STANDARD_CONTROLLER;",
        "bool prearm_background = wcl || (standard && bgscan != 0);",
        "if (standard && bgscan == 0)",
        "iwn_prepare_standard_foreground_scan(ic);",
        "iwn_scan_lease_reserve(sc, owner, upper_generation",
):
    require(scan_start, token, "IWN standard owner")
ordered(scan_start, "foreground preparation follows durable lower arm",
        "iwn_scan_lease_arm_submission(sc, serial, &abort_requested)",
        "if (abort_requested)",
        "iwn_prepare_standard_foreground_scan(ic);",
        "iwn_scan_submit(sc, flags, bgscan, prearm_background)")
for token in (
        "iwn_scan_lease_defer_terminal_replay",
        "sc->sc_scan_lease.terminal_claimed",
        "return iwn_scan_lease_defer_terminal_replay(sc, nstate, arg) ? 1 : 0;",
):
    require(iwn, token, "post-terminal foreground scan replay")
stop = body(iwn, "case IWN_STOP_SCAN", "IWN scan terminal")
ordered(stop, "tagged normal terminal after generic completion",
        "iwn_scan_lease_claim_terminal", "ieee80211_end_scan(ifp)",
        "IEEE80211_EVT_STANDARD_SCAN_TERMINAL")
for token in (
        "terminal.standard",
        "terminal.publish_standard_terminal",
        "standard_terminal.generation = terminal.upper_generation",
        "standard_terminal.backend_generation",
        "IEEE80211_EVT_STANDARD_SCAN_INVALIDATED",
):
    require(iwn, token, "exact IWN normal terminal/reset")

for token in (
        "void ieee80211_prepare_scan(struct _ifnet *);",
        "void ieee80211_begin_scan(struct _ifnet *);",
):
    require(nodeh, token, "foreground preparation split")
prepare = body(node, "void\nieee80211_prepare_scan", "scan preparation")
forbid(prepare, "ieee80211_next_scan", "second lower scan from preparation")
generic = body(node, "void\nieee80211_begin_scan", "generic scan begin")
ordered(generic, "generic scan still submits after preparation",
        "ieee80211_prepare_scan(ifp);", "ieee80211_next_scan(ifp);")
for token in (
        "IEEE80211_EVT_STANDARD_SCAN_TERMINAL",
        "IEEE80211_EVT_STANDARD_SCAN_INVALIDATED",
        "struct ieee80211_standard_scan_terminal",
):
    require(var, token, "normal terminal ABI")
end_scan = body(node, "void\nieee80211_end_scan", "scan completion")
if end_scan.count("IEEE80211_F_BGSCAN |\n                              IEEE80211_F_DISABLE_BG_AUTO_CONNECT") < 2:
    fail("early background-scan exits do not restore scan flags")

done_start = v2.find("case IEEE80211_EVT_SCAN_DONE:")
done_end = v2.find("case IEEE80211_EVT_WCL_REASSOC_DONE:", done_start)
if done_start < 0 or done_end < 0:
    fail("generic scan-done consumer is missing")
done = v2[done_start:done_end]
for token in (
        "sRT.scanDoneCount++",
        "apple80211Msg = APPLE80211_M_SCAN_DONE",
        "msgData = &scanStatus",
        "msgDataLen = sizeof(scanStatus)",
):
    require(done, token, "normal generic terminal publication")

print("Tahoe standard-scan lifecycle contract: PASS")
PY
