#!/bin/sh
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

python3 - "$repo_root" <<'PY'
from pathlib import Path
import sys

root = Path(sys.argv[1])
owner = (root / "AirportItlwm/AirportItlwmAPSTAOwner.cpp").read_text()
iwx = (root / "itlwm/hal_iwx/ItlIwx.cpp").read_text()


def body(source: str, signature: str) -> str:
    start = source.find(signature)
    if start < 0:
        raise SystemExit(f"FAIL: missing function: {signature}")
    opening = source.find("{", start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening:index + 1]
    raise SystemExit(f"FAIL: unterminated function: {signature}")


retry = body(owner, "static bool apsta_lower_start_retryable(")
for status in (
    "kIOReturnBusy",
    "kIOReturnNotReady",
    "kIOReturnTimeout",
    "kIOReturnAborted",
):
    assert status in retry, f"missing retryable lower status {status}"

hostap = body(owner, "IOReturn AirportItlwmAPSTAOwner::setHostAPMode(")
for needle in (
    "!isApRunning() && !radioResetResumePending",
    "apsta_lower_stop_pending(stopResult)",
    "confirmedHostAPStartPending = true;",
    "const IOReturn result = startLowerIfReady();",
    "state.resetState26c = 0;",
    "state.hostApTransitionState270 = 1;",
    "radioResetResumePending = true;",
    "return kIOReturnSuccess;",
):
    assert needle in hostap, f"missing accepted HostAP transition edge: {needle}"
assert hostap.index("state.hostApTransitionState270 = 1;") < \
       hostap.index("radioResetResumePending = true;"), \
       "transition state must precede watchdog retry publication"

resume = body(owner, "IOReturn AirportItlwmAPSTAOwner::resumeAfterRadioReset()")
assert "!apsta_lower_start_retryable(result)" in resume
assert "state.hostApTransitionState270 = 0;" in resume, \
    "terminal deferred-start failure must close transition state"

hal_start = body(iwx, "startAPMode(const struct ItlHalApConfig *config)")
for needle in (
    "apStartPending",
    "iwx_ap_schedule_task(this, &com.ap_start_task)",
    "return kIOReturnNotReady;",
):
    assert needle in hal_start, f"missing asynchronous IWX AP ingress: {needle}"
assert "iwx_start_ap_mode(&com, &apRuntime)" not in hal_start, \
    "upper AirportItlwm command gate must not wait for IWX q0"

lower_start = body(iwx, "iwx_ap_start_task(void *argument)")
assert "iwx_start_ap_mode(sc, &that->apRuntime)" in lower_start
assert "iwx_stop_ap_mode(sc, &that->apRuntime)" in lower_start, \
    "an in-flight start must consume a concurrent stop request"

dispatch = body(iwx, "iwx_ap_start_task_dispatch(void *argument)")
assert "iwx_task_gate_enter(sc, false)" in dispatch
assert "iwx_ap_start_task(argument)" in dispatch
assert "iwx_task_gate_leave(sc)" in dispatch

resource_start = body(iwx, "iwx_start_ap_mode(struct iwx_softc *sc,")
for stable_state in ("IEEE80211_S_INIT", "IEEE80211_S_RUN"):
    assert stable_state in resource_start, \
        f"missing stable lower AP admission state {stable_state}"
for transitional_state in ("SCAN/AUTH/ASSOC", "stable STA epoch"):
    assert transitional_state in resource_start, \
        f"missing AP/STA mixed-epoch fence evidence: {transitional_state}"
assert "sc->sc_ic.ic_state == IEEE80211_S_RUN" in resource_start, \
    "only an associated STA may lead the AP TSF/PHY context"
for needle in (
    "wclScanPhase == ItlIwxWclScanPhase::BackgroundActive",
    "generation = wclScanUpperGeneration",
    "abortWclBackgroundScan(generation)",
    "IWX_FLAG_SCANNING | IWX_FLAG_BGSCAN",
):
    assert needle in resource_start, \
        f"missing exact WCL background-scan retirement edge: {needle}"
assert resource_start.index("abortWclBackgroundScan(generation)") < \
       resource_start.index("const uint32_t blockedFlags"), \
       "active WCL cache scan must retire before generic scan-owner retry"
assert "iwx_scan_abort(sc)" not in resource_start, \
    "AP start must not clear a background scan outside the tagged WCL owner"

mapping = body(iwx, "iwx_ap_start_result_from_errno(int error)")
for errno, status in (
    ("EBUSY", "kIOReturnBusy"),
    ("ENXIO", "kIOReturnNotReady"),
    ("EWOULDBLOCK", "kIOReturnNotReady"),
):
    assert errno in mapping and status in mapping, \
        f"missing IWX transient map {errno} -> {status}"

assert "case IWX_BEACON_TEMPLATE_CMD:" in iwx, \
    "async beacon response must retire its q0 descriptor"

print("PASS: Tahoe IWX HostAP lower-epoch retry contract")
PY
