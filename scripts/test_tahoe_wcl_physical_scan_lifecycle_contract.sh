#!/usr/bin/env bash
# Static + pure-unit regression gate for the first real Tahoe WCL scan layer.
# It deliberately covers only an associated background scan; foreground scan
# start/error ownership is a later backend bridge, not a timer fallback.
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
        "instance->reserveWclPhysicalScan(&generation)",
        "ieee80211_begin_cache_bgscan(&ic->ic_ac.ac_if)",
        "instance->activateWclPhysicalScan(generation)")
require(request, "instance->failWclPhysicalScanStart(generation)",
        "failed physical-start reconciliation")
forbid(request, "scheduleScanSource", "timer completion in WCL request")
forbid(request, "if (fScanResultWrapping)", "iterator-as-busy admission")

abort = body(sky, "setWCL_SCAN_ABORT(void *data)", "setWCL_SCAN_ABORT")
ordered(abort, "real WCL abort",
        "instance->markWclPhysicalScanAborting(&generation)",
        "controller->abortScanForWcl()")
require(abort, "resumeWclPhysicalScanAfterAbortFailure(generation)",
        "failed backend abort returns the ticket to the live terminal")
forbid(abort, "cancelScanSource", "timer cancellation in WCL abort")
forbid(abort, "APPLE80211_M_SCAN_DONE", "synthetic terminal in WCL abort")
forbid(abort, "ic->ic_flags &=", "manual net80211 scan-flag clear")

event = body(v2, "eventHandler(struct ieee80211com *ic, int msgCode, void *data)",
             "eventHandler")
ordered(event, "physical terminal claim",
        "case IEEE80211_EVT_SCAN_DONE:",
        "claimWclPhysicalScanCompletion",
        "postWclPhysicalScanCompletionGated")
require(event, "CompletionDisposition::Suppress",
        "teardown late-terminal suppression")

publisher = body(v2, "postWclPhysicalScanCompletionGated(",
                 "physical WCL completion publisher")
ordered(publisher, "WCL terminal publisher",
        "ownsWclPhysicalScanCompletion(generation)",
        "postWclScanResultsGated", "finishWclPhysicalScanCompletion(generation)")
require(publisher, "APPLE80211_M_WCL_SCAN_DONE",
        "abort terminal WCL completion")

fake = body(v2, "void AirportItlwm::fakeScanDone", "fakeScanDone")
require(fake, "postMessageGated", "legacy generic scan completion")
require(fake, "APPLE80211_M_SCAN_DONE", "legacy generic scan bulletin")
forbid(fake, "postWclScanResultsGated", "fake WCL result publication")

for token in (
        "enum class Phase", "Draining", "CompletionDisposition",
        "beginDraining", "reopenAfterRadioReset", "resumeAfterAbortFailure",
        "claimCompletion"):
    require(contracts, token, "ticket reducer")
require(v2_hpp, "AirportItlwmWclPhysicalScanLifecycle",
        "per-controller WCL ticket lifecycle")
require(v2_hpp, "fWclPhysicalScanLifecycle",
        "per-controller WCL ticket storage")
require(v2, "invalidateWclPhysicalScan();",
        "teardown/power invalidation")

require(driver_controller, "virtual IOReturn abortScanForWcl() = 0;",
        "backend WCL abort API")
for source, flags, endscan, label in (
        (iwn, "IWN_FLAG_WCL_SCAN_ABORTING", "ieee80211_end_scan(ifp);", "IWN"),
        (iwm, "IWM_FLAG_WCL_SCAN_ABORTING", "abortScanForWcl()", "IWM"),
        (iwx, "IWX_FLAG_WCL_SCAN_ABORTING", "abortScanForWcl()", "IWX"),
):
    require(source, flags, f"{label} WCL abort marker")
    require(source, endscan, f"{label} physical terminal path")
require(iwn_var, "IWN_FLAG_WCL_SCAN_ABORTING", "IWN marker declaration")
require(iwm_var, "IWM_FLAG_WCL_SCAN_ABORTING", "IWM marker declaration")
require(iwx_var, "IWX_FLAG_WCL_SCAN_ABORTING", "IWX marker declaration")
for source, label in ((iwm_mac, "IWM"), (iwx, "IWX")):
    require(source, "WCL_SCAN_ABORTING", f"{label} endscan abort preservation")
    require(source, "ieee80211_end_scan", f"{label} endscan terminal")

print("Tahoe WCL physical-scan lifecycle: PASS")
PY
