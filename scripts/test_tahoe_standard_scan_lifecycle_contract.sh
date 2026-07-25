#!/usr/bin/env bash
# Contract for the user-visible Tahoe CoreWLAN scan path.  A physical request
# must receive its sole completion from the IWN terminal, never from the
# cache-only 100 ms compatibility timer.
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

python3 - "$root" <<'PY'
from pathlib import Path
import sys


root = Path(sys.argv[1])
sky = (root / "AirportItlwm/AirportItlwmSkywalkInterface.cpp").read_text(
    encoding="utf-8")
v2 = (root / "AirportItlwm/AirportItlwmV2.cpp").read_text(encoding="utf-8")
iwn = (root / "itlwm/hal_iwn/ItlIwn.cpp").read_text(encoding="utf-8")


def fail(message: str) -> None:
    raise SystemExit(f"Tahoe standard-scan lifecycle contract: {message}")


def require(text: str, needle: str, label: str) -> None:
    if needle not in text:
        fail(f"missing {label}: {needle}")


def forbid(text: str, needle: str, label: str) -> None:
    if needle in text:
        fail(f"unexpected {label}: {needle}")


def ordered(text: str, label: str, *needles: str) -> None:
    cursor = 0
    for needle in needles:
        position = text.find(needle, cursor)
        if position < 0:
            fail(f"{label} missing ordered token: {needle}")
        cursor = position + len(needle)


def body(text: str, marker: str, label: str) -> str:
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
        "if (sd == nullptr)",
        "APPLE80211_SCAN_TYPE_FAST",
        "scheduleScanSource(100)",
        "cancelScanSource()",
        "IFF_RUNNING",
        "IEEE80211_S_RUN",
        "ieee80211_begin_cache_bgscan(&ic->ic_ac.ac_if);",
        "IEEE80211_S_SCAN",
        "ieee80211_begin_scan(&ic->ic_ac.ac_if);",
        "return kIOReturnBusy;",
):
    require(setter, token, "standard scan admission")
if setter.count("scheduleScanSource(100)") != 1:
    fail("cache-only timer must be armed exactly once")
fast_start = setter.find("if (sd->scan_type == APPLE80211_SCAN_TYPE_FAST)")
physical_start = setter.find("if (instance == nullptr || !instance->cancelScanSource())")
if fast_start < 0 or physical_start < 0 or fast_start >= physical_start:
    fail("cache-only and physical scan branches are not separated")
fast_branch = setter[fast_start:physical_start]
forbid(fast_branch, "APPLE80211_SCAN_TYPE_PASSIVE",
       "PASSIVE request in cache-only branch")
forbid(setter[physical_start:], "scheduleScanSource(100)",
       "synthetic completion after physical admission")
forbid(setter, "postMessage(",
       "direct synthetic completion in standard scan setter")
forbid(setter, "postMessageGated",
       "command-gated completion from standard scan setter")
ordered(setter, "physical scan admission after cache-only branch",
        "if (sd->scan_type == APPLE80211_SCAN_TYPE_FAST)",
        "scheduleScanSource(100)",
        "cancelScanSource()",
        "if (ic->ic_state == IEEE80211_S_RUN)",
        "ieee80211_begin_cache_bgscan(&ic->ic_ac.ac_if);",
        "if (ic->ic_state == IEEE80211_S_SCAN)",
        "ieee80211_begin_scan(&ic->ic_ac.ac_if);")

stop_scan = body(iwn, "case IWN_STOP_SCAN", "IWN physical scan terminal")
for token in (
        "iwn_scan_lease_claim_terminal",
        "ieee80211_end_scan",
):
    require(stop_scan, token, "IWN terminal owner")
ordered(stop_scan, "IWN terminal reaches net80211 completion",
        "iwn_scan_lease_claim_terminal", "ieee80211_end_scan")

scan_done_start = v2.find("case IEEE80211_EVT_SCAN_DONE:")
scan_done_end = v2.find("case IEEE80211_EVT_WCL_REASSOC_DONE:", scan_done_start)
if scan_done_start < 0 or scan_done_end < 0:
    fail("generic scan-done consumer is missing or unterminated")
scan_done = v2[scan_done_start:scan_done_end]
for token in (
        "sRT.scanDoneCount++",
        "apple80211Msg = APPLE80211_M_SCAN_DONE",
        "msgData = &scanStatus",
        "msgDataLen = sizeof(scanStatus)",
):
    require(scan_done, token, "real generic scan terminal publication")
require(v2, "gate->runAction(postMessageGated,",
        "command-gated CoreWLAN scan completion publication")

print("Tahoe standard-scan lifecycle contract: PASS")
PY
