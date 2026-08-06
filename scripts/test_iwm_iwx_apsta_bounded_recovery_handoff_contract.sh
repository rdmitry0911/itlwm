#!/usr/bin/env bash
# Prove that a retained AP receives the radio after a bounded untagged STA
# scan, and that its credential generation or ordinary census remains yielded
# for the AP lifetime and resumes only after lower teardown.
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

python3 - "$root" <<'PY'
from pathlib import Path
import re
import sys


root = Path(sys.argv[1])


def body(text: str, signature: str) -> str:
    match = re.search(
        r"^" + re.escape(signature) + r"\s*\([^;{}]*\)\s*(?:const\s*)?\{",
        text,
        re.M | re.S,
    )
    if match is None:
        raise AssertionError(f"missing function: {signature}")
    opening = text.rfind("{", match.start(), match.end())
    depth = 0
    for index in range(opening, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[opening + 1:index]
    raise AssertionError(f"unterminated function: {signature}")


def ordered(text: str, *tokens: str) -> None:
    cursor = 0
    for token in tokens:
        position = text.find(token, cursor)
        if position < 0:
            raise AssertionError(f"missing ordered token: {token}")
        cursor = position + len(token)


hal = (root / "include/HAL/ItlHalService.hpp").read_text()
owner = (root / "AirportItlwm/AirportItlwmAPSTAOwner.cpp").read_text()
owner_h = (root / "AirportItlwm/AirportItlwmAPSTAOwner.hpp").read_text()
iwm = (root / "itlwm/hal_iwm/ItlIwm.cpp").read_text()
iwm_mac = (root / "itlwm/hal_iwm/mac80211.cpp").read_text()
iwm_scan = (root / "itlwm/hal_iwm/scan.cpp").read_text()
iwm_h = (root / "itlwm/hal_iwm/ItlIwm.hpp").read_text()
iwx = (root / "itlwm/hal_iwx/ItlIwx.cpp").read_text()
iwx_h = (root / "itlwm/hal_iwx/ItlIwx.hpp").read_text()
iwn = (root / "itlwm/hal_iwn/ItlIwn.cpp").read_text()
node_h = (root / "itl80211/openbsd/net80211/ieee80211_node.h").read_text()

bridge = "airportItlwmHandoffPrimaryStaRecoveryScanToAP"
assert f'extern "C" IOReturn {bridge}(' in hal
assert f"virtual IOReturn {bridge}" not in hal, "handoff must not shift HAL vtable"
assert "radioResetPrimaryStaScanHandoff" in owner_h
assert "bool initialHostAPAdmissionPending;" in owner_h
assert "bool confirmedHostAPStartPending;" in owner_h
publish_start = owner_h.index("bool shouldPublishPrimaryOpMode() const")
publish_end = owner_h.index("const char *bsdName()", publish_start)
publish = owner_h[publish_start:publish_end]
assert "return isApRunning();" in publish
assert "initialHostAPAdmissionPending" not in publish
assert "confirmedHostAPStartPending" not in publish

resume = body(owner, "IOReturn AirportItlwmAPSTAOwner::resumeAfterRadioReset")
ordered(
    resume,
    "isPrimaryStaRecoveryScanPending()",
    "const bool publicHostAPStartPending =",
    "initialHostAPAdmissionPending ||",
    "confirmedHostAPStartPending",
    "const bool waitForRetainedPrimary =",
    "!publicHostAPStartPending",
    "kAirportItlwmAPSTARadioResetPrimaryStaWaitTicks",
    "const bool foregroundScanShouldYield =",
    f"{bridge}(",
    "radioResetPrimaryStaScanHandoff =",
    "const IOReturn result = startLowerIfReady();",
)
assert "handoffResult != kIOReturnSuccess" in resume
assert "handoffResult != kIOReturnUnsupported" in resume
assert "!radioResetPrimaryStaScanHandoff" in resume
assert "initialHostAPAdmissionPending ||" in resume
ordered(
    resume,
    "const bool asynchronousPublicStart =",
    "initialHostAPAdmissionPending = false;",
    "confirmedHostAPStartPending = false;",
    "APSTA asynchronous public HostAP start reached lower",
)

hostap = body(owner, "IOReturn AirportItlwmAPSTAOwner::setHostAPMode")
ordered(
    hostap,
    "const IOReturn stopResult = stopLower();",
    "apsta_lower_stop_pending(stopResult)",
    "accepted asynchronous HostAP stop pending lower",
    "if (isApRunning())",
)
ordered(
    hostap,
    "if (isApRunning())",
    "confirmedHostAPStartPending = true;",
    "if (lowerStopPending)",
    "driveLowerStopToTerminal()",
    "const IOReturn result = startLowerIfReady();",
    "radioResetResumePending = true;",
    "initialHostAPAdmissionPending = !confirmedHostAPStartPending;",
)
assert "not require that repetition to publish SWAP" in hostap
assert "queued confirmed HostAP replacement behind" in hostap
assert "lower stop result=" in hostap

retained = body(
    owner,
    "void AirportItlwmAPSTAOwner::prepareRetainedLowerReset",
)
assert "initialHostAPAdmissionPending = false;" in retained
assert "confirmedHostAPStartPending = false;" in retained

assert "IEEE80211_SCAN_COMPLETION_AP_HANDOFF" in node_h

for family, source, header, flags, phase, abort in (
    ("IWM", iwm, iwm_h, "IWM", "ItlIwmWclScanPhase::Idle", "iwm_umac_scan_abort(sc)"),
    ("IWX", iwx, iwx_h, "IWX", "ItlIwxWclScanPhase::Idle", "iwx_umac_scan_abort(sc)"),
):
    assert "bool apPrimaryStaRecoveryScanAbortPending;" in header
    assert "bool apPrimaryStaRecoveryScanYielded;" in header
    assert "bool apPrimaryStaRecoveryScanGeneric;" in header
    assert "uint64_t apPrimaryStaRecoveryScanGeneration;" in header
    handoff = body(source, f"IOReturn Itl{family.title()}::\nhandoffPrimaryStaRecoveryScanToAP")
    for token in (
        phase,
        f"{flags}_FLAG_BGSCAN",
        f"{flags}_FLAG_SCANNING",
        "sc_sae_bss_loss_recovery_armed",
        "sc_sae_bss_loss_recovery_generation",
        "sc_sae_wcl_credential.request_generation",
        "itl_sae_wcl_credential_is_well_formed",
        "const bool scanGeneric = generation == 0",
        abort,
        "apPrimaryStaRecoveryScanGeneration = generation",
        "apPrimaryStaRecoveryScanGeneric = scanGeneric",
        "apPrimaryStaRecoveryScanAbortPending = true",
    ):
        assert token in handoff, f"{family} handoff lacks {token}"
    ordered(
        handoff,
        "apPrimaryStaRecoveryScanAbortPending = true",
        f"if ((sc->sc_flags & {flags}_FLAG_SCANNING) == 0)",
        "apPrimaryStaRecoveryScanYielded = true",
        abort,
    )

    terminal = body(
        source,
        f"bool Itl{family.title()}::\ncompletePrimaryStaRecoveryScanAPHandoff",
    )
    ordered(
        terminal,
        "apPrimaryStaRecoveryScanAbortPending",
        "generic = apPrimaryStaRecoveryScanGeneric",
        f"{flags}_FLAG_SCANNING",
        "IEEE80211_SCAN_COMPLETION_AP_HANDOFF",
        "apPrimaryStaRecoveryScanYielded = true",
    )

    restart = body(
        source,
        f"void Itl{family.title()}::\nresumePrimaryStaRecoveryScanAfterAPHandoff",
    )
    ordered(
        restart,
        "apPrimaryStaRecoveryScanGeneration",
        "sc_sae_bss_loss_recovery_generation == generation",
        "ic->ic_state != IEEE80211_S_SCAN",
        f"{flags}_FLAG_SCANNING",
        "ieee80211_new_state(ic, IEEE80211_S_SCAN, -1)",
    )
    assert "bool isAPScanFenceActive() const;" in header

iwm_scan_owner = body(iwm_scan, "int ItlIwm::\niwm_scan")
ordered(
    iwm_scan_owner,
    "if (isAPScanFenceActive())",
    "noteWclInitialScanCommandRejected()",
    "IWM STA scan deferred by live AP radio fence",
    "return 0",
    "iwm_umac_scan",
)
iwm_bgscan_owner = body(iwm_scan, "int ItlIwm::\niwm_bgscan")
ordered(
    iwm_bgscan_owner,
    "that->isAPScanFenceActive()",
    "return EBUSY",
    "iwm_umac_scan",
)
iwx_scan_owner = body(iwx, "int ItlIwx::\niwx_scan")
ordered(
    iwx_scan_owner,
    "if (isAPScanFenceActive())",
    "noteWclInitialScanCommandRejected()",
    "IWX STA scan deferred by live AP radio fence",
    "return 0",
    "iwx_umac_scan",
)
iwx_bgscan_owner = body(iwx, "int ItlIwx::\niwx_bgscan")
ordered(
    iwx_bgscan_owner,
    "that->isAPScanFenceActive()",
    "return EBUSY",
    "iwx_umac_scan",
)

for source, family in ((iwm, "Iwm"), (iwx, "Iwx")):
    initial = body(source, f"IOReturn Itl{family}::\nbeginWclInitialScan")
    background = body(source, f"IOReturn Itl{family}::\nbeginWclBackgroundScan")
    assert "if (isAPScanFenceActive())" in initial
    assert "if (isAPScanFenceActive())" in background

iwm_start = body(iwm, "IOReturn ItlIwm::\nstartAPMode")
ordered(
    iwm_start,
    "iwm_start_ap_resources",
    "if (error != 0)",
    "resumePrimaryStaRecoveryScanAfterAPHandoff",
)
assert iwm_start.count("resumePrimaryStaRecoveryScanAfterAPHandoff") == 5
iwm_stop = body(iwm, "IOReturn ItlIwm::\nstopAPMode")
ordered(
    iwm_stop,
    "iwm_stop_ap_resources",
    "if (error != 0)",
    "resumePrimaryStaRecoveryScanAfterAPHandoff",
)
iwm_lower = body(iwm_mac, "int ItlIwm::\niwm_start_ap_resources")
assert "ic_state == IEEE80211_S_SCAN" in iwm_lower
assert "apPrimaryStaRecoveryScanYielded" in iwm_lower

iwx_worker = body(iwx, "static void\niwx_ap_start_task")
ordered(iwx_worker, "iwx_start_ap_mode", "resumePrimaryStaRecoveryScanAfterAPHandoff")
iwx_restart = body(
    iwx,
    "void ItlIwx::\nresumePrimaryStaRecoveryScanAfterAPHandoff",
)
ordered(iwx_restart, "if (apLowerRunning)", "apPrimaryStaRecoveryScanYielded")
iwx_stop_worker = body(iwx, "static void\niwx_ap_stop_task")
ordered(
    iwx_stop_worker,
    "apLowerRunning = false",
    "IWX AP lower stop worker complete",
    "resumePrimaryStaRecoveryScanAfterAPHandoff",
)
iwx_handoff = body(iwx, "IOReturn ItlIwx::\nhandoffPrimaryStaRecoveryScanToAP")
ordered(
    iwx_handoff,
    "if (apStartResultValid)",
    "apStartResultValid = false",
    "priorResult != kIOReturnBusy",
    "iwx_umac_scan_abort(sc)",
)
iwx_lower = body(iwx, "int ItlIwx::\niwx_start_ap_mode")
for token in (
    "ic_state == IEEE80211_S_SCAN",
    "recoveryScanYielded",
    "scanFlags == 0",
):
    assert token in iwx_lower

dispatch = body(iwn, f'extern "C" IOReturn\n{bridge}')
assert "airportItlwmHandoffIwmPrimaryStaRecoveryScanToAP" in dispatch
assert "airportItlwmHandoffIwxPrimaryStaRecoveryScanToAP" in dispatch

print("PASS: IWM/IWX bounded foreground-scan handoff owns AP lifetime then resumes STA")
PY
