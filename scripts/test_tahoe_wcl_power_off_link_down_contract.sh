#!/usr/bin/env bash
# Static regression gate for the reference split between public DISASSOCIATE
# link-down ownership and radio/system power-off availability/teardown.
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

python3 - "$root" <<'PY'
from pathlib import Path
import sys


source = (Path(sys.argv[1]) / "AirportItlwm/AirportItlwmV2.cpp").read_text()


def fail(message):
    raise SystemExit(f"Tahoe WCL power-off link-down: {message}")


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
if "ic->ic_state != IEEE80211_S_RUN" not in producer:
    fail("link-state carrier must require an authoritative RUN BSS")
if "(linkUp && ic->ic_state" in producer:
    fail("link-down carrier remains admitted outside the RUN association")

availability = body(
    "publishDeferredPowerAvailabilityGated(OSObject *target"
)
off_start = availability.find(
    "action == kAirportItlwmDeferredPowerAvailabilityPublishOff"
)
on_start = availability.find(
    "action != kAirportItlwmDeferredPowerAvailabilityPublishOn", off_start
)
if off_start < 0 or on_start < 0:
    fail("missing bounded PublishOff branch")
off_branch = availability[off_start:on_start]
ordered(
    off_branch,
    "cancelDeferredPowerOnAvailabilityRaw()",
    "postTahoeWclInternalLinkDownInd()",
    "getTahoeOwnerRegistry().association =",
    "getTahoeOwnerRegistry().publicAssociation =",
    "postTahoeDriverAvailabilityTransition(",
    "TahoeDriverAvailabilityContracts::Transition::PowerOff",
)
if "postTahoeWclLinkStateInd(" in off_branch or \
        "setWCL_JOIN_ABORT(" in off_branch or \
        "APPLE80211_M_WCL_JOIN_ABORT" in off_branch:
    fail("power-off must use only the exact internal fallback, never RUN/JOIN_ABORT")

radio = body("handlePowerStateChangeCore(uint32_t newState")
for transition in (
    "newState == kWiFiPowerOff",
    "newState == kWiFiPowerStandby",
):
    start = radio.find(transition)
    if start < 0:
        fail(f"missing radio transition: {transition}")
ordered(
    radio,
    "publishDeferredPowerOffAvailability()",
    "disableAdapterCore(netif)",
)

system = body("void AirportItlwm::handleSystemPowerStateChange(")
ordered(
    system,
    "publishDeferredPowerOffAvailability()",
    "disableAdapterCore(netif)",
)

disable = body("void AirportItlwm::disableAdapterCore(")
ordered(
    disable,
    "ic->ic_opmode == IEEE80211_M_STA",
    "ic->ic_state == IEEE80211_S_RUN",
    "IEEE80211_SEND_MGMT(",
    "IEEE80211_FC0_SUBTYPE_DEAUTH",
    "IEEE80211_REASON_AUTH_LEAVE",
    "power_off STA_DEAUTH_QUIESCE",
    "IOSleep(20)",
    "fAPSTAOwner->prepareForRadioReset()",
    "fHalService->disable(netif)",
)

print("Tahoe WCL power-off link-down contract: PASS")
PY
