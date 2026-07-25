#!/usr/bin/env bash
# Contract for the identity-free trace of the real IWN-owned WCL physical
# scan path.  This is deliberately a proof of driver ownership boundaries,
# not a claim about a particular access point or consumer-side delivery.
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

bash "$root/scripts/test_payload_builders.sh"

python3 - "$root" <<'PY'
from pathlib import Path
import sys


root = Path(sys.argv[1])
abi = (root / "include/ClientKit/AirportItlwmPostPltiTrace.h").read_text()
bridge = (root / "include/ClientKit/AirportItlwmPostPltiTraceBridge.h").read_text()
contracts = (root / "include/ClientKit/AirportItlwmWclPhysicalScanTraceContracts.h").read_text()
v2 = (root / "AirportItlwm/AirportItlwmV2.cpp").read_text()
sky = (root / "AirportItlwm/AirportItlwmSkywalkInterface.cpp").read_text()
iwn = (root / "itlwm/hal_iwn/ItlIwn.cpp").read_text()
client = (root / "AirportItlwmPostPltiTrace/airport_itlwm_post_plti_trace.c").read_text()
build = (root / "scripts/build_post_plti_trace.sh").read_text()
aggregate = (root / "scripts/test_tahoe_sae_quarantine_contract.sh").read_text()


def fail(message):
    raise SystemExit(f"WCL physical-scan trace contract: {message}")


def require(text, needle, label):
    if needle not in text:
        fail(f"missing {label}: {needle}")


def forbid(text, needle, label):
    if needle in text:
        fail(f"unexpected {label}: {needle}")


def ordered(text, label, *needles):
    cursor = 0
    for needle in needles:
        pos = text.find(needle, cursor)
        if pos < 0:
            fail(f"{label} missing ordered token: {needle}")
        cursor = pos + len(needle)


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


for token in (
        "AIRPORT_ITLWM_POST_PLTI_TRACE_ABI_VERSION 7U",
        "kAirportItlwmPostPltiTraceEventWclPhysicalScanRequestAccepted = 59",
        "kAirportItlwmPostPltiTraceEventWclPhysicalScanLowerLeaseReserved = 60",
        "kAirportItlwmPostPltiTraceEventWclPhysicalScanTerminalComplete = 61",
        "kAirportItlwmPostPltiTraceEventWclPhysicalScanResultPublicationIssued = 62",
        "kAirportItlwmPostPltiTraceEventWclPhysicalScanTerminalAborted = 63",
        "kAirportItlwmPostPltiTraceEventWclPhysicalScanDonePublicationIssued = 64",
        "kAirportItlwmPostPltiTraceEventMax = 65",
        "AIRPORT_ITLWM_POST_PLTI_TRACE_WCL_PHYSICAL_SCAN_EVENT_FIRST",
        "AIRPORT_ITLWM_POST_PLTI_TRACE_WCL_PHYSICAL_SCAN_EVENT_LAST",
):
    require(abi, token, "append-only WCL trace ABI")
for token in (
        "AirportItlwmPostPltiTraceBeginWclPhysicalScanEpisode",
        "AirportItlwmPostPltiTraceRecordWclPhysicalScan",
        "AirportItlwmPostPltiTraceCompleteWclPhysicalScanEpisode",
        "AirportItlwmPostPltiTraceAbortWclPhysicalScanEpisode",
):
    require(bridge, token, "WCL trace bridge")

# The safe evaluator permits an empty successful scan and treats the result
# publication call as optional.  It must fail closed on a second episode,
# altered record, or post-terminal append.
for token in (
        "Safe categorical evaluator for one IWN-owned physical WCL scan",
        "AirportItlwmWclPhysicalScanTraceVerdictPhysicalScanObserved",
        "AirportItlwmWclPhysicalScanTraceVerdictTerminalAborted",
        "AirportItlwmWclPhysicalScanTraceVerdictDoneNotPublished",
        "airport_itlwm_wcl_physical_scan_trace_classify_entries_with_stage",
        "airport_itlwm_wcl_physical_scan_trace_result_publication_issued",
        "episode_count != 1 || active_episode != 0",
        "WclPhysicalScanRequestAccepted",
        "WclPhysicalScanLowerLeaseReserved",
        "WclPhysicalScanTerminalComplete",
        "WclPhysicalScanResultPublicationIssued",
        "WclPhysicalScanTerminalAborted",
        "WclPhysicalScanDonePublicationIssued",
        "event == kAirportItlwmPostPltiTraceEventCaptureWindowSealed",
        "sealed || phase >= 5",
):
    require(contracts, token, "WCL trace evaluator")
ordered(contracts, "WCL positive categorical order",
        "WclPhysicalScanRequestAccepted",
        "WclPhysicalScanLowerLeaseReserved",
        "WclPhysicalScanTerminalComplete",
        "WclPhysicalScanResultPublicationIssued",
        "WclPhysicalScanDonePublicationIssued")

# The active initial fact is private recorder state.  Matching close prevents
# a late lower/publisher callback from closing an unrelated PMF/SAE episode.
for token in (
        "volatile uint32_t activeInitialEvent",
        "airportItlwmPostPltiTraceActiveInitialEventIs",
        "airportItlwmPostPltiTraceCloseActiveMatching",
        "kAirportItlwmPostPltiTraceEventWclPhysicalScanRequestAccepted",
        "kAirportItlwmPostPltiTraceEventWclPhysicalScanDonePublicationIssued",
        "kAirportItlwmPostPltiTraceEventWclPhysicalScanTerminalAborted",
        "kAirportItlwmPostPltiTraceEventUnknown",
):
    require(v2, token, "WCL trace episode fence")
begin = body(v2, "airportItlwmPostPltiTraceBeginEpisodeWithInitialEvent",
             "shared trace episode begin")
ordered(begin, "initial fact published only after token admission",
        "__atomic_compare_exchange_n(&sPostPltiTrace.activeToken",
        "__atomic_store_n(&sPostPltiTrace.activeInitialEvent, initial_event",
        "airportItlwmPostPltiTraceTokenIsCurrent")
record = body(v2, "AirportItlwmPostPltiTraceRecordWclPhysicalScan",
              "WCL trace interior recorder")
for token in (
        "airportItlwmPostPltiTraceWclPhysicalScanInteriorEvent",
        "airportItlwmPostPltiTraceProducerEnter",
        "airportItlwmPostPltiTraceTryLock",
        "airportItlwmPostPltiTraceNoteContendedProducer",
        "airportItlwmPostPltiTraceActiveInitialEventIs",
        "airportItlwmPostPltiTraceRecordToken",
):
    require(record, token, "WCL trace interior recorder")
for token in ("AirportItlwmRegDiag", "XYLog", "setProperty", "OSData",
              "OSString", "IOMalloc", "fHalService"):
    forbid(record, token, "unsafe WCL trace recorder dependency")
for marker, terminal in (
        ("AirportItlwmPostPltiTraceCompleteWclPhysicalScanEpisode",
         "kAirportItlwmPostPltiTraceEventWclPhysicalScanDonePublicationIssued"),
        ("AirportItlwmPostPltiTraceAbortWclPhysicalScanEpisode",
         "kAirportItlwmPostPltiTraceEventWclPhysicalScanTerminalAborted"),
):
    producer = body(v2, marker, marker)
    for token in (
            "airportItlwmPostPltiTraceProducerEnter",
            "airportItlwmPostPltiTraceTryLock",
            "airportItlwmPostPltiTraceNoteContendedProducer",
            "airportItlwmPostPltiTraceCloseActiveMatching",
            terminal,
            "kAirportItlwmPostPltiTraceEventWclPhysicalScanRequestAccepted",
    ):
        require(producer, token, f"fenced WCL terminal producer {marker}")

# The upper request begins only after its own reservation.  A pre-terminal
# lower-start failure must not manufacture a false TerminalAborted fact.
scan_request = body(sky, "IOReturn AirportItlwmSkywalkInterface::\nsetWCL_SCAN_REQ",
                    "WCL physical scan request")
ordered(scan_request, "upper request then lower ownership",
        "reserveWclPhysicalScan", "reserveResult != kIOReturnSuccess",
        "AirportItlwmPostPltiTraceBeginWclPhysicalScanEpisode(ic);",
        "beginWclBackgroundScan")
forbid(scan_request, "AirportItlwmPostPltiTraceAbortWclPhysicalScanEpisode",
       "false pre-terminal abort producer")

scan_start = body(iwn, "int ItlIwn::\niwn_scan_start",
                  "IWN physical scan lease start")
ordered(scan_start, "lower lease precedes any submit boundary",
        "iwn_scan_lease_reserve", "AirportItlwmPostPltiTraceRecordWclPhysicalScan",
        "kAirportItlwmPostPltiTraceEventWclPhysicalScanLowerLeaseReserved",
        "iwn_scan_lease_arm_submission", "iwn_scan_submit")
stop_scan = body(iwn, "case IWN_STOP_SCAN", "IWN STOP_SCAN terminal")
ordered(stop_scan, "exact lower terminal precedes generic cleanup",
        "iwn_scan_lease_claim_terminal", "if (terminal.wcl)",
        "AirportItlwmPostPltiTraceAbortWclPhysicalScanEpisode",
        "AirportItlwmPostPltiTraceRecordWclPhysicalScan",
        "ieee80211_end_scan")
require(stop_scan,
        "kAirportItlwmPostPltiTraceEventWclPhysicalScanTerminalComplete",
        "normal lower terminal fact")
require(stop_scan,
        "terminal.publish_wcl_terminal",
        "separate upward WCL terminal publication guard")

# Publication facts are placed only after the corresponding void postMessage
# call.  They establish issued calls under ownership, never delivery.
publisher = body(v2, "IOReturn AirportItlwm::\npostWclPhysicalScanCompletionGated",
                 "gated WCL physical scan publisher")
for token in (
        "bool resultPublicationIssued = false",
        "postMessage() is void",
        "ownsWclPhysicalScanCompletion",
        "WclPhysicalScanResultPublicationIssued",
        "IEEE80211_WCL_SCAN_TERMINAL_STATUS_COMPLETE",
        "AirportItlwmPostPltiTraceCompleteWclPhysicalScanEpisode",
):
    require(publisher, token, "WCL publication trace boundary")
ordered(publisher, "result trace follows issued result call",
        "APPLE80211_M_WCL_SCAN_RESULT",
        "resultPublicationIssued = true;",
        "AirportItlwmPostPltiTraceRecordWclPhysicalScan")
ordered(publisher, "positive terminal trace follows issued DONE call",
        "APPLE80211_M_WCL_SCAN_DONE",
        "terminalStatus == IEEE80211_WCL_SCAN_TERMINAL_STATUS_COMPLETE",
        "AirportItlwmPostPltiTraceCompleteWclPhysicalScanEpisode")

for token in (
        "#include <ClientKit/AirportItlwmWclPhysicalScanTraceContracts.h>",
        "get_iwn_wcl_physical_scan_report",
        "iwn-wcl-physical-scan-report",
        "iwn_wcl_physical_scan_verdict=%s first_missing_stage=%s",
        "result_publication_issued=%u",
        "IWN_WCL_PHYSICAL_SCAN_OBSERVED",
        "postMessage() is void",
):
    require(client + publisher, token, "safe WCL runtime report vocabulary")
report = body(client, "get_iwn_wcl_physical_scan_report",
              "safe WCL physical scan report")
for token in ("ssid", "bssid", "rssi", "channel", "payload", "credential",
              "IORegistryEntrySetCFProperty"):
    forbid(report.lower(), token, "identity-bearing WCL runtime report")

for token in (
        "require_external_bridge ItlIwn AirportItlwmPostPltiTraceRecordWclPhysicalScan",
        "require_external_bridge ItlIwn AirportItlwmPostPltiTraceAbortWclPhysicalScanEpisode",
        "require_external_bridge AirportItlwmSkywalkInterface AirportItlwmPostPltiTraceBeginWclPhysicalScanEpisode",
):
    require(build, token, "external Tahoe WCL trace bridge proof")
for token in (
        "test_tahoe_wcl_physical_scan_lifecycle_contract.sh",
        "test_tahoe_wcl_physical_scan_trace_contract.sh",
):
    require(aggregate, token, "SAE aggregate WCL gate")

print("WCL physical-scan trace contract OK")
PY
