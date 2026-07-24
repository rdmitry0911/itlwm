#!/usr/bin/env bash
# Contract for the isolated, safe-only post-PLTI association trace.
#
# IWN retains the sole generic ordered association evaluator.  IWX has a
# separate categorical PMF/BIP evaluator; neither evaluator establishes a
# runtime traffic claim without its separate sanitized evidence gate.
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
matrix = (root / "include/ClientKit/AirportItlwmPostPltiTraceMatrixContracts.h").read_text()
facade = (root / "AirportItlwm/TahoePostPltiTraceContracts.hpp").read_text()
iwx_pmf_bip = (root / "include/ClientKit/AirportItlwmIwxPmfBipTraceContracts.h").read_text()
iwx_pmf_bip_facade = (root / "AirportItlwm/TahoeIwxPmfBipTraceContracts.hpp").read_text()
iwn_software_pmf = (root / "include/ClientKit/AirportItlwmIwnSoftwarePmfTraceContracts.h").read_text()
v2 = (root / "AirportItlwm/AirportItlwmV2.cpp").read_text()
sky = (root / "AirportItlwm/AirportItlwmSkywalkInterface.cpp").read_text()
iwn = (root / "itlwm/hal_iwn/ItlIwn.cpp").read_text()
iwnvar = (root / "itlwm/hal_iwn/if_iwnvar.h").read_text()
node = (root / "itl80211/openbsd/net80211/ieee80211_node.c").read_text()
iwx = (root / "itlwm/hal_iwx/ItlIwx.cpp").read_text()
output = (root / "itl80211/openbsd/net80211/ieee80211_output.c").read_text()
input_source = (root / "itl80211/openbsd/net80211/ieee80211_input.c").read_text()
proto = (root / "itl80211/openbsd/net80211/ieee80211_proto.c").read_text()
bip = (root / "itl80211/openbsd/net80211/ieee80211_crypto_bip.c").read_text()
protocol = (root / "docs/TAHOE_POST_PLTI_TRACE_RUNTIME_PROTOCOL.md").read_text()
matrix_record = (root / "docs/TAHOE_POST_PLTI_TRACE_MATRIX_CONTRACT.md").read_text()
pae_output = (root / "itl80211/openbsd/net80211/ieee80211_pae_output.c").read_text()
pae_input = (root / "itl80211/openbsd/net80211/ieee80211_pae_input.c").read_text()
auth = (root / "AirportItlwm/TahoeAssociationAuthContracts.hpp").read_text()
client = (root / "AirportItlwmPostPltiTrace/airport_itlwm_post_plti_trace.c").read_text()
build = (root / "scripts/build_post_plti_trace.sh").read_text()
source_identity = (root / "scripts/tahoe_source_identity.py").read_text()
tahoe_build = (root / "scripts/build_tahoe.sh").read_text()
runner = (root / "scripts/run_tahoe_sae_quarantine_layer.sh").read_text()
aggregate = (root / "scripts/test_tahoe_sae_quarantine_contract.sh").read_text()
payload_script = (root / "scripts/test_payload_builders.sh").read_text()
payload_test = (root / "tests/tahoe_payload_builders_test.cpp").read_text()
iwx_c_fixture = (root / "tests/iwx_pmf_bip_trace_contract_test.c").read_text()
iwn_c_fixture = (root / "tests/iwn_software_pmf_trace_contract_test.c").read_text()


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


def require_categorical_record(text, event, controller, label):
    match = re.search(
        r"AirportItlwmPostPltiTraceRecord\(\s*([^,]+),\s*" +
        re.escape(event) + r"\s*\)",
        text,
    )
    if match is None:
        fail(f"missing categorical trace record for {label}")
    if match.group(1).strip() != controller:
        fail(f"trace record carries non-controller data for {label}")


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
        "AIRPORT_ITLWM_POST_PLTI_TRACE_ABI_VERSION 4U",
        "AIRPORT_ITLWM_POST_PLTI_TRACE_MAX_ENTRIES 128U",
        "AIRPORT_ITLWM_POST_PLTI_TRACE_CONTROL_PROPERTY",
        "AIRPORT_ITLWM_POST_PLTI_TRACE_CONTROL_ACK_PROPERTY",
        "AIRPORT_ITLWM_POST_PLTI_TRACE_SNAPSHOT_PROPERTY",
        "AIRPORT_ITLWM_POST_PLTI_TRACE_BUFFER_PROPERTY",
        "kAirportItlwmPostPltiTraceBackendIwn",
        "kAirportItlwmPostPltiTraceBackendUnsupported",
        "kAirportItlwmPostPltiTraceBackendIwx",
        "kAirportItlwmPostPltiTraceEventWclPmkReadyScanResume",
        "kAirportItlwmPostPltiTraceEventPortValidTransition",
        "kAirportItlwmPostPltiTraceEventEpisodeAborted",
        "kAirportItlwmPostPltiTraceEventStateScanSelfRequestObserved",
        "kAirportItlwmPostPltiTraceEventIwnScanStateEntered",
        "kAirportItlwmPostPltiTraceEventIwnScanCommandRejected",
        "kAirportItlwmPostPltiTraceEventScanNoCandidate",
        "kAirportItlwmPostPltiTraceEventCaptureWindowSealed",
        "kAirportItlwmPostPltiTraceEventIwxMfpPaeRxDelivered",
        "kAirportItlwmPostPltiTraceEventIwxMfpPaeQ0Doorbelled",
        "kAirportItlwmPostPltiTraceEventIwxMfpPaeQ0CompletionObserved",
        "kAirportItlwmPostPltiTraceEventIwxIgtkSlot4Published",
        "kAirportItlwmPostPltiTraceEventIwxIgtkSlot5Published",
        "kAirportItlwmPostPltiTraceEventIwxIgtkSlot4TxSelected",
        "kAirportItlwmPostPltiTraceEventIwxIgtkSlot5TxSelected",
        "kAirportItlwmPostPltiTraceEventIwnMfpPaePtkSoftwarePrepared",
        "kAirportItlwmPostPltiTraceEventIwnMfpPaeGtkSoftwarePrepared",
        "kAirportItlwmPostPltiTraceEventIwnMfpPaeIgtkStageAcknowledged",
        "kAirportItlwmPostPltiTraceEventIwnMfpPaeSoftwareCcmpBipPublished",
        "kAirportItlwmPostPltiTraceEventIwnIgtkSlot4Published",
        "kAirportItlwmPostPltiTraceEventIwnIgtkSlot5Published",
        "kAirportItlwmPostPltiTraceEventIwnIgtkSlot4TxSelected",
        "kAirportItlwmPostPltiTraceEventIwnIgtkSlot5TxSelected",
        "AIRPORT_ITLWM_POST_PLTI_TRACE_IWN_SOFTWARE_PMF_EVENT_FIRST",
        "AIRPORT_ITLWM_POST_PLTI_TRACE_IWN_SOFTWARE_PMF_EVENT_LAST",
        "kAirportItlwmPostPltiTraceEventMax",
):
    require(abi, needle, "safe-only public ABI")
for needle in (
        "kAirportItlwmPostPltiTraceBackendUnsupported = 2",
        "kAirportItlwmPostPltiTraceBackendIwx = 3",
        "kAirportItlwmPostPltiTraceEventCaptureWindowSealed = 34",
        "kAirportItlwmPostPltiTraceEventIwxMfpPaeRxDelivered = 35",
        "kAirportItlwmPostPltiTraceEventIwxMfpPaeQ0Doorbelled = 36",
        "kAirportItlwmPostPltiTraceEventIwxMfpPaeQ0CompletionObserved = 37",
        "kAirportItlwmPostPltiTraceEventIwxIgtkSlot4Published = 38",
        "kAirportItlwmPostPltiTraceEventIwxIgtkSlot5Published = 39",
        "kAirportItlwmPostPltiTraceEventIwxIgtkSlot4TxSelected = 40",
        "kAirportItlwmPostPltiTraceEventIwxIgtkSlot5TxSelected = 41",
        "kAirportItlwmPostPltiTraceEventIwnMfpPaePtkSoftwarePrepared = 42",
        "kAirportItlwmPostPltiTraceEventIwnMfpPaeGtkSoftwarePrepared = 43",
        "kAirportItlwmPostPltiTraceEventIwnMfpPaeIgtkStageAcknowledged = 44",
        "kAirportItlwmPostPltiTraceEventIwnMfpPaeSoftwareCcmpBipPublished = 45",
        "kAirportItlwmPostPltiTraceEventIwnIgtkSlot4Published = 46",
        "kAirportItlwmPostPltiTraceEventIwnIgtkSlot5Published = 47",
        "kAirportItlwmPostPltiTraceEventIwnIgtkSlot4TxSelected = 48",
        "kAirportItlwmPostPltiTraceEventIwnIgtkSlot5TxSelected = 49",
        "kAirportItlwmPostPltiTraceEventMax = 50",
):
    require(abi, needle, "append-only IWX PMF observer ABI")

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
        "AirportItlwmPostPltiTraceRecordIgtkPublicationSelection",
        "AirportItlwmPostPltiTraceCompleteEpisode",
        "AirportItlwmPostPltiTraceAbortEpisode",
        "AirportItlwmPostPltiTraceNoteStateRequest",
        "neither allocate, log, publish",
        "static inline void",
        "Shared producer sources",
        "defined(IO80211FAMILY_V3)",
):
    require(bridge, needle, "safe bridge lifecycle contract")
forbid(bridge, "defined(__MAC_26_0)",
       "availability-dependent trace bridge selection")
for needle in (
        "OBJECT_DIR=",
        "require_external_bridge()",
        "AirportItlwm-Tahoe.build/Objects-normal/x86_64",
        "require_external_bridge ieee80211_input AirportItlwmPostPltiTraceRecord",
        "require_external_bridge ieee80211_node AirportItlwmPostPltiTraceRecord",
        "require_external_bridge ieee80211_output AirportItlwmPostPltiTraceRecord",
        "require_external_bridge ieee80211_pae_input AirportItlwmPostPltiTraceCompleteEpisode",
        "require_external_bridge ieee80211_pae_output AirportItlwmPostPltiTraceRecord",
        "require_external_bridge ieee80211_proto AirportItlwmPostPltiTraceNoteStateRequest",
        "require_external_bridge ieee80211_crypto_bip AirportItlwmPostPltiTraceRecordIgtkPublicationSelection",
        "require_external_bridge ItlIwn AirportItlwmPostPltiTraceRecord",
        "require_external_bridge ItlIwx AirportItlwmPostPltiTraceRecord",
        "__ZL.*AirportItlwmPostPltiTrace",
        "external trace bridge",
        "ITLWM_DERIVED_DATA_OVERRIDE",
):
    require(build, needle, "Tahoe trace linkage gate")
require(build, "grep -E", "pipefail-safe Tahoe trace linkage gate")
forbid(build, "grep -Eq", "early-exit Tahoe trace linkage probe")
for needle in (
        "ITLWM_DERIVED_DATA_OVERRIDE",
        "must be an absolute path",
        "DERIVED_DATA=\"$ITLWM_DERIVED_DATA_OVERRIDE\"",
):
    require(tahoe_build, needle, "fresh Tahoe derived-data override")

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
        "IWN software-PMF facts are evaluated by their dedicated contract",
        "AIRPORT_ITLWM_POST_PLTI_TRACE_IWN_SOFTWARE_PMF_EVENT_FIRST",
        "AIRPORT_ITLWM_POST_PLTI_TRACE_IWN_SOFTWARE_PMF_EVENT_LAST",
):
    require(shared, needle, "IWN software-PMF neutral generic evaluator boundary")
for needle in (
        "enum AirportItlwmPostPltiTraceMatrixVerdict",
        "enum AirportItlwmPostPltiTraceMissingStage",
        "airport_itlwm_post_plti_trace_matrix_classify_entries",
        "airport_itlwm_post_plti_trace_matrix_classify_entries_with_stage",
        "airport_itlwm_post_plti_trace_matrix_phase_missing_stage",
        "CaptureWindowSealed",
        "IwnScanCommandRejected",
        "ScanNoCandidate",
        "JoinBss",
        "AuthDequeue",
        "EapolEnqueue",
        "out_missing_stage",
        "terminal",
        "saw_no_candidate",
        "eapol_enqueued",
        "eapol_submitted",
        "eapol_done",
        "eapol_submitted >= eapol_enqueued",
        "eapol_done >= eapol_submitted",
        "kAirportItlwmPostPltiTraceMatrixVerdictKernelChainObserved",
):
    require(matrix, needle, "v2 ordered trace matrix")
for needle in (
        "airport_itlwm_post_plti_trace_matrix_event_is_iwn_software_pmf",
        "AIRPORT_ITLWM_POST_PLTI_TRACE_IWN_SOFTWARE_PMF_EVENT_FIRST",
        "AIRPORT_ITLWM_POST_PLTI_TRACE_IWN_SOFTWARE_PMF_EVENT_LAST",
):
    require(matrix, needle, "IWN software-PMF neutral matrix boundary")
for needle in (
        "#include <ClientKit/AirportItlwmPostPltiTraceMatrixContracts.h>",
        "inline Verdict\nclassifyEntries",
        "MissingStage *outMissingStage",
        "airport_itlwm_post_plti_trace_matrix_classify_entries_with_stage",
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
        "volatile uint8_t recorderLock",
        "volatile uint32_t controlEpoch",
        "volatile uint32_t producerCount",
        "airportItlwmPostPltiTraceTryLock",
        "airportItlwmPostPltiTraceLock",
        "airportItlwmPostPltiTraceUnlock",
        "airportItlwmPostPltiTraceProducerEnter",
        "airportItlwmPostPltiTraceControlLock",
        "airportItlwmPostPltiTraceNoteContendedProducer",
        "producer-side try-lock failure is accounted as a dropped entry",
        "Producers never reserve a sequence without publishing it",
):
    require(v2, needle, "trace reset/seal serialization")
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

# Every producer establishes the epoch guard before it can attempt the
# non-sleeping recorder lock, and a failed attempt must poison exact verdicts
# through the existing dropped-entry integrity boundary before it leaves.
for marker in (
        "AirportItlwmPostPltiTraceRecord",
        "AirportItlwmPostPltiTraceRecordIgtkPublicationSelection",
        "AirportItlwmPostPltiTraceBeginEpisode",
        "AirportItlwmPostPltiTraceCompleteEpisode",
        "AirportItlwmPostPltiTraceAbortEpisode",
        "AirportItlwmPostPltiTraceNoteStateRequest",
):
    producer = body(v2, marker, f"epoch-guarded producer {marker}")
    require(producer, "airportItlwmPostPltiTraceProducerEnter",
            f"producer epoch entry {marker}")
    require(producer, "airportItlwmPostPltiTraceProducerLeave",
            f"producer epoch release {marker}")
    require(producer, "airportItlwmPostPltiTraceNoteContendedProducer",
            f"producer loss accounting {marker}")

for marker in (
        "airportItlwmPostPltiTraceApplyControl",
        "airportItlwmPostPltiTraceInvalidate",
):
    control = body(v2, marker, f"epoch-fenced control {marker}")
    require(control, "airportItlwmPostPltiTraceControlLock",
            f"control epoch closure {marker}")
    require(control, "airportItlwmPostPltiTraceControlUnlock",
            f"control epoch reopening {marker}")

poll = body(v2, "airportItlwmPostPltiTracePoll", "trace watchdog poll")
ordered(poll, "no-op watchdog poll does not close the producer epoch",
        "const uintptr_t bound", "if (reinterpret_cast<uintptr_t>(actual) != bound)",
        "airportItlwmPostPltiTraceControlLock")

close = body(v2, "airportItlwmPostPltiTraceCloseActive",
             "safe episode close")
ordered(close, "terminal token detached before terminal event",
        "__atomic_compare_exchange_n(&sPostPltiTrace.activeToken", "0, false",
        "airportItlwmPostPltiTraceRecordToken(ic, event, token, false)")
may_begin = body(v2, "airportItlwmPostPltiTraceMayBegin",
                 "episode admission gate")
require(may_begin, "admitEpisodes", "episode admission state")
begin = body(v2, "AirportItlwmPostPltiTraceBeginEpisode",
             "safe episode begin bridge")
for needle in (
        "activeToken", "airportItlwmPostPltiTraceMayBegin", "episodeCount",
        "kAirportItlwmPostPltiTraceEventWclPmkReadyScanResume",
        "airportItlwmPostPltiTraceTryLock",
        "airportItlwmPostPltiTraceUnlock",
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
        "airportItlwmPostPltiTraceCloseActive",
        "kAirportItlwmPostPltiTraceEventStateScanSelfRequestObserved",
):
    require(note_state, needle, "strict state lifecycle")

proto_note = body(proto, "ieee80211_pae_assoc_epoch_note_newstate",
                  "net80211 state lifecycle hook")
ordered(proto_note, "trace state hook remains passive and pre-callback",
        "AirportItlwmPostPltiTraceNoteStateRequest", "if ((ic->ic_state")
ordered(proto_note, "trace state hook is not hidden by the STA epoch gate",
        "if (ic == NULL)", "AirportItlwmPostPltiTraceNoteStateRequest",
        "if (ic->ic_opmode != IEEE80211_M_STA)")
pae_terminal = body(pae_input, "void\nieee80211_recv_4way_msg3",
                    "kernel PAE terminal transition")
require(pae_terminal, "AirportItlwmPostPltiTraceCompleteEpisode(ic)",
        "port-valid reaches the trace lifecycle bridge")
forbid(pae_terminal, "kAirportItlwmPostPltiTraceEventPortValidTransition",
       "direct terminal append after close")

bind = body(v2, "airportItlwmPostPltiTraceBind", "categorical backend bind")
for needle in (
        "OSDynamicCast(ItlIwn, driver->fHalService)",
        "kAirportItlwmPostPltiTraceBackendIwn",
        "OSDynamicCast(ItlIwx, driver->fHalService)",
        "kAirportItlwmPostPltiTraceBackendIwx",
        "kAirportItlwmPostPltiTraceBackendUnsupported",
):
    require(bind, needle, "categorical trace backend bind")
admit = body(v2, "airportItlwmPostPltiTraceAdmits", "IWN/IWX trace admission")
for needle in (
        "kAirportItlwmPostPltiTraceBackendIwn",
        "kAirportItlwmPostPltiTraceBackendIwx",
):
    require(admit, needle, "implemented categorical trace backend admission")
require(shared, "backend != kAirportItlwmPostPltiTraceBackendIwn",
        "IWN-only ordered trace evaluator")
forbid(shared, "kAirportItlwmPostPltiTraceBackendIwx",
       "IWX ordered trace success path")

# IWX has its own sealed categorical PMF/BIP evaluator.  It must never relax
# the IWN association matrix above, and it must require a PMF RX -> q0
# doorbell/completion -> post-ack publication -> matching active-slot chain.
for needle in (
        "AirportItlwmIwxPmfBipTraceVerdictCrossSlotRekeyObserved",
        "AirportItlwmIwxPmfBipTraceInitialProgress",
        "AirportItlwmIwxPmfBipTraceMissingStageCaptureSeal",
        "airport_itlwm_iwx_pmf_bip_trace_classify_entries_with_stage",
        "airport_itlwm_iwx_pmf_bip_trace_classify_initial_prefix_with_stage",
        "backend != kAirportItlwmPostPltiTraceBackendIwx",
        "kAirportItlwmPostPltiTraceEventCaptureWindowSealed",
        "kAirportItlwmPostPltiTraceEventPortValidTransition",
        "kAirportItlwmPostPltiTraceEventIwxMfpPaeRxDelivered",
        "kAirportItlwmPostPltiTraceEventIwxMfpPaeQ0Doorbelled",
        "kAirportItlwmPostPltiTraceEventIwxMfpPaeQ0CompletionObserved",
        "kAirportItlwmPostPltiTraceEventIwxIgtkSlot4Published",
        "kAirportItlwmPostPltiTraceEventIwxIgtkSlot5Published",
        "kAirportItlwmPostPltiTraceEventIwxIgtkSlot4TxSelected",
        "kAirportItlwmPostPltiTraceEventIwxIgtkSlot5TxSelected",
        "entries[i].captureGeneration != generation",
        "entries[i].episode != episode",
        "selected_slot != published_slot",
        "selected_slot == active_slot",
        "rekey_count != 0",
        "active_episode != 0",
        "active_episode != episode",
        "InitialPmfBipReady",
        "if (port_valid)",
):
    require(iwx_pmf_bip, needle, "IWX PMF/BIP evaluator fence")
for needle in (
        "enum class Verdict", "CrossSlotRekeyObserved",
        "enum class MissingStage", "CaptureSeal", "classifyEntries",
        "enum class InitialProgress", "InitialPmfBipReady",
        "classifyInitialPrefix",
):
    require(iwx_pmf_bip_facade, needle, "C++ IWX PMF/BIP evaluator facade")
iwx_initial_prefix = body(
    iwx_pmf_bip,
    "airport_itlwm_iwx_pmf_bip_trace_classify_initial_prefix_with_stage",
    "IWX PMF/BIP active-prefix evaluator")
ordered(iwx_initial_prefix, "IWX initial MFP multi-q0 progression",
        "kAirportItlwmPostPltiTraceEventIwxMfpPaeQ0Doorbelled",
        "airport_itlwm_iwx_pmf_bip_trace_phase_need_q0_doorbell &&",
        "airport_itlwm_iwx_pmf_bip_trace_phase_need_igtk_publication",
        "airport_itlwm_iwx_pmf_bip_trace_phase_wait_q0_completion")
ordered(iwx_initial_prefix, "IWX initial M1/Msg3 pre-q0 progression",
        "kAirportItlwmPostPltiTraceEventIwxMfpPaeRxDelivered",
        "airport_itlwm_iwx_pmf_bip_trace_phase_need_pmf_rx &&",
        "airport_itlwm_iwx_pmf_bip_trace_phase_need_q0_doorbell &&",
        "airport_itlwm_iwx_pmf_bip_trace_phase_need_igtk_publication",
        "airport_itlwm_iwx_pmf_bip_trace_phase_need_q0_doorbell;")

# IWN has a separate, counter-only software-PMF evaluator.  It is stricter
# than the generic association matrix: one closed episode must carry the
# ordered local PTK/GTK/IGTK stages, a coherent selected IGTK slot, the final
# locked software CCMP+BIP publication proof, and port-valid.
for needle in (
        "Counter-only evaluator for IWN's lab-gated software PMF owner",
        "AirportItlwmIwnSoftwarePmfTraceVerdictInitialSoftwarePmfObserved",
        "AirportItlwmIwnSoftwarePmfTraceMissingStageCaptureSeal",
        "airport_itlwm_iwn_software_pmf_trace_classify_entries_with_stage",
        "backend != kAirportItlwmPostPltiTraceBackendIwn",
        "episode_count != 1 || active_episode != 0",
        "kAirportItlwmPostPltiTraceEventIwnMfpPaePtkSoftwarePrepared",
        "kAirportItlwmPostPltiTraceEventIwnMfpPaeGtkSoftwarePrepared",
        "kAirportItlwmPostPltiTraceEventIwnMfpPaeIgtkStageAcknowledged",
        "kAirportItlwmPostPltiTraceEventIwnMfpPaeSoftwareCcmpBipPublished",
        "kAirportItlwmPostPltiTraceEventIwnIgtkSlot4Published",
        "kAirportItlwmPostPltiTraceEventIwnIgtkSlot5Published",
        "kAirportItlwmPostPltiTraceEventIwnIgtkSlot4TxSelected",
        "kAirportItlwmPostPltiTraceEventIwnIgtkSlot5TxSelected",
        "entries[i].captureGeneration != generation",
        "entries[i].episode != episode",
        "selected_slot != published_slot",
        "event == kAirportItlwmPostPltiTraceEventEpisodeAborted",
        "event == kAirportItlwmPostPltiTraceEventPortValidTransition",
        "event == kAirportItlwmPostPltiTraceEventCaptureWindowSealed",
):
    require(iwn_software_pmf, needle, "IWN software-PMF evaluator fence")
iwn_software_classifier = body(
    iwn_software_pmf,
    "airport_itlwm_iwn_software_pmf_trace_classify_entries_with_stage",
    "IWN software-PMF ordered evaluator")
ordered(iwn_software_classifier, "IWN fixed PTK/GTK/IGTK stage order",
        "kAirportItlwmPostPltiTraceEventIwnMfpPaePtkSoftwarePrepared",
        "kAirportItlwmPostPltiTraceEventIwnMfpPaeGtkSoftwarePrepared",
        "!ptk_prepared || gtk_prepared || igtk_acknowledged",
        "kAirportItlwmPostPltiTraceEventIwnMfpPaeIgtkStageAcknowledged",
        "!ptk_prepared || !gtk_prepared || igtk_acknowledged",
        "if (publication_slot != 0)",
        "!ptk_prepared || !gtk_prepared || !igtk_acknowledged")

iwn_stage_task = body(iwn, "void ItlIwn::\niwn_mfp_pae_task",
                      "IWN software-PMF stage worker")
ordered(iwn_stage_task, "IWN stage fact follows durable local acceptance",
        "txn->accepted_mask |= iwn_mfp_pae_stage_mask(stage);",
        "iwn_mfp_pae_trace_stage_event(stage)",
        "AirportItlwmPostPltiTraceRecord(ic, trace_stage_event)",
        "IOSimpleLockUnlock(sc->sc_mfp_pae_lock);")
iwn_finish = body(iwn, "int ItlIwn::\niwn_pae_mfp_txn_finish",
                  "IWN software-PMF final publication")
ordered(iwn_finish, "IWN final fact follows locked generic publication",
        "ieee80211_pae_mfp_txn_finish_publish_locked(ic, txn_id);",
        "iwn_mfp_pae_software_keyset_live_locked(ic, generic,",
        "txn->ni))",
        "kAirportItlwmPostPltiTraceEventIwnMfpPaeSoftwareCcmpBipPublished",
        "IOSimpleLockUnlock(sc->sc_mfp_pae_lock);")
iwn_keyset = body(iwn, "iwn_mfp_pae_software_keyset_live_locked",
                  "IWN locked software keyset verifier")
for needle in (
        "generic->have_ptk", "generic->have_gtk", "generic->have_igtk",
        "generic->finish_published", "IEEE80211_KEY_SWCRYPTO",
        "IEEE80211_KEY_PAE_MFP_LIVE", "ic->ic_igtk_kid",
        "IEEE80211_NODE_TXMGMTPROT", "IEEE80211_NODE_RXMGMTPROT",
):
    require(iwn_keyset, needle, "IWN locked software keyset verifier")
forbidden_iwn_keyset = ("ieee80211_bip_key_shape_valid", "XYLog", "setProperty",
                        "->k_key", "k_mgmt_rsc", "firmware")
for needle in forbidden_iwn_keyset:
    forbid(iwn_keyset, needle, "unsafe IWN software keyset evidence")

trace_publish = body(bip, "ieee80211_bip_trace_publish_locked",
                     "post-ack BIP trace publisher")
for needle in (
        "ieee80211_bip_ctx_live_locked", "ic->ic_igtk_kid != slot->k_id",
        "slot->k_id != 4 && slot->k_id != 5",
        "AirportItlwmPostPltiTraceRecordIgtkPublicationSelection(ic, slot->k_id)",
):
    require(trace_publish, needle, "narrow BIP publication trace source")
for needle in ("malloc", "free", "XYLog", "setProperty", "k_key",
               "k_mgmt_rsc", "firmware"):
    forbid(trace_publish, needle, "unsafe BIP trace producer content")
bip_publish = body(bip, "ieee80211_bip_key_publish_retire_locked",
                   "atomic BIP publication")
ordered(bip_publish, "BIP trace follows coherent publication and TX selection",
        "*slot = *new_key;", "newctx->published = 1;",
        "ic->ic_igtk_kid = slot->k_id;",
        "ieee80211_bip_trace_publish_locked(ic, slot);")
require(bip, "#include <ClientKit/AirportItlwmPostPltiTraceBridge.h>",
        "BIP trace bridge include")

igtk_pair = body(v2, "AirportItlwmPostPltiTraceRecordIgtkPublicationSelection",
                 "atomic IGTK trace pair bridge")
for needle in (
        "airportItlwmPostPltiTraceProducerEnter",
        "airportItlwmPostPltiTraceTryLock",
        "airportItlwmPostPltiTraceNoteContendedProducer",
        "airportItlwmPostPltiTraceIgtkEventsForBackend",
        "airportItlwmPostPltiTraceRecordToken(ic, publication_event, token",
        "airportItlwmPostPltiTraceRecordToken(ic, selection_event, token",
        "airportItlwmPostPltiTraceUnlock",
        "airportItlwmPostPltiTraceProducerLeave",
):
    require(igtk_pair, needle, "atomic IGTK trace pair bridge")
ordered(igtk_pair, "IGTK pair remains contiguous in one recorder admission",
        "airportItlwmPostPltiTraceTryLock",
        "airportItlwmPostPltiTraceIgtkEventsForBackend",
        "airportItlwmPostPltiTraceRecordToken(ic, publication_event, token",
        "airportItlwmPostPltiTraceRecordToken(ic, selection_event, token",
        "airportItlwmPostPltiTraceUnlock")
igtk_mapper = body(v2, "airportItlwmPostPltiTraceIgtkEventsForBackend",
                   "backend-specific IGTK event mapper")
for needle in (
        "kAirportItlwmPostPltiTraceBackendIwx",
        "kAirportItlwmPostPltiTraceBackendIwn",
        "kAirportItlwmPostPltiTraceEventIwxIgtkSlot4Published",
        "kAirportItlwmPostPltiTraceEventIwxIgtkSlot5TxSelected",
        "kAirportItlwmPostPltiTraceEventIwnIgtkSlot4Published",
        "kAirportItlwmPostPltiTraceEventIwnIgtkSlot5TxSelected",
):
    require(igtk_mapper, needle, "backend-specific IGTK event mapper")

complete = body(v2, "AirportItlwmPostPltiTraceCompleteEpisode",
                "port-valid trace lifecycle")
ordered(complete, "IWX retains a bounded rekey observation window",
        "kAirportItlwmPostPltiTraceBackendIwx",
        "kAirportItlwmPostPltiTraceEventPortValidTransition",
        "else", "airportItlwmPostPltiTraceCloseActive")
iwx_event_filter = body(v2, "airportItlwmPostPltiTraceEventRequiresIwx",
                        "IWX-only event vocabulary filter")
require(iwx_event_filter,
        "kAirportItlwmPostPltiTraceEventIwxMfpPaeRxDelivered",
        "append-only IWX event vocabulary boundary")
require(iwx_event_filter,
        "kAirportItlwmPostPltiTraceEventIwxIgtkSlot5TxSelected",
        "explicit IWX event vocabulary upper boundary")
iwn_event_filter = body(v2, "airportItlwmPostPltiTraceEventRequiresIwn",
                        "IWN software-PMF event vocabulary filter")
for needle in (
        "AIRPORT_ITLWM_POST_PLTI_TRACE_IWN_SOFTWARE_PMF_EVENT_FIRST",
        "AIRPORT_ITLWM_POST_PLTI_TRACE_IWN_SOFTWARE_PMF_EVENT_LAST",
):
    require(iwn_event_filter, needle, "explicit IWN software-PMF vocabulary boundary")
record_token = body(v2, "airportItlwmPostPltiTraceRecordToken",
                    "trace record token")
ordered(record_token, "shared BIP producer cannot contaminate IWN traces",
        "airportItlwmPostPltiTraceEventRequiresIwx(event)",
        "kAirportItlwmPostPltiTraceBackendIwx",
        "airportItlwmPostPltiTraceEventRequiresIwn(event)",
        "kAirportItlwmPostPltiTraceBackendIwn",
        "airportItlwmPostPltiTraceTokenIsCurrent")
control = body(v2, "airportItlwmPostPltiTraceApplyControl",
               "trace control acknowledgement")
for needle in (
        "generation=%u backend=%u", "captureGeneration",
        "airportItlwmPostPltiTraceBind(driver)", "admitEpisodes",
        "airportItlwmPostPltiTraceControlLock()",
        "airportItlwmPostPltiTraceControlUnlock()",
        "uint32_t seal = 0",
        "kAirportItlwmPostPltiTraceEventCaptureWindowSealed",
        "seal=%u",
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
for needle in (
        "AirportItlwmControllerLifecycleOperationGuard lifecycle(this, false)",
        "if (!lifecycle.admitted())",
        "return kIOReturnNotReady",
):
    require(set_properties, needle, "trace control lifecycle admission")
ordered(set_properties, "trace control is held through bind and acknowledgement",
        "AirportItlwmControllerLifecycleOperationGuard lifecycle(this, false)",
        "setProperty(AIRPORT_ITLWM_POST_PLTI_TRACE_CONTROL_PROPERTY",
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
        "kAirportItlwmPostPltiTraceEventIwnScanStateEntered",
        "iwn_scan(sc, IEEE80211_CHAN_2GHZ, 0)",
        "case IEEE80211_S_SCAN:\n    {",
):
    require(iwn_newstate, needle, "IWN scan-state trace without scan-policy change")
for needle in ("scanFlags", "IEEE80211_IS_CHAN_5GHZ(ic->ic_bss->ni_chan)"):
    forbid(iwn_newstate, needle, "trace-driven scan-policy change")
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
require(iwn_scan, "kAirportItlwmPostPltiTraceEventIwnScanCommandRejected",
        "categorical IWN scan failure marker")
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
        "kAirportItlwmPostPltiTraceEventScanNoCandidate",
):
    require(end_scan, needle, "scan/selection event")
join_bss = body(node, "void\nieee80211_node_join_bss", "net80211 BSS join")
ordered(join_bss, "selection before join marker",
        "kAirportItlwmPostPltiTraceEventBssSelected",
        "kAirportItlwmPostPltiTraceEventJoinBssEntered")
require(iwx, "#include <ClientKit/AirportItlwmPostPltiTraceBridge.h>",
        "IWX safe trace bridge")
iwx_rx_task = body(iwx, "void ItlIwx::\niwx_security_rx_task(void *arg)",
                   "IWX PMF security RX worker")
ordered(iwx_rx_task, "IWX PMF RX trace follows stale-entry rejection",
        "entry.assoc_epoch == 0",
        "ieee80211_pae_assoc_epoch_current(ic) != entry.assoc_epoch",
        "ic->ic_bss != entry.ni",
        "AirportItlwmPostPltiTraceRecord",
        "ieee80211_eapol_key_input")
require_categorical_record(
    iwx_rx_task, "kAirportItlwmPostPltiTraceEventIwxMfpPaeRxDelivered",
    "ic", "IWX PMF RX delivery")
iwx_send = body(iwx, "int ItlIwx::\niwx_send_cmd", "IWX q0 sender")
ordered(iwx_send, "IWX PMF q0 trace follows the observed doorbell",
        "sc->sc_cmdq_slots[idx].state = IWX_CMD_SLOT_SUBMITTED;",
        "IWX_WRITE(sc, IWX_HBUS_TARG_WRPTR",
        "hcmd->async_owner == IWX_CMD_ASYNC_OWNER_MFP_PAE",
        "AirportItlwmPostPltiTraceRecord",
        "unlock:",
        "IOSimpleLockUnlock(sc->sc_cmdq_lock);")
require_categorical_record(
    iwx_send, "kAirportItlwmPostPltiTraceEventIwxMfpPaeQ0Doorbelled",
    "&sc->sc_ic", "IWX PMF q0 doorbell")
iwx_done = body(iwx, "void ItlIwx::\niwx_cmd_done", "IWX q0 completion")
ordered(iwx_done, "IWX PMF completion trace leaves q0 and sleep locks first",
        "IOSimpleLockUnlock(sc->sc_cmdq_lock);",
        "unlockTsleep();",
        "async_result_ready",
        "AirportItlwmPostPltiTraceRecord",
        "that->iwx_mfp_pae_q0_done")
require_categorical_record(
    iwx_done, "kAirportItlwmPostPltiTraceEventIwxMfpPaeQ0CompletionObserved",
    "&sc->sc_ic", "IWX PMF q0 completion")

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
        "#include <ClientKit/AirportItlwmPostPltiTraceMatrixContracts.h>",
        "#include <ClientKit/AirportItlwmIwnSoftwarePmfTraceContracts.h>",
        "airport_itlwm_post_plti_trace_matrix_classify_entries_with_stage",
        "first_missing_stage",
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
        "kAirportItlwmPostPltiTraceMatrixVerdictBackendUnsupported",
        "IORegistryEntrySetCFProperty",
):
    require(client, needle, "safe trace client integrity boundary")
for needle in ("static enum trace_verdict", "uint64_t *out_events",
               "classify(uint64_t events", "AirportItlwmDiag", "ioreg",
               "RegDiag", "print_mac", "print_bytes", "ssid", "bssid",
               "IOMACAddress"):
    forbid(client, needle, "unsafe or bitset-only client surface")
for needle in (
        "kAirportItlwmPostPltiTraceEventIwxMfpPaeRxDelivered",
        "iwx-mfp-pae-rx-delivered",
        "kAirportItlwmPostPltiTraceEventIwxMfpPaeQ0Doorbelled",
        "iwx-mfp-pae-q0-doorbelled",
        "kAirportItlwmPostPltiTraceEventIwxMfpPaeQ0CompletionObserved",
        "iwx-mfp-pae-q0-completion-observed",
        "kAirportItlwmPostPltiTraceEventIwxIgtkSlot4Published",
        "iwx-igtk-slot4-published",
        "kAirportItlwmPostPltiTraceEventIwxIgtkSlot5Published",
        "iwx-igtk-slot5-published",
        "kAirportItlwmPostPltiTraceEventIwxIgtkSlot4TxSelected",
        "iwx-igtk-slot4-tx-selected",
        "kAirportItlwmPostPltiTraceEventIwxIgtkSlot5TxSelected",
        "iwx-igtk-slot5-tx-selected",
        "#include <ClientKit/AirportItlwmIwxPmfBipTraceContracts.h>",
        "get_iwx_pmf_bip_report",
        "pmf-bip-report",
        "pmf_bip_verdict=%s first_missing_stage=%s",
        "get_iwx_pmf_bip_progress",
        "pmf-bip-progress",
        "pmf_bip_progress=%s first_missing_stage=%s",
        "INITIAL_PMF_BIP_READY",
        "kAirportItlwmIwxPmfBipTraceVerdictCrossSlotRekeyObserved",
        "kAirportItlwmPostPltiTraceBackendIwx",
        "return \"IWX\";",
):
    require(client, needle, "IWX categorical client mapping")
for needle in (
        "kAirportItlwmPostPltiTraceEventIwnMfpPaePtkSoftwarePrepared",
        "iwn-mfp-pae-ptk-software-prepared",
        "kAirportItlwmPostPltiTraceEventIwnMfpPaeGtkSoftwarePrepared",
        "iwn-mfp-pae-gtk-software-prepared",
        "kAirportItlwmPostPltiTraceEventIwnMfpPaeIgtkStageAcknowledged",
        "iwn-mfp-pae-igtk-stage-acknowledged",
        "kAirportItlwmPostPltiTraceEventIwnMfpPaeSoftwareCcmpBipPublished",
        "iwn-mfp-pae-software-ccmp-bip-published",
        "kAirportItlwmPostPltiTraceEventIwnIgtkSlot4Published",
        "iwn-igtk-slot4-published",
        "kAirportItlwmPostPltiTraceEventIwnIgtkSlot5TxSelected",
        "iwn-igtk-slot5-tx-selected",
        "#include <ClientKit/AirportItlwmIwnSoftwarePmfTraceContracts.h>",
        "get_iwn_software_pmf_report",
        "iwn-software-pmf-report",
        "iwn_software_pmf_verdict=%s first_missing_stage=%s",
        "INITIAL_SOFTWARE_PMF_OBSERVED",
        "kAirportItlwmPostPltiTraceBackendIwn",
        "return \"IWN\";",
):
    require(client, needle, "IWN software-PMF categorical client mapping")
for needle in (
        "-std=c11 -Wall -Wextra -Werror",
        "AirportItlwmPostPltiTrace/airport_itlwm_post_plti_trace.c",
        "-framework IOKit",
):
    require(build, needle, "safe trace client build")
for needle in (
        '"AirportItlwmPostPltiTrace"',
        '"scripts/build_post_plti_trace.sh"',
        'tahoe-airportitlwm-source-identity-v2',
):
    require(source_identity, needle, "trace-client source identity coverage")
for needle in (
        "-Iinclude", "tests/iwx_pmf_bip_trace_contract_test.c",
        "iwx_pmf_bip_trace_contract_test",
        "tests/iwn_software_pmf_trace_contract_test.c",
        "iwn_software_pmf_trace_contract_test",
):
    require(payload_script, needle, "unit build for PMF trace C contracts")
for needle in (
        "initial slot-4 PMF transaction", "slot-4 initial followed by slot-5",
        "slot-5 initial followed by slot-4", "missing q0 completion",
        "one PMF Msg3 with PTK/GTK/IGTK q0 stages",
        "MFP Msg1 then Msg3 with PTK/GTK/IGTK q0 stages",
        "repeated q0 doorbell still requires the preceding completion",
        "MFP RX while q0 completion is outstanding",
        "still-active IWX episode",
        "publication without a PMF owner sequence", "active-slot fact before publication",
        "repeated active-slot selection", "same-slot replacement",
        "more than one cross-slot rekey",
        "mixed episode", "mixed generation", "caller-detected drop or overflow",
        "event after the terminal seal", "cancellation or detach",
        "IWN backend", "unknown backend",
):
    require(iwx_c_fixture, needle, "deterministic IWX PMF/BIP C fixture")
for needle in (
        "one closed initial transaction proves software PMF ownership",
        "a sealed partial transaction cannot skip software GTK preparation",
        "software GTK cannot precede the software PTK stage",
        "the IGTK acknowledgement cannot precede software GTK preparation",
        "a selected IGTK slot must match its categorical publication",
        "final publication cannot precede an active IGTK slot",
        "a record after the close-on-port-valid terminal is rejected",
        "a mixed capture generation is never a PMF verdict",
        "a caller-detected drop or overflow is fail-closed",
        "the IWX backend cannot borrow the IWN software-PMF evaluator",
):
    require(iwn_c_fixture, needle, "deterministic IWN software-PMF C fixture")
ordered(runner, "isolated Tahoe producer build precedes trace audit",
        "ITLWM_SOURCE_ID_OVERRIDE='$SOURCE_ID' ./scripts/build_tahoe.sh '$BOOTKC'",
        "cd '$REMOTE_DIR' && ./scripts/build_post_plti_trace.sh")
require(aggregate, "test_tahoe_post_plti_trace_contract.sh",
        "SAE aggregate includes safe trace contract")
for needle in (
        "test_tahoe_iwn_software_pmf_lab_build_contract.sh",
        "test_tahoe_iwn_software_pmf_contract.sh",
        "test_tahoe_iwn_software_pmf_reconnect_contract.sh",
):
    require(aggregate, needle, "SAE aggregate includes IWN software-PMF regression gates")
for needle in (
        "TahoePostPltiTraceContracts.hpp", "testTahoePostPltiTraceContracts",
        "wrongOrder", "mixedGeneration", "overflowGap", "BackendUnsupported",
        "testTahoePostPltiTraceMatrixSealedPrefixes",
        "TahoeIwxPmfBipTraceContracts.hpp", "testTahoeIwxPmfBipTraceContracts",
        "InitialPmfBipObserved", "CrossSlotRekeyObserved", "same_slot_replacement",
        "multiple_cross_slot_rekeys",
        "classifyInitialPrefix", "InitialPmfBipReady",
        "one RX with PTK/GTK/IGTK q0 stages",
        "three-stage initial PMF Msg3",
        "sole rekey authorization progress state",
        "CaptureWindowSealed", "ScanCommandRejected", "ScanNoCandidate",
        "JoinBss", "AuthDequeue", "PortValid",
        "safe post-PLTI and IWX PMF/BIP trace matrices",
):
    require(payload_test, needle, "ordered safe trace unit matrix")

# The committed matrix record is itself contract-bound: it records every
# verified sealed prefix, preserves the one-release policy, and cannot grow a
# raw identity surface while serving as test evidence.
for needle in (
        "## Sealed capture rule",
        "## Versioned synthetic scenarios",
        "trace v4 layer",
        "IWN software-PMF evaluator",
        "fixed PTK-to-GTK-to-IGTK",
        "does not claim SAE, an on-air association",
        "WCL resume followed by seal",
        "A no-candidate retry",
        "partial authentication prefix",
        "A post-terminal event",
        "The sole complete diagnostic trace classification is KernelChainObserved.",
        "A semantic version owns one mutable prerelease asset.",
        "This layer does not implement or prove pure SAE",
):
    require(matrix_record, needle, "committed trace-matrix evidence record")
for needle in (
        "The generic ordered association evaluator is IWN-only.",
        "dedicated IWX PMF/BIP evaluator",
        "post-acknowledgement IGTK slot-4 or slot-5",
        "does not prove PMF-required association, traffic, SAE",
        "port-valid record intentionally",
        "keeps an IWX episode open until the explicit seal",
        "active-prefix classifier",
        "rekey authorization predicate",
        "never a sealed verdict or final success",
        "Slot 4 followed by slot 5, and slot 5 followed by slot 4",
        "Repeated selection, same-slot",
        "replacement, publication without the PMF owner chain",
):
    require(matrix_record, needle, "IWX PMF/BIP evidence boundary")
require(protocol, "The current release-bound runner remains IWN-only.",
        "IWN-only release-bound runtime protocol")
for needle in (
        "backend=iwx",
        "verdict=BACKEND_UNSUPPORTED",
        "trace-backend-iwx-ordered-unsupported",
        "then stops before the",
        "radio OFF/ON transition.",
):
    require(protocol, needle, "IWX ordered-runner boundary")
for pattern, label in (
        (r"\b(?:\d{1,3}\.){3}\d{1,3}\b", "IPv4 identity"),
        (r"\b(?:[0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}\b", "MAC identity"),
):
    if re.search(pattern, matrix_record):
        fail(f"unexpected {label} in committed trace-matrix evidence record")
for needle in (
        "seal the capture",
        "two identical frozen safe trace reads",
        "structurally valid sealed diagnostic prefix",
        "create_tahoe_candidate_provenance.py",
        "not accept a free source-commit label.",
        "evidence contract to report `result=PASS`",
):
    require(protocol, needle, "sealed runtime protocol")

print("post-PLTI safe trace contract ok")
PY
