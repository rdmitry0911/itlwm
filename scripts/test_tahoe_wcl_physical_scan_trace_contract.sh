#!/usr/bin/env bash
# Contract for the identity-free trace of the real IWN-owned WCL physical
# scan path.  This is deliberately a proof of driver ownership boundaries,
# not a claim about a particular access point or consumer-side delivery.
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

bash "$root/scripts/test_payload_builders.sh"

python3 - "$root" <<'PY'
from pathlib import Path
import re
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

# Queued/STARTED/REJECTED are lower lifecycle edges, not new post-PLTI trace
# facts.  Keep v7's WCL vocabulary closed: a queued handoff that never owns a
# command must remain a request-only, diagnostic trajectory.
fixed_wcl_trace_events = {
    "kAirportItlwmPostPltiTraceEventWclPhysicalScanRequestAccepted",
    "kAirportItlwmPostPltiTraceEventWclPhysicalScanLowerLeaseReserved",
    "kAirportItlwmPostPltiTraceEventWclPhysicalScanTerminalComplete",
    "kAirportItlwmPostPltiTraceEventWclPhysicalScanResultPublicationIssued",
    "kAirportItlwmPostPltiTraceEventWclPhysicalScanTerminalAborted",
    "kAirportItlwmPostPltiTraceEventWclPhysicalScanDonePublicationIssued",
}
defined_wcl_trace_events = set(re.findall(
    r"^\s*(kAirportItlwmPostPltiTraceEventWclPhysicalScan[A-Za-z0-9_]*)\s*=",
    abi, re.MULTILINE))
if defined_wcl_trace_events != fixed_wcl_trace_events:
    fail("Queued/STARTED/REJECTED expanded the fixed WCL trace vocabulary")
for token in (
        "kAirportItlwmPostPltiTraceEventWclPhysicalScanQueued",
        "kAirportItlwmPostPltiTraceEventWclPhysicalScanStarted",
        "kAirportItlwmPostPltiTraceEventWclPhysicalScanStartRejected",
):
    forbid(abi, token, "unapproved WCL lifecycle trace event")
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

# The upper request begins only after its own reservation.  Initial discovery
# may queue behind the boot generic foreground lease, but queue admission is
# not a WCL lower lease and must not manufacture a false terminal fact.
scan_request = body(sky, "IOReturn AirportItlwmSkywalkInterface::\nsetWCL_SCAN_REQ",
                    "WCL physical scan request")
for token in (
        "bool initialForeground = false;",
        "ic->ic_state == IEEE80211_S_SCAN",
        "initialForeground = true;",
        "fHalService->beginWclInitialScan",
        "fHalService->beginWclBackgroundScan",
):
    require(scan_request, token, "initial/background WCL request split")
ordered(scan_request, "upper request then initial lower admission",
        "initialForeground = true;", "reserveWclPhysicalScan",
        "reserveResult != kIOReturnSuccess",
        "AirportItlwmPostPltiTraceBeginWclPhysicalScanEpisode(ic);",
        "const IOReturn beginResult = initialForeground ?",
        "fHalService->beginWclInitialScan")
queued_initial_request = body(
    scan_request,
    "if (initialForeground && beginResult == kIOReturnSuccess",
    "queued initial WCL request")
require(scan_request,
        "if (initialForeground && beginResult == kIOReturnSuccess &&\n"
        "        backendGeneration == 0)",
        "zero-backend queued initial condition")
for token in (
        "instance->queueWclInitialPhysicalScan(generation)",
        "StartDisposition::Active",
        "return kIOReturnSuccess",
):
    require(queued_initial_request, token, "queued initial WCL handoff")
for token in (
        "AirportItlwmPostPltiTraceRecordWclPhysicalScan",
        "kAirportItlwmPostPltiTraceEventWclPhysicalScanLowerLeaseReserved",
        "AirportItlwmPostPltiTraceAbortWclPhysicalScanEpisode",
):
    forbid(queued_initial_request, token,
           "queue admission claiming a physical WCL boundary")
forbid(scan_request, "AirportItlwmPostPltiTraceAbortWclPhysicalScanEpisode",
       "false pre-terminal abort producer")

# Queueing is permitted only behind an exact live generic foreground lease.
# It leaves the post-PLTI episode request-only until replay reserves a fresh
# WCL_INITIAL lease.
initial_queue = body(iwn,
    "iwn_wcl_initial_scan_queue(struct iwn_softc *sc, u_int64_t generation,",
    "IWN initial WCL queue admission")
for token in (
        "IWN_SCAN_LEASE_GENERIC_FOREGROUND",
        "sc->sc_scan_lease.command_submitted",
        "sc->sc_scan_lease.phase == IWN_SCAN_LEASE_ACTIVE",
        "sc->sc_wcl_initial_scan_pending.queued = true",
):
    require(initial_queue, token, "exact generic-to-WCL queue fence")
for token in (
        "AirportItlwmPostPltiTraceRecordWclPhysicalScan",
        "kAirportItlwmPostPltiTraceEventWclPhysicalScanLowerLeaseReserved",
        "IEEE80211_EVT_WCL_SCAN_STARTED",
        "IEEE80211_EVT_WCL_SCAN_START_REJECTED",
):
    forbid(initial_queue, token, "queue admission lifecycle publication")

initial_replay = body(iwn, "void ItlIwn::\niwn_scan_lease_replay_task",
                      "IWN initial WCL replay")
ordered(initial_replay, "retired generic lease precedes fresh WCL lease",
        "!iwn_scan_lease_live_locked(sc)", "terminal_handoff_ready",
        "iwn_scan_start", "IWN_SCAN_LEASE_WCL_INITIAL")
for token in (
        "reject_initial = error != 0 && !command_started",
        "IEEE80211_EVT_WCL_SCAN_START_REJECTED",
):
    require(initial_replay, token, "no-doorbell initial rejection")
for token in (
        "AirportItlwmPostPltiTraceRecordWclPhysicalScan",
        "AirportItlwmPostPltiTraceCompleteWclPhysicalScanEpisode",
        "AirportItlwmPostPltiTraceAbortWclPhysicalScanEpisode",
        "IEEE80211_EVT_WCL_SCAN_TERMINAL",
):
    forbid(initial_replay, token, "rejected queued handoff terminal producer")

scan_start = body(iwn, "int ItlIwn::\niwn_scan_start",
                  "IWN physical scan lease start")
ordered(scan_start, "lower lease precedes any submit boundary",
        "iwn_scan_lease_reserve", "AirportItlwmPostPltiTraceRecordWclPhysicalScan",
        "kAirportItlwmPostPltiTraceEventWclPhysicalScanLowerLeaseReserved",
        "iwn_scan_lease_arm_submission", "iwn_scan_submit")

# STARTED is deliberately a net80211 lifecycle edge, emitted after WRPTR
# while the exact WCL_INITIAL lease lock still excludes a raced STOP_SCAN.
scan_submit = body(iwn, "int ItlIwn::\niwn_scan_submit",
                   "IWN scan command submit")
ordered(scan_submit, "WCL initial post-doorbell hook",
        "iwn_cmd_with_doorbell_hook", "iwn_scan_lease_prepare_doorbell",
        "iwn_scan_lease_finish_doorbell")
finish_doorbell = body(iwn,
    "iwn_scan_lease_finish_doorbell(struct iwn_softc *sc, void *opaque)",
    "IWN initial WCL post-doorbell start")
for token in (
        "The WRPTR write is now complete",
        "sc->sc_scan_lease.owner == IWN_SCAN_LEASE_WCL_INITIAL",
        "sc->sc_scan_lease.command_submitted",
        "sc->sc_scan_lease.wcl_initial_started = true",
        "IEEE80211_EVT_WCL_SCAN_STARTED",
):
    require(finish_doorbell, token, "post-WRPTR WCL STARTED boundary")
ordered(finish_doorbell, "STARTED precedes release of exact lease fence",
        "sc->sc_scan_lease.wcl_initial_started = true",
        "publish_wcl_initial_started = true",
        "IEEE80211_EVT_WCL_SCAN_STARTED",
        "IOSimpleLockUnlock(context->lock)")

started_event = body(v2, "if (msgCode == IEEE80211_EVT_WCL_SCAN_STARTED)",
                     "upper WCL STARTED event")
ordered(started_event, "STARTED activates before a raced terminal publish",
        "activateWclPhysicalScan", "StartDisposition::TerminalPending",
        "queueWclPhysicalScanTerminalPublication")

# START_REJECTED is a no-doorbell cleanup edge.  It may clear a queued upper
# ticket, but it cannot synthesize any lower terminal/result/DONE trace fact.
rejected_event = body(v2,
    "if (msgCode == IEEE80211_EVT_WCL_SCAN_START_REJECTED)",
    "upper WCL START_REJECTED event")
require(rejected_event, "rejectWclInitialPhysicalScanStart",
        "START_REJECTED reducer cleanup")
for token in (
        "queueWclPhysicalScanTerminalPublication",
        "finishWclPhysicalScanCompletion",
        "postMessage",
        "APPLE80211_M_WCL_SCAN_RESULT",
        "APPLE80211_M_WCL_SCAN_DONE",
        "AirportItlwmPostPltiTraceRecordWclPhysicalScan",
        "AirportItlwmPostPltiTraceCompleteWclPhysicalScanEpisode",
        "AirportItlwmPostPltiTraceAbortWclPhysicalScanEpisode",
        "IEEE80211_EVT_WCL_SCAN_TERMINAL",
):
    forbid(rejected_event, token, "START_REJECTED synthetic terminal path")

initial_reset = body(iwn,
    "static enum iwn_scan_lease_owner\niwn_scan_lease_begin_hardware_invalidation(",
    "queued initial WCL reset")
for token in (
        "queued handoff has not doorbelled a WCL command",
        "zero-backend INVALIDATED event",
        "*queued_initial_rejected_generation =",
        "iwn_wcl_initial_scan_pending_clear_locked(sc)",
):
    require(initial_reset, token, "queued reset rejection fence")
for token in (
        "AirportItlwmPostPltiTraceRecordWclPhysicalScan",
        "AirportItlwmPostPltiTraceCompleteWclPhysicalScanEpisode",
        "AirportItlwmPostPltiTraceAbortWclPhysicalScanEpisode",
        "IEEE80211_EVT_WCL_SCAN_TERMINAL",
):
    forbid(initial_reset, token, "queued reset terminal producer")
hardware_stop = body(iwn, "void ItlIwn::\niwn_hw_stop",
                     "IWN hardware stop")
ordered(hardware_stop, "queued reset emits rejection rather than terminal",
        "queued_initial_rejected_generation != 0",
        "IEEE80211_EVT_WCL_SCAN_START_REJECTED")

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

# Skywalk publishes its actual BSD endpoint on the AirportItlwm controller.
# The client must consume that exact controller property, never guess a global
# CoreWLAN interface name.
require(sky, 'instance->setProperty("BSD Name", value)',
        "AirportItlwm controller BSD endpoint publication")
endpoint_resolver = body(client, "copy_airport_itlwm_bsd_name",
                         "AirportItlwm controller BSD endpoint resolver")
for token in (
        'copy_property(service, "BSD Name")',
        "CFStringGetCString",
        "kCFStringEncodingUTF8",
        "name[0] != 'e'",
        "name[1] != 'n'",
        "name[index] < '0' || name[index] > '9'",
        "memset(name, 0, capacity)",
):
    require(endpoint_resolver, token, "bounded controller BSD endpoint resolver")
forbid(client, '@"en1"', "hard-coded CoreWLAN endpoint")
forbid(client, "interfaceWithName:@", "literal CoreWLAN endpoint")

# The only stimulus is fixed, undirected CoreWLAN scan enumeration.  It
# accepts no network input and reports no per-network field or NSError text.
stimulus = body(client, "scan_wcl_physical(io_service_t service)",
                "fixed WCL physical scan stimulus")
for token in (
        "@autoreleasepool",
        "copy_airport_itlwm_bsd_name(service, endpoint_name,",
        "endpoint_binding = \"airport-itlwm-bsd\";",
        "[client interfaceWithName:endpoint]",
        "[interface scanForNetworksWithName:nil error:NULL]",
        "[network wlanChannel]",
        "[channel channelBand]",
        "wcl_physical_scan_stimulus=%s endpoint_binding=%s total=%u",
        "band_5ghz=%u band_6ghz=%u band_other=%u",
        "return strcmp(outcome, \"ok\") == 0 ? 0 : 1;",
):
    require(stimulus, token, "fixed aggregate-only WCL scan stimulus")
ordered(stimulus, "controller endpoint resolution precedes CoreWLAN lookup",
        "copy_airport_itlwm_bsd_name(service, endpoint_name,",
        "endpoint_binding = \"airport-itlwm-bsd\";",
        "[client interfaceWithName:endpoint]")
require(stimulus, "outcome, endpoint_binding, total",
        "categorical endpoint binding output")
forbid(stimulus, "outcome, endpoint_name, total",
       "raw endpoint output")
require(client, "scan_wcl_physical(service)",
        "same AirportItlwm controller passed to physical scan stimulus")
for token in (
        "[network ssid]", "[network bssid]", "[network rssiValue]",
        "[network security]", "[network informationElement]",
        "localizedDescription", "error.code", "setPower", "associate",
        "disassociate", "setConfiguration", "networkName",
):
    forbid(stimulus, token, "identity or mutation in WCL scan stimulus")
for token in (
        "-x objective-c", "-framework CoreWLAN", "-framework Foundation",
):
    require(build, token, "receipt-bound CoreWLAN trace-client build")

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
