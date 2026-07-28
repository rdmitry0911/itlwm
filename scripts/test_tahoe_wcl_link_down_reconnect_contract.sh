#!/usr/bin/env bash
# Static regression gate for the Tahoe WCL link-down indication that lets the
# framework leave its deauth drain and start a fresh saved-profile join.
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

python3 - "$root" <<'PY'
from pathlib import Path
import sys


source = (Path(sys.argv[1]) / "AirportItlwm/AirportItlwmV2.cpp").read_text()


def fail(message):
    raise SystemExit(f"Tahoe WCL link-down reconnect: {message}")


def body(marker):
    start = source.find(marker)
    if start < 0:
        fail(f"missing marker: {marker}")
    opening = source.find("{", start)
    if opening < 0:
        fail(f"missing body: {marker}")
    depth = 0
    for position in range(opening, len(source)):
        if source[position] == "{":
            depth += 1
        elif source[position] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1:position]
    fail(f"unterminated body: {marker}")


def ordered(text, *needles):
    cursor = 0
    for needle in needles:
        position = text.find(needle, cursor)
        if position < 0:
            fail(f"missing ordered token: {needle}")
        cursor = position + len(needle)


producer = body("static bool postTahoeWclLinkStateInd(")
for token in (
    "TahoeWclLinkChangedPayload payload",
    "IEEE80211_ADDR_COPY(payload.bssid, ic->ic_bss->ni_bssid)",
    "payload.linkState = linkUp ? 1 : 0",
    "payload.interfaceType = kTahoeWclInfraInterfaceType",
    "payload.reasonCode = buildTahoeWclLinkReason(rawReason)",
    "kTahoeWclLinkChanged",
    "sizeof(payload)",
):
    if token not in producer:
        fail(f"missing 0xd8 carrier invariant: {token}")
if "setLinkState(" in producer or "reportLinkStatus(" in producer:
    fail("0xd8 producer must not force parent or Skywalk link state")

down_action = body("static IOReturn postTahoeWclLinkDownIndGated(")
ordered(
    down_action,
    "static_cast<unsigned int>((uintptr_t)arg0)",
    "postTahoeWclLinkStateInd(controller, false, rawReason)",
)

event_handler = body("void AirportItlwm::\neventHandler(")
deauth_case_start = event_handler.find("case IEEE80211_EVT_STA_DEAUTH:")
scan_case_start = event_handler.find(
    "case IEEE80211_EVT_SCAN_DONE:", deauth_case_start
)
if deauth_case_start < 0 or scan_case_start < 0:
    fail("missing bounded STA_DEAUTH case")
deauth_case = event_handler[deauth_case_start:scan_case_start]
if "APPLE80211_M_DEAUTH_RECEIVED" not in deauth_case:
    fail("legacy DEAUTH_RECEIVED publication was not preserved")

tail_start = event_handler.find("gate->runAction(postMessageGated")
if tail_start < 0:
    fail("missing common postMessage tail")
tail = event_handler[tail_start:]
ordered(
    tail,
    "gate->runAction(postMessageGated",
    "if (msgCode == IEEE80211_EVT_STA_DEAUTH)",
    "postTahoeWclLinkDownIndGated",
    "ic->ic_deauth_reason",
)

print("Tahoe WCL link-down reconnect contract: PASS")
PY
