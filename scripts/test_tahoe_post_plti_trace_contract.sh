#!/usr/bin/env bash
# Contract for the isolated, safe-only post-PLTI association trace.
#
# It admits only a categorical, one-episode IWN trace.  A successful runtime
# scenario still needs a separate sanitized evidence record after this source
# contract and the Tahoe build gate pass.
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
shared = (root / "include/ClientKit/AirportItlwmPostPltiTraceContracts.h").read_text()
facade = (root / "AirportItlwm/TahoePostPltiTraceContracts.hpp").read_text()
v2 = (root / "AirportItlwm/AirportItlwmV2.cpp").read_text()
sky = (root / "AirportItlwm/AirportItlwmSkywalkInterface.cpp").read_text()
iwn = (root / "itlwm/hal_iwn/ItlIwn.cpp").read_text()
iwnvar = (root / "itlwm/hal_iwn/if_iwnvar.h").read_text()
node = (root / "itl80211/openbsd/net80211/ieee80211_node.c").read_text()
output = (root / "itl80211/openbsd/net80211/ieee80211_output.c").read_text()
input_source = (root / "itl80211/openbsd/net80211/ieee80211_input.c").read_text()
proto = (root / "itl80211/openbsd/net80211/ieee80211_proto.c").read_text()
pae_output = (root / "itl80211/openbsd/net80211/ieee80211_pae_output.c").read_text()
pae_input = (root / "itl80211/openbsd/net80211/ieee80211_pae_input.c").read_text()
auth = (root / "AirportItlwm/TahoeAssociationAuthContracts.hpp").read_text()
client = (root / "AirportItlwmPostPltiTrace/airport_itlwm_post_plti_trace.c").read_text()
build = (root / "scripts/build_post_plti_trace.sh").read_text()
runner = (root / "scripts/run_tahoe_sae_quarantine_layer.sh").read_text()
aggregate = (root / "scripts/test_tahoe_sae_quarantine_contract.sh").read_text()
payload_script = (root / "scripts/test_payload_builders.sh").read_text()
payload_test = (root / "tests/tahoe_payload_builders_test.cpp").read_text()


def fail(message):
    raise SystemExit(f"post-PLTI safe trace contract: {message}")


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


def struct_block(name):
    marker = f"typedef struct {name}"
    start = abi.find(marker)
    if start < 0:
        fail(f"missing ABI struct {name}")
    end = abi.find(f"}} {name};", start)
    if end < 0:
        fail(f"unterminated ABI struct {name}")
    return abi[start:end]


for needle in (
        "AIRPORT_ITLWM_POST_PLTI_TRACE_ABI_VERSION 1U",
        "AIRPORT_ITLWM_POST_PLTI_TRACE_MAX_ENTRIES 128U",
        "AIRPORT_ITLWM_POST_PLTI_TRACE_CONTROL_PROPERTY",
        "AIRPORT_ITLWM_POST_PLTI_TRACE_CONTROL_ACK_PROPERTY",
        "AIRPORT_ITLWM_POST_PLTI_TRACE_SNAPSHOT_PROPERTY",
        "AIRPORT_ITLWM_POST_PLTI_TRACE_BUFFER_PROPERTY",
        "kAirportItlwmPostPltiTraceBackendIwn",
        "kAirportItlwmPostPltiTraceBackendUnsupported",
        "kAirportItlwmPostPltiTraceEventWclPmkReadyScanResume",
        "kAirportItlwmPostPltiTraceEventPortValidTransition",
        "kAirportItlwmPostPltiTraceEventEpisodeAborted",
        "kAirportItlwmPostPltiTraceEventMax",
):
    require(abi, needle, "safe-only public ABI")

# Every public ABI field remains a categorical/bookkeeping dword.  The public
# data layout deliberately has no byte carrier, pointer-width field, or
# identity/status field.
for name in (
        "AirportItlwmPostPltiTraceEntry",
        "AirportItlwmPostPltiTraceSnapshot",
        "AirportItlwmPostPltiTraceBuffer",
):
    fields = struct_block(name)
    forbid(fields, "uint8_t", f"identity byte carrier in {name}")
    forbid(fields, "uint64_t", f"pointer/status-width carrier in {name}")
    for banned in ("ssid", "bssid", "mac", "address", "ip", "key", "status",
                   "rssi", "channel", "packet", "firmware"):
        if re.search(rf"\b{banned}\b", fields, re.IGNORECASE):
            fail(f"identity/status field in {name}: {banned}")

entry_fields = struct_block("AirportItlwmPostPltiTraceEntry")
ordered(entry_fields, "entry ABI ordering", "sequence;", "captureGeneration;",
        "episode;", "event;")
snapshot_fields = struct_block("AirportItlwmPostPltiTraceSnapshot")
ordered(snapshot_fields, "snapshot ABI ordering", "version;", "size;",
        "captureGeneration;", "backend;", "enabled;", "targetBound;",
        "activeEpisode;", "episodeCount;", "firstSequence;", "entryCount;",
        "droppedEntries;", "latestSequence;")
buffer_fields = struct_block("AirportItlwmPostPltiTraceBuffer")
ordered(buffer_fields, "buffer ABI ordering", "version;", "captureGeneration;",
        "backend;", "entryCount;", "droppedEntries;", "firstSequence;",
        "latestSequence;", "entries[")

for needle in (
        "AirportItlwmPostPltiTraceBeginEpisode",
        "AirportItlwmPostPltiTraceRecord",
        "AirportItlwmPostPltiTraceCompleteEpisode",
        "AirportItlwmPostPltiTraceAbortEpisode",
        "AirportItlwmPostPltiTraceNoteStateRequest",
        "neither allocate, log, publish",
        "static inline void",
        "__IO80211_TARGET >= __MAC_26_0",
):
    require(bridge, needle, "safe bridge lifecycle contract")

for needle in (
        "enum AirportItlwmPostPltiTraceVerdict",
        "airport_itlwm_post_plti_trace_classify_entries",
        "kAirportItlwmPostPltiTraceVerdictBackendUnsupported",
        "kAirportItlwmPostPltiTraceVerdictKernelChainObserved",
        "backend != kAirportItlwmPostPltiTraceBackendIwn",
        "episodeCount != 1 || activeEpisode != 0",
        "entries[i].episode != episode",
        "kAirportItlwmPostPltiTraceEventEpisodeAborted",
        "kAirportItlwmPostPltiTraceEventPortValidTransition",
):
    require(shared, needle, "shared ordered C verdict evaluator")
for needle in (
        "#include <ClientKit/AirportItlwmPostPltiTraceContracts.h>",
        "inline Verdict\nclassifyEntries",
        "A mask-only caller can never establish the ordered success verdict",
):
    require(facade, needle, "C++ trace verdict facade")
mask_only = body(facade, "constexpr Verdict classify", "mask-only facade")
forbid(mask_only, "KernelChainObserved", "mask-only success verdict")

# The recorder carries one generation+episode token.  Publishing the sequence
# last is the only release publication from the hot path.
recorder = body(v2, "airportItlwmPostPltiTraceRecordToken",
                "fast-path trace recorder")
for needle in (
        "airportItlwmPostPltiTraceTokenIsCurrent(ic, token, requireActive)",
        "__atomic_add_fetch(&sPostPltiTrace.nextSequence",
        "captureFirstSequence",
        "sequence - first >= AIRPORT_ITLWM_POST_PLTI_TRACE_MAX_ENTRIES",
        "__atomic_store_n(&entry->sequence, 0, __ATOMIC_RELEASE)",
        "__atomic_store_n(&entry->captureGeneration, generation, __ATOMIC_RELAXED)",
        "__atomic_store_n(&entry->episode, episode, __ATOMIC_RELAXED)",
        "__atomic_store_n(&entry->event, event, __ATOMIC_RELAXED)",
        "__atomic_store_n(&entry->sequence, sequence, __ATOMIC_RELEASE)",
):
    require(recorder, needle, "generation/episode ring recorder")
ordered(recorder, "entry invalidated before categorical write",
        "__atomic_store_n(&entry->sequence, 0, __ATOMIC_RELEASE)",
        "__atomic_store_n(&entry->captureGeneration, generation, __ATOMIC_RELAXED)",
        "__atomic_store_n(&entry->episode, episode, __ATOMIC_RELAXED)",
        "__atomic_store_n(&entry->event, event, __ATOMIC_RELAXED)",
        "__atomic_store_n(&entry->sequence, sequence, __ATOMIC_RELEASE)")
for needle in ("AirportItlwmRegDiag", "IWX_AUTH_DIAG", "XYLog", "setProperty",
               "OSData", "OSString", "fHalService"):
    forbid(recorder, needle, "unsafe fast-path recorder dependency")

close = body(v2, "airportItlwmPostPltiTraceCloseActive",
             "safe episode close")
ordered(close, "terminal token detached before terminal event",
        "__atomic_compare_exchange_n(&sPostPltiTrace.activeToken", "0, false",
        "airportItlwmPostPltiTraceRecordToken(ic, event, token, false)")
begin = body(v2, "AirportItlwmPostPltiTraceBeginEpisode",
             "safe episode begin bridge")
for needle in (
        "activeToken", "initialScanPermitToken", "episodeCount",
        "kAirportItlwmPostPltiTraceEventWclPmkReadyScanResume",
):
    require(begin, needle, "one-token episode begin")
for needle in ("AirportItlwmRegDiag", "IWX_AUTH_DIAG", "XYLog", "setProperty",
               "OSData", "OSString"):
    forbid(begin, needle, "unsafe episode begin dependency")
note_state = body(v2, "AirportItlwmPostPltiTraceNoteStateRequest",
                  "passive trace lifecycle state hook")
for needle in (
        "oldState == IEEE80211_S_SCAN && nextState == IEEE80211_S_SCAN",
        "oldState == IEEE80211_S_SCAN && nextState == IEEE80211_S_AUTH",
        "oldState == IEEE80211_S_AUTH && nextState == IEEE80211_S_ASSOC",
        "oldState == IEEE80211_S_ASSOC && nextState == IEEE80211_S_RUN",
        "AirportItlwmPostPltiTraceAbortEpisode(ic)",
):
    require(note_state, needle, "strict state lifecycle")

proto_note = body(proto, "ieee80211_pae_assoc_epoch_note_newstate",
                  "net80211 state lifecycle hook")
ordered(proto_note, "trace state hook remains passive and pre-callback",
        "AirportItlwmPostPltiTraceNoteStateRequest", "if ((ic->ic_state")
pae_terminal = body(pae_input, "void\nieee80211_recv_4way_msg3",
                    "kernel PAE terminal transition")
require(pae_terminal, "AirportItlwmPostPltiTraceCompleteEpisode(ic)",
        "port-valid closes the trace episode")
forbid(pae_terminal, "kAirportItlwmPostPltiTraceEventPortValidTransition",
       "direct terminal append after close")

bind = body(v2, "airportItlwmPostPltiTraceBind", "categorical backend bind")
for needle in (
        "OSDynamicCast(ItlIwn, driver->fHalService)",
        "kAirportItlwmPostPltiTraceBackendIwn",
        "kAirportItlwmPostPltiTraceBackendUnsupported",
):
    require(bind, needle, "IWN-only trace backend")
admit = body(v2, "airportItlwmPostPltiTraceAdmits", "IWN trace admission")
require(admit, "kAirportItlwmPostPltiTraceBackendIwn",
        "only the implemented IWN backend admits events")
control = body(v2, "airportItlwmPostPltiTraceApplyControl",
               "trace control acknowledgement")
for needle in (
        "generation=%u backend=%u", "captureGeneration",
        "airportItlwmPostPltiTraceBind(driver)",
):
    require(control, needle, "generation-aware control acknowledgement")
publish = body(v2, "static void\nairportItlwmPostPltiTracePublish(AirportItlwm",
               "trace snapshot publication")
for needle in (
        "buffer.captureGeneration = generation", "buffer.backend = backend",
        "buffer.firstSequence = firstSequence", "buffer.latestSequence = latestSequence",
        "snapshot.captureGeneration = generation", "snapshot.backend = backend",
        "snapshot.firstSequence = buffer.firstSequence",
        "snapshot.latestSequence = buffer.latestSequence",
        "entrySequence < firstSequence", "entrySequence > latestSequence",
):
    require(publish, needle, "generation/window snapshot publication")
require(v2, "airportItlwmPostPltiTraceInvalidate();",
        "teardown invalidates target-bound trace before HAL detach")
require(v2, "airportItlwmPostPltiTracePoll(this);",
        "watchdog-only safe property publication")
set_properties = body(v2, "AirportItlwm::setProperties", "controller setProperties")
ordered(set_properties, "isolated trace control route",
        "AIRPORT_ITLWM_POST_PLTI_TRACE_CONTROL_PROPERTY",
        "airportItlwmPostPltiTraceApplyControl", "return kIOReturnSuccess;")

hidden_assoc = body(sky, "IOReturn AirportItlwmSkywalkInterface::\nsetWCL_ASSOCIATEImpl",
                    "WCL association ingress")
ordered(hidden_assoc, "trace begins immediately before ordinary SCAN request",
        "shouldResumeScanAfterExternalPmk(scanResumeFacts)",
        "AirportItlwmPostPltiTraceBeginEpisode(ic);",
        "ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);")

for needle in (
        "enum iwn_post_plti_trace_tx_class",
        "IWN_POST_PLTI_TRACE_TX_AUTH",
        "IWN_POST_PLTI_TRACE_TX_ASSOC",
        "IWN_POST_PLTI_TRACE_TX_EAPOL",
        "uint8_t post_plti_trace_class;",
):
    require(iwnvar, needle, "IWN categorical per-slot class")
iwn_classifier = body(iwn, "iwn_post_plti_trace_classify_tx",
                      "IWN categorical classifier")
for needle in (
        "IEEE80211_FC0_SUBTYPE_AUTH", "IEEE80211_FC0_SUBTYPE_ASSOC_REQ",
        "ETHERTYPE_PAE", "IWN_POST_PLTI_TRACE_TX_EAPOL",
):
    require(iwn_classifier, needle, "IWN classifier protocol class")
for needle in ("diag_peer", "diag_auth_seq", "auth_seq1", "setProperty",
               "IWX_AUTH_DIAG", "XYLog"):
    forbid(iwn_classifier, needle, "identity/raw diagnostic in IWN classifier")
completion = body(iwn, "static void\niwn_post_plti_trace_record_completion",
                  "IWN categorical completion")
for needle in ("txfail", "ackfail", "rate", "status", "qid", "idx"):
    forbid(completion, needle, "raw completion detail")
for needle in (
        "kAirportItlwmPostPltiTraceEventAuthTxDone",
        "kAirportItlwmPostPltiTraceEventAssocTxDone",
        "kAirportItlwmPostPltiTraceEventEapolTxDone",
):
    require(completion, needle, "IWN categorical completion event")
iwn_newstate = body(iwn, "int ItlIwn::\niwn_newstate", "IWN newstate")
ordered(iwn_newstate, "IWN active SCAN coalesce trace",
        "if (ic->ic_state == IEEE80211_S_SCAN)", "IWN_FLAG_SCANNING",
        "kAirportItlwmPostPltiTraceEventIwnScanCoalesced", "return 0;")
for needle in (
        "kAirportItlwmPostPltiTraceEventAuthStateEntered",
        "kAirportItlwmPostPltiTraceEventAssocStateEntered",
        "kAirportItlwmPostPltiTraceEventRunEntered",
):
    require(iwn_newstate, needle, "IWN state boundary")
iwn_scan = body(iwn, "int ItlIwn::\niwn_scan", "IWN scan")
ordered(iwn_scan, "IWN trace records only successful scan submission",
        "error = iwn_cmd(sc, IWN_CMD_SCAN", "if (error == 0)",
        "IWN_FLAG_SCANNING", "kAirportItlwmPostPltiTraceEventIwnScanStarted")
iwn_tx = body(iwn, "int ItlIwn::\niwn_tx", "IWN transmit")
ordered(iwn_tx, "IWN per-slot class reaches canonical ring kick",
        "post_plti_trace_class = iwn_post_plti_trace_classify_tx",
        "data->post_plti_trace_class = post_plti_trace_class",
        "IWN_WRITE(sc, IWN_HBUS_TARG_WRPTR", "iwn_post_plti_trace_record_submit")
iwn_done = body(iwn, "void ItlIwn::\niwn_tx_done(struct", "IWN TX_DONE")
ordered(iwn_done, "IWN completion observed before reclaim",
        "iwn_post_plti_trace_record_completion", "iwn_tx_done_free_txdata")
start_task = body(iwn, "IOReturn ItlIwn::\n_iwn_start_task", "IWN start task")
for needle in (
        "kAirportItlwmPostPltiTraceEventAuthDequeued",
        "kAirportItlwmPostPltiTraceEventAssocDequeued",
):
    require(start_task, needle, "IWN management dequeue")
rx_done = body(iwn, "void ItlIwn::\niwn_rx_done", "IWN RX")
for needle in (
        "kAirportItlwmPostPltiTraceEventAuthRxFromFirmware",
        "kAirportItlwmPostPltiTraceEventAssocRxFromFirmware",
        "ieee80211_inputm",
):
    require(rx_done, needle, "IWN firmware-to-net80211 ingress")
ordered(rx_done, "IWN RX trace follows FCS and length validation",
        "if ((flags & IWN_RX_NOERROR) != IWN_RX_NOERROR)",
        "if (ic->ic_opmode == IEEE80211_M_MONITOR)",
        "if (len < sizeof (*wh) &&",
        "kAirportItlwmPostPltiTraceEventAuthRxFromFirmware")

end_scan = body(node, "void\nieee80211_end_scan", "net80211 scan completion")
for needle in (
        "kAirportItlwmPostPltiTraceEventScanCompleted",
        "kAirportItlwmPostPltiTraceEventSelectionHeld",
):
    require(end_scan, needle, "scan/selection event")
join_bss = body(node, "void\nieee80211_node_join_bss", "net80211 BSS join")
ordered(join_bss, "selection before join marker",
        "kAirportItlwmPostPltiTraceEventBssSelected",
        "kAirportItlwmPostPltiTraceEventJoinBssEntered")
mgmt_output = body(output, "int\nieee80211_mgmt_output", "net80211 management output")
for needle in (
        "enqueue_dropped == 0",
        "kAirportItlwmPostPltiTraceEventAuthEnqueued",
        "kAirportItlwmPostPltiTraceEventAssocEnqueued",
):
    require(mgmt_output, needle, "accepted management enqueue")
recv_auth = body(input_source, "void\nieee80211_recv_auth", "net80211 auth RX")
require(recv_auth, "kAirportItlwmPostPltiTraceEventAuthRxNet80211",
        "net80211 auth ingress")
recv_assoc = body(input_source, "void\nieee80211_recv_assoc_resp",
                 "net80211 assoc RX")
require(recv_assoc, "kAirportItlwmPostPltiTraceEventAssocRxNet80211",
        "net80211 assoc ingress")
enqueue_data = body(input_source, "void\nieee80211_enqueue_data",
                    "net80211 EAPOL route")
ordered(enqueue_data, "kernel EAPOL route marker",
        "kAirportItlwmPostPltiTraceEventEapolRxDecapped",
        "kAirportItlwmPostPltiTraceEventEapolRxKernelPae")
send_eapol = body(pae_output, "int\nieee80211_send_eapol_key",
                  "kernel EAPOL output")
ordered(send_eapol, "EAPOL accepted enqueue marker",
        "if (error)", "return (error);",
        "kAirportItlwmPostPltiTraceEventEapolTxEnqueued")

# The trace must not broaden the pure-SAE quarantine.
require(auth, "inline bool requiresUnsupportedWpa3Auth",
        "unchanged pure-SAE rejection guard")
require(auth, "return authtypeUpper == kAuditedWpa3PskTransitionAuth;",
        "exact audited PSK transition only")

# The client is responsible for rejecting torn/mixed snapshots before it
# invokes the ordered evaluator.  A missing generation, a changed backend,
# an overflow, or a gap must therefore reach the evaluator as integrity=false.
for needle in (
        "#include <ClientKit/AirportItlwmPostPltiTraceContracts.h>",
        "airport_itlwm_post_plti_trace_classify_entries",
        "snapshot->captureGeneration != buffer->captureGeneration",
        "snapshot->backend != buffer->backend",
        "snapshot->firstSequence != buffer->firstSequence",
        "snapshot->latestSequence != buffer->latestSequence",
        "snapshot->droppedEntries != 0 || buffer->droppedEntries != 0",
        "entry->captureGeneration != snapshot->captureGeneration",
        "entry->sequence < snapshot->firstSequence",
        "entry->sequence > snapshot->latestSequence",
        "count != expected || count != snapshot->entryCount",
        "count != buffer->entryCount",
        "out[i].sequence != snapshot->firstSequence + i",
        "kAirportItlwmPostPltiTraceVerdictBackendUnsupported",
        "IORegistryEntrySetCFProperty",
):
    require(client, needle, "safe trace client integrity boundary")
for needle in ("static enum trace_verdict", "uint64_t *out_events",
               "classify(uint64_t events", "AirportItlwmDiag", "ioreg",
               "RegDiag", "print_mac", "print_bytes", "ssid", "bssid",
               "IOMACAddress"):
    forbid(client, needle, "unsafe or bitset-only client surface")
for needle in (
        "-std=c11 -Wall -Wextra -Werror",
        "AirportItlwmPostPltiTrace/airport_itlwm_post_plti_trace.c",
        "-framework IOKit",
):
    require(build, needle, "safe trace client build")
require(payload_script, "-Iinclude", "unit build include path for shared C contract")
require(runner, "./scripts/build_post_plti_trace.sh",
        "isolated Tahoe gate builds safe trace client")
require(aggregate, "test_tahoe_post_plti_trace_contract.sh",
        "SAE aggregate includes safe trace contract")
for needle in (
        "TahoePostPltiTraceContracts.hpp", "testTahoePostPltiTraceContracts",
        "wrongOrder", "mixedGeneration", "overflowGap", "BackendUnsupported",
        "safe post-PLTI trace",
):
    require(payload_test, needle, "ordered safe trace unit matrix")

print("post-PLTI safe trace contract ok")
PY
