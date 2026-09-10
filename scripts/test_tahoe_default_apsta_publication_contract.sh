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
iwm = (root / "itlwm/hal_iwm/ItlIwm.cpp").read_text()
iwm_scan = (root / "itlwm/hal_iwm/scan.cpp").read_text()
iwx = (root / "itlwm/hal_iwx/ItlIwx.cpp").read_text()

assert "IOReturn publishDefaultAPSTAInterface();" in header
assert "void publishInitialBSDInterfaces();" in header

publish = controller[
    controller.index("IOReturn AirportItlwm::publishDefaultAPSTAInterface()"):
    controller.index("void AirportItlwm::publishInitialBSDInterfaces()")
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
assert start.index("markLifecycleLive()") < start.index(
    "publishInitialBSDInterfaces();") < start.index("\n    registerService();")
assert "fNetIf->deferBSDAttach(false);" not in start
initial = controller[controller.index("void AirportItlwm::publishInitialBSDInterfaces()"):
    controller.index("IOReturn AirportItlwm::materializeAPSTAInterface(")]
assert initial.index("publishDefaultAPSTAInterface();") < initial.index(
    "fNetIf->deferBSDAttach(false);")
assert "ifnet_find_by_name(name, &visible)" in initial
assert "visible == fAPSTANetIf->getBSDInterface()" in initial
assert "ifnet_release(visible)" in initial

boot = controller[
    controller.index("void AirportItlwm::performTahoeBootChipImage()"):
    controller.index("bool AirportItlwmBootNub::start(IOService *provider)")
]
assert "publishDefaultAPSTAInterface();" not in boot, (
    "APSTA capability must not be queried before asynchronous IWN init")

for (family, source, scan_source, attach_start, attach_end, scan_marker,
     scan_end_marker) in (
    (
        "IWM",
        iwm,
        iwm_scan,
        "bool ItlIwm::\nattach(IOPCIDevice *device)",
        "void ItlIwm::\nfree()",
        "iwm_scan(struct iwm_softc *sc, const ItlStateTransitionRequest &request)",
        "int ItlIwm::\niwm_bgscan(",
    ),
    (
        "IWX",
        iwx,
        iwx,
        "bool ItlIwx::attach(IOPCIDevice *device)",
        "void ItlIwx::\ndetach(IOPCIDevice *device)",
        "iwx_scan(struct iwx_softc *sc, const ItlStateTransitionRequest &request)",
        "int ItlIwx::\niwx_bgscan(",
    ),
):
    attach = source[source.index(attach_start):source.index(attach_end)]
    assert "wclScanNeedsReopen = true;" in attach, (
        f"{family} must arm its initial lower-radio-ready publication")
    assert "first committed SCAN state" in attach, (
        f"{family} initial APSTA publication rationale is missing")

    note_start = source.index("noteWclScanRadioReady(uint64_t serial)")
    note_end = source.index("claimWclScanTerminal(", note_start)
    note = source[note_start:note_end]
    for needle in (
        "if (scanCommand.current(serial, com.sc_generation) && wclScanNeedsReopen)",
        "wclScanNeedsReopen = false",
        "IEEE80211_EVT_WCL_SCAN_REOPENED",
    ):
        assert needle in note, (
            f"{family} one-shot lower-radio-ready edge missing: {needle}")

    scan_start = scan_source.index(scan_marker)
    scan = scan_source[
        scan_start:scan_source.index(scan_end_marker, scan_start)
    ]
    assert scan.index("ic->ic_state = IEEE80211_S_SCAN") < scan.index(
        "noteWclScanRadioReady(scanSerial)"), (
            f"{family} must publish only after committing SCAN")

print("PASS: Tahoe publishes firmware-admitted AP before primary BSD discovery and rechecks on lower ready")
PY
