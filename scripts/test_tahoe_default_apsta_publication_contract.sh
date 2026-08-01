#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

python3 - "$PROJECT_DIR" <<'PY'
from pathlib import Path
import sys

root = Path(sys.argv[1])
controller = (root / "AirportItlwm/AirportItlwmV2.cpp").read_text()
header = (root / "AirportItlwm/AirportItlwmV2.hpp").read_text()

assert "IOReturn publishDefaultAPSTAInterface();" in header

publish = controller[
    controller.index("IOReturn AirportItlwm::publishDefaultAPSTAInterface()"):
    controller.index("IOReturn AirportItlwm::materializeAPSTAInterface(")
]
for needle in (
    "fHalService->supportsAPMode()",
    "create.version = APPLE80211_VERSION",
    "create.role = APPLE80211_VIF_SOFT_AP",
    'reinterpret_cast<char *>(create.bsd_name), "ap1"',
    "ensureAPSTAOwner(&create)",
    "materializeAPSTAInterface(&create)",
    "deleteAPSTAOwner();",
):
    assert needle in publish, f"missing default APSTA publication edge: {needle}"
assert publish.index("ensureAPSTAOwner(&create)") < publish.index(
    "materializeAPSTAInterface(&create)")

ready = controller[
    controller.index("if (msgCode == IEEE80211_EVT_WCL_SCAN_REOPENED)"):
    controller.index("if (msgCode == IEEE80211_EVT_STANDARD_SCAN_INVALIDATED)")
]
for needle in (
    "reopenWclPhysicalScanAfterRadioReset()",
    "reopenStandardPhysicalScanAfterRadioReset()",
    "gate->runAction(publishDefaultAPSTAInterfaceGated)",
    "noteRadioScanReadyAndQueuePowerOnAvailability()",
):
    assert needle in ready, f"missing Tahoe backend-ready publication stage: {needle}"
assert ready.index("reopenStandardPhysicalScanAfterRadioReset()") < ready.index(
    "gate->runAction(publishDefaultAPSTAInterfaceGated)") < ready.index(
    "noteRadioScanReadyAndQueuePowerOnAvailability()")

gated = controller[
    controller.index("publishDefaultAPSTAInterfaceGated(OSObject *owner"):
    controller.index("static void\nhandleTahoeBootChipImage")
]
assert "self->publishDefaultAPSTAInterface()" in gated
assert "result != kIOReturnUnsupported" in gated
assert "continuing with primary STA" in gated

start = controller[
    controller.index("bool AirportItlwm::start(IOService *provider)"):
    controller.index("void AirportItlwm::stop(IOService *provider)")
]
assert "publishDefaultAPSTAInterface();" not in start, (
    "APSTA capability must not be queried before firmware setup")

boot = controller[
    controller.index("void AirportItlwm::performTahoeBootChipImage()"):
    controller.index("bool AirportItlwmBootNub::start(IOService *provider)")
]
assert "publishDefaultAPSTAInterface();" not in boot, (
    "APSTA capability must not be queried before asynchronous IWN init")

print("PASS: Tahoe publishes capable APSTA role at backend-ready edge")
PY
