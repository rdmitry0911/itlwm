#!/usr/bin/env bash
# Static regression gate for AppleBCMWLANNetAdapter's exact internal WCL
# link-down fallback and its sole reference owner, public DISASSOCIATE.
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

python3 - "$root" <<'PY'
from pathlib import Path
import sys


root = Path(sys.argv[1])
controller = (root / "AirportItlwm/AirportItlwmV2.cpp").read_text()
interface = (root / "AirportItlwm/AirportItlwmSkywalkInterface.cpp").read_text()
header = (root / "AirportItlwm/AirportItlwmV2.hpp").read_text()


def fail(message):
    raise SystemExit(f"Tahoe WCL DISASSOCIATE link-down: {message}")


def body(source, marker):
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


if "bool postTahoeWclInternalLinkDownInd();" not in header:
    fail("controller entry point is not declared")

producer = body(controller, "bool AirportItlwm::postTahoeWclInternalLinkDownInd()")
ordered(
    producer,
    "if (fNetIf == nullptr)",
    "TahoeWclLinkChangedPayload payload",
    "bzero(&payload, sizeof(payload))",
    "payload.interfaceType = kTahoeWclInfraInterfaceType",
    "payload.reasonCode = 9",
    "postMessage(fNetIf, kTahoeWclLinkChanged",
    "sizeof(payload), true",
)
for forbidden in (
    "IEEE80211_ADDR_COPY",
    "ic_state",
    "ic_bss",
    "buildTahoeWclLinkReason",
    "setLinkState",
    "reportLinkStatus",
):
    if forbidden in producer:
        fail(f"internal fallback gained a non-reference dependency: {forbidden}")

setter = body(interface, "setDISASSOCIATE(void *ad)")
ordered(
    setter,
    'clearExternalPmkEligibilityLocked("setDISASSOCIATE")',
    "instance->postTahoeWclInternalLinkDownInd()",
    "if (ic->ic_state < IEEE80211_S_SCAN)",
)
if setter.count("postTahoeWclInternalLinkDownInd()") != 1:
    fail("public DISASSOCIATE must own exactly one internal fallback call")

availability = body(
    controller, "publishDeferredPowerAvailabilityGated(OSObject *target"
)
off_start = availability.find(
    "action == kAirportItlwmDeferredPowerAvailabilityPublishOff"
)
on_start = availability.find(
    "action != kAirportItlwmDeferredPowerAvailabilityPublishOn", off_start
)
if off_start < 0 or on_start < 0:
    fail("missing bounded Intel PublishOff branch")
off_branch = availability[off_start:on_start]
ordered(
    off_branch,
    "postTahoeWclInternalLinkDownInd()",
    "postTahoeDriverAvailabilityTransition(",
    "TahoeDriverAvailabilityContracts::Transition::PowerOff",
)
if "postTahoeWclLinkStateInd(" in off_branch:
    fail("Intel power-off substitute must not require an authoritative RUN BSS")

print("Tahoe WCL DISASSOCIATE link-down contract: PASS")
PY
