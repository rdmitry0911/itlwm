#!/usr/bin/env python3
"""Generate and verify Tahoe state-machine closure evidence."""

import argparse
import json
import re
import sys
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[1]

STEP_ID = "step:itlwm-rm-04"
ROADMAP_ITEM_ID = "itlwm-rm-04"
GOAL_ITEM_IDS = ["itlwm-fg-04-consumer-producer-state-machines"]
INPUT_HEAD = "abceca0f53d7f9a322de04626373803ed473ed0c"
CAPTURE_SOURCE = "committed-source-apple-reference-and-contract-header"

REFERENCE_CASES = [
    {
        "id": "apple-wcl-join-manager",
        "path": "docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/wifi_bundle_full_v3/70_WCLJoinManager_fully_symbolic_FSM_checked.yaml",
        "tokens": [
            "fsm_name: JOIN_MANAGER",
            "JOIN_MANAGER_STATE_CONNECT_COMPLETE",
            "JOIN_MANAGER_EVENT_JOIN_ABORT_REQ",
            "halt_pattern",
            "timeout_pattern",
        ],
    },
    {
        "id": "apple-wcl-scan-manager",
        "path": "docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/wifi_bundle_full_v3/69_WCLScanManager_fully_symbolic_FSM_corrected.yaml",
        "tokens": [
            "fsm_name: SCAN_MANAGER",
            "SCAN_ABORT_REQ",
            "handleScanComplete",
            "SYSTEM_POWER_OFF",
            "handleTimeoutAbort",
        ],
    },
    {
        "id": "apple-wcl-net-manager",
        "path": "docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/wifi_bundle_full_v3/72_WCLNetManager_fully_symbolic_FSM_checked.yaml",
        "tokens": [
            "fsm_name: NET_MANAGER",
            "WAITING_FOR_CONNECT_COMPLETE",
            "SLEEP_DEAUTH",
            "sleep_wake",
            "driver_reset",
        ],
    },
    {
        "id": "apple-tx-queue-space-pending",
        "path": "docs/reference/AppleBCMWLAN_tx_queue_space_pending_2026_04_27.md",
        "tokens": [
            "getTxQueueDepth()",
            "pendingPackets(unsigned char)",
            "packetSpace(unsigned char)",
            "fTxQueue->getFreeSpace()",
        ],
    },
    {
        "id": "apple-rx-completion-producer",
        "path": "docs/reference/AppleBCMWLAN_rx_completion_pending_producer_2026_04_27.md",
        "tokens": [
            "IOSkywalkRxCompletionQueue::requestEnqueue",
            "fixed-capacity",
            "pending RX producer queue",
            "teardown drains pending prepared packets",
        ],
    },
    {
        "id": "apple-tx-completion-producer",
        "path": "docs/reference/AppleBCMWLAN_tx_completion_producer_2026_04_27.md",
        "tokens": [
            "pending TX-completion producer",
            "requestEnqueue(nullptr, 0)",
            "returns produced count",
            "teardown drains staged completion packets",
        ],
    },
]

STATE_MACHINES = [
    {
        "name": "association-join",
        "domain": "association",
        "states": [
            "INIT",
            "SCAN",
            "AUTH",
            "ASSOC",
            "RUN",
            "JOIN_MANAGER_IDLE",
            "JOIN_MANAGER_IN_PROGRESS",
            "JOIN_MANAGER_ASSOC_DONE",
            "JOIN_MANAGER_CONNECT_COMPLETE",
        ],
        "ordered_events": [
            "scan result selection",
            "APPLE80211_IOC_ASSOCIATE",
            "WCL_ASSOCIATE",
            "WCL_LINK_UP_DONE",
            "WCL_CONNECT_COMPLETE_EVENT",
        ],
        "reference_ids": ["apple-wcl-join-manager", "apple-wcl-net-manager"],
        "implementation_checks": [
            {
                "path": "AirportItlwm/TahoeStateMachineClosure.hpp",
                "tokens": [
                    '"association-join"',
                    "INIT->SCAN->AUTH->ASSOC->RUN plus WCL JOIN_MANAGER",
                    "IDLE->IN_PROGRESS->ASSOC_DONE->CONNECT_COMPLETE->IDLE",
                    "JOIN_ABORT_REQ, TIMEOUT, DRIVER_RESET, and SYSTEM_POWER_OFF",
                ],
            },
            {
                "path": "itl80211/openbsd/net80211/ieee80211_proto.c",
                "tokens": [
                    "case IEEE80211_S_SCAN:",
                    "case IEEE80211_S_AUTH:",
                    "case IEEE80211_S_ASSOC:",
                    "timeout_del(&ic->ic_bgscan_timeout)",
                    "mq_purge(&ic->ic_mgtq)",
                    "ieee80211_crypto_clear_groupkeys(ic)",
                ],
            },
            {
                "path": "AirportItlwm/AirportItlwmV2.cpp",
                "tokens": [
                    "WCL JoinDone owns assoc completion",
                    "postTahoeWclConnectCompleteEvent",
                    "APPLE80211_M_WCL_CONNECT_COMPLETE_EVENT",
                ],
            },
        ],
        "recovery_case_ids": [
            "join-abort-halt-timeout",
            "driver-reset-power-wake",
        ],
    },
    {
        "name": "auth-pmk",
        "domain": "authentication",
        "states": [
            "NO_TARGET",
            "TARGET_PUBLISHED",
            "WAITING_FOR_PMK",
            "PMK_INSTALLED",
            "HANDSHAKE_READY",
            "CLEARED",
        ],
        "ordered_events": [
            "associateSSID target publish",
            "PLTI user-client open",
            "generation_echo validate",
            "deliverExternalPMK install",
            "EAPOL handshake",
        ],
        "reference_ids": ["apple-wcl-join-manager"],
        "implementation_checks": [
            {
                "path": "AirportItlwm/TahoeStateMachineClosure.hpp",
                "tokens": [
                    '"auth-pmk"',
                    "NO_TARGET->TARGET_PUBLISHED->WAITING_FOR_PMK->PMK_INSTALLED",
                    "generation_echo rejects stale deliveries",
                ],
            },
            {
                "path": "AirportItlwm/AirportItlwmV2.cpp",
                "tokens": [
                    "Project-owned PLTI PMK producer trigger surface",
                    "PRODUCER (kext, AirportItlwmSkywalkInterface::associateSSID)",
                    "generation_echo == fAssocTarget.generation",
                    "deliverExternalPMK INSTALLED generation_echo",
                ],
            },
            {
                "path": "AirportItlwm/AirportItlwmSkywalkInterface.cpp",
                "tokens": [
                    "clearExternalPmkEligibilityLocked(\"setCLEAR_PMKSA_CACHE\")",
                    "clearExternalPmkEligibilityLocked(\"setWCL_REASSOC\")",
                    "clearExternalPmkEligibilityLocked(\"setWCL_JOIN_ABORT\")",
                    "external_pmk_eligibility_clear_count",
                ],
            },
        ],
        "recovery_case_ids": [
            "auth-generation-clear",
            "join-abort-halt-timeout",
        ],
    },
    {
        "name": "scan-wcl",
        "domain": "scan",
        "states": [
            "SCAN_MANAGER_IDLE",
            "SCAN_MANAGER_IN_PROGRESS",
            "SCAN_MANAGER_HALTED",
            "SCAN_MANAGER_ABORTED",
        ],
        "ordered_events": [
            "setWCL_SCAN_REQ",
            "cache bgscan",
            "per-BSS WCL_SCAN_RESULT",
            "terminal WCL_SCAN_DONE",
        ],
        "reference_ids": ["apple-wcl-scan-manager"],
        "implementation_checks": [
            {
                "path": "AirportItlwm/TahoeStateMachineClosure.hpp",
                "tokens": [
                    '"scan-wcl"',
                    "IDLE->IN_PROGRESS->IDLE with HALTED and ABORTED recovery states",
                    "WCL_SCAN_RESULT before",
                ],
            },
            {
                "path": "AirportItlwm/AirportItlwmSkywalkInterface.cpp",
                "tokens": [
                    "setWCL_SCAN_REQ(apple80211ScanRequest *req)",
                    "ieee80211_begin_cache_bgscan(&ic->ic_ac.ac_if)",
                    "scanSource->setTimeoutMS(100)",
                ],
            },
            {
                "path": "AirportItlwm/AirportItlwmV2.cpp",
                "tokens": [
                    "postWclScanResultsGated",
                    "APPLE80211_M_WCL_SCAN_RESULT",
                    "APPLE80211_M_WCL_SCAN_DONE",
                    "scanSource->cancelTimeout()",
                    "scanSource->disable()",
                ],
            },
        ],
        "recovery_case_ids": [
            "scan-abort-timeout-power",
            "driver-reset-power-wake",
        ],
    },
    {
        "name": "data-path",
        "domain": "data-path",
        "states": [
            "DOWN",
            "QUEUE_READY",
            "TX_RUNNING",
            "RX_RUNNING",
            "DRAINING",
            "DOWN",
        ],
        "ordered_events": [
            "ifq_enqueue",
            "ifq_dequeue",
            "iwx_tx",
            "firmware completion",
            "rx reorder",
            "net80211 delivery",
        ],
        "reference_ids": [
            "apple-tx-queue-space-pending",
            "apple-rx-completion-producer",
            "apple-tx-completion-producer",
        ],
        "implementation_checks": [
            {
                "path": "AirportItlwm/TahoeStateMachineClosure.hpp",
                "tokens": [
                    '"data-path"',
                    "ifq_enqueue feeds the bounded IOPacketQueue",
                    "qfullmsk sets ifq_oactive",
                ],
            },
            {
                "path": "itl80211/openbsd/sys/_ifq.cpp",
                "tokens": [
                    "IOPacketQueue::withCapacity(maxLen)",
                    "lockEnqueueWithDrop(m)",
                    "lockDequeue()",
                    "ifq->ifq_oactive = 1",
                ],
            },
            {
                "path": "itlwm/hal_iwx/ItlIwx.cpp",
                "tokens": [
                    "if (sc->qfullmsk != 0)",
                    "ifq_set_oactive(&ifp->if_snd)",
                    "if (sc->sc_flags & IWX_FLAG_TXFLUSH)",
                    "m = ifq_dequeue(&ifp->if_snd)",
                    "iwx_clear_reorder_buffer(sc, rxba)",
                ],
            },
            {
                "path": "AirportItlwm/AirportItlwmV2.hpp",
                "tokens": [
                    "kAirportItlwmRxPendingCapacity = 256",
                    "kAirportItlwmTxCompletionPendingCapacity = 256",
                ],
            },
            {
                "path": "AirportItlwm/AirportItlwmV2.cpp",
                "tokens": [
                    "skywalkRxAction",
                    "skywalkTxCompletionAction",
                    "requestEnqueue(nullptr, 0)",
                    "kAirportItlwmRxPendingCapacity",
                    "kAirportItlwmTxCompletionPendingCapacity",
                ],
            },
        ],
        "recovery_case_ids": [
            "tx-backpressure-drain",
            "rx-reorder-reset",
        ],
    },
]

PRODUCER_CONSUMER_CHAINS = [
    {
        "id": "scan-request-to-wcl-done",
        "machine": "scan-wcl",
        "producer": "AirportItlwmSkywalkInterface::setWCL_SCAN_REQ / setSCAN_REQ",
        "queue": "scanSource timer plus ieee80211_begin_cache_bgscan",
        "consumer": "AirportItlwm::postWclScanResultsGated",
        "ordered": True,
        "backpressure_safe": True,
        "recovery_case_ids": ["scan-abort-timeout-power"],
        "implementation_checks": [
            {
                "path": "AirportItlwm/TahoeStateMachineClosure.hpp",
                "tokens": [
                    '"scan-request-to-wcl-done"',
                    "single timer-driven scan completion",
                    "IWX_FLAG_SCANNING",
                ],
            },
        ],
    },
    {
        "id": "join-candidate-to-connect-complete",
        "machine": "association-join",
        "producer": "AirportItlwmSkywalkInterface::setWCL_ASSOCIATE",
        "queue": "WCL JOIN_MANAGER and net80211 newstate work queue",
        "consumer": "postTahoeWclConnectCompleteEvent",
        "ordered": True,
        "backpressure_safe": True,
        "recovery_case_ids": ["join-abort-halt-timeout"],
        "implementation_checks": [
            {
                "path": "AirportItlwm/TahoeStateMachineClosure.hpp",
                "tokens": [
                    '"join-candidate-to-connect-complete"',
                    "WCLJoinManager IN_PROGRESS",
                    "postTahoeWclConnectCompleteEvent",
                ],
            },
            {
                "path": "itlwm/hal_iwx/ItlIwx.cpp",
                "tokens": [
                    "iwx_add_task(sc, sc->sc_nswq, &sc->newstate_task)",
                    "case IEEE80211_S_AUTH:",
                    "case IEEE80211_S_ASSOC:",
                    "case IEEE80211_S_RUN:",
                ],
            },
        ],
    },
    {
        "id": "auth-target-to-pmk-install",
        "machine": "auth-pmk",
        "producer": "AirportItlwmSkywalkInterface::associateSSID",
        "queue": "fAssocTarget generation gate",
        "consumer": "AirportItlwm::deliverExternalPMK",
        "ordered": True,
        "backpressure_safe": True,
        "recovery_case_ids": ["auth-generation-clear"],
        "implementation_checks": [
            {
                "path": "AirportItlwm/TahoeStateMachineClosure.hpp",
                "tokens": [
                    '"auth-target-to-pmk-install"',
                    "one pending generation is valid at a time",
                    "generation_echo",
                    "rejects late producers",
                ],
            },
        ],
    },
    {
        "id": "tx-ifqueue-to-firmware-ring",
        "machine": "data-path",
        "producer": "ifq_enqueue",
        "queue": "bounded IOPacketQueue and iwx qfullmsk",
        "consumer": "iwx_tx",
        "ordered": True,
        "backpressure_safe": True,
        "recovery_case_ids": ["tx-backpressure-drain"],
        "implementation_checks": [
            {
                "path": "AirportItlwm/TahoeStateMachineClosure.hpp",
                "tokens": [
                    '"tx-ifqueue-to-firmware-ring"',
                    "lockEnqueueWithDrop bounds producer pressure",
                    "TXFLUSH blocks dequeue",
                ],
            },
        ],
    },
    {
        "id": "rx-firmware-ring-to-net80211",
        "machine": "data-path",
        "producer": "firmware RX completion ring",
        "queue": "RX ring plus AMPDU reorder buffer",
        "consumer": "ieee80211_input path",
        "ordered": True,
        "backpressure_safe": True,
        "recovery_case_ids": ["rx-reorder-reset"],
        "implementation_checks": [
            {
                "path": "AirportItlwm/TahoeStateMachineClosure.hpp",
                "tokens": [
                    '"rx-firmware-ring-to-net80211"',
                    "RX descriptor validity, duplicate detection, BA window",
                    "iwx_clear_reorder_buffer",
                ],
            },
        ],
    },
]

RECOVERY_CASES = [
    {
        "id": "join-abort-halt-timeout",
        "description": "Active join states leave forward progress through explicit abort, halt, timeout, driver-reset, and power-off edges.",
        "paths": [
            "docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/wifi_bundle_full_v3/70_WCLJoinManager_fully_symbolic_FSM_checked.yaml",
            "AirportItlwm/AirportItlwmSkywalkInterface.cpp",
        ],
        "checks": [
            {
                "path": "docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/wifi_bundle_full_v3/70_WCLJoinManager_fully_symbolic_FSM_checked.yaml",
                "tokens": ["abort_flow", "halt_pattern", "timeout_pattern"],
            },
            {
                "path": "AirportItlwm/AirportItlwmSkywalkInterface.cpp",
                "tokens": [
                    "setWCL_JOIN_ABORT(apple80211_wcl_abort_join *data)",
                    "clearExternalPmkEligibilityLocked(\"setWCL_JOIN_ABORT\")",
                    "APPLE80211_M_WCL_JOIN_ABORT_COMPLETE",
                ],
            },
        ],
    },
    {
        "id": "scan-abort-timeout-power",
        "description": "WCL scan request, abort, timeout, driver reset, and power-off transitions drain to ABORTED/HALTED/IDLE.",
        "paths": [
            "docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/wifi_bundle_full_v3/69_WCLScanManager_fully_symbolic_FSM_corrected.yaml",
            "AirportItlwm/AirportItlwmV2.cpp",
        ],
        "checks": [
            {
                "path": "docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/wifi_bundle_full_v3/69_WCLScanManager_fully_symbolic_FSM_corrected.yaml",
                "tokens": ["SCAN_ABORT_REQ", "TIMEOUT", "DRIVER_RESET", "SYSTEM_POWER_OFF"],
            },
            {
                "path": "AirportItlwm/AirportItlwmV2.cpp",
                "tokens": ["scanSource->cancelTimeout()", "scanSource->disable()"],
            },
        ],
    },
    {
        "id": "auth-generation-clear",
        "description": "PMK producer state is rejected when stale and cleared on disassociate, leave, PMKSA clear, RSN disable, JOIN_ABORT, and REASSOC.",
        "paths": [
            "AirportItlwm/AirportItlwmV2.cpp",
            "AirportItlwm/AirportItlwmSkywalkInterface.cpp",
        ],
        "checks": [
            {
                "path": "AirportItlwm/AirportItlwmV2.cpp",
                "tokens": [
                    "generation_echo != pending",
                    "generation_echo != s->fAssocTarget.generation",
                    "deliverExternalPMK REJECT_GENERATION",
                ],
            },
            {
                "path": "AirportItlwm/AirportItlwmSkywalkInterface.cpp",
                "tokens": [
                    "associateSSID_disable_rsn",
                    "setCLEAR_PMKSA_CACHE",
                    "setWCL_REASSOC",
                    "setWCL_JOIN_ABORT",
                ],
            },
        ],
    },
    {
        "id": "tx-backpressure-drain",
        "description": "TX producers are bounded by IOPacketQueue capacity, qfullmsk/oactive, TXFLUSH, timers, and ring reset/free paths.",
        "paths": [
            "itl80211/openbsd/sys/_ifq.cpp",
            "itlwm/hal_iwx/ItlIwx.cpp",
        ],
        "checks": [
            {
                "path": "itl80211/openbsd/sys/_ifq.cpp",
                "tokens": ["IOPacketQueue::withCapacity(maxLen)", "lockEnqueueWithDrop(m)"],
            },
            {
                "path": "itlwm/hal_iwx/ItlIwx.cpp",
                "tokens": [
                    "sc->qfullmsk |= 1 << ring->qid",
                    "ifq_set_oactive(&ifp->if_snd)",
                    "sc->sc_flags |= IWX_FLAG_TXFLUSH",
                    "sc->sc_flags &= ~IWX_FLAG_TXFLUSH",
                    "iwx_free_tx_ring(sc, &sc->txq[txq_i])",
                ],
            },
        ],
    },
    {
        "id": "rx-reorder-reset",
        "description": "RX producers are bounded by fixed pending capacity and BA/reorder windows, with stale entries cleared on stop and init failure.",
        "paths": [
            "AirportItlwm/AirportItlwmV2.cpp",
            "itlwm/hal_iwx/ItlIwx.cpp",
        ],
        "checks": [
            {
                "path": "AirportItlwm/AirportItlwmV2.cpp",
                "tokens": [
                    "fRxPendingCount < kAirportItlwmRxPendingCapacity",
                    "skywalkRxAction",
                    "fRxQueue->requestEnqueue(nullptr, 0)",
                ],
            },
            {
                "path": "itlwm/hal_iwx/ItlIwx.cpp",
                "tokens": [
                    "iwx_clear_reorder_buffer(sc, rxba)",
                    "iwx_free_rx_ring(sc, &sc->rxq)",
                    "IWX_RX_REORDER_DATA_INVALID_BAID",
                ],
            },
        ],
    },
    {
        "id": "driver-reset-power-wake",
        "description": "Recovered Apple NET_MANAGER and local newstate paths make sleep, wake, and driver-reset behavior explicit.",
        "paths": [
            "docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/wifi_bundle_full_v3/72_WCLNetManager_fully_symbolic_FSM_checked.yaml",
            "itlwm/hal_iwx/ItlIwx.cpp",
        ],
        "checks": [
            {
                "path": "docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/wifi_bundle_full_v3/72_WCLNetManager_fully_symbolic_FSM_checked.yaml",
                "tokens": ["sleep_wake", "driver_reset", "WAKE", "DRIVER_RESET"],
            },
            {
                "path": "itlwm/hal_iwx/ItlIwx.cpp",
                "tokens": [
                    "if (sc->sc_flags & IWX_FLAG_SHUTDOWN)",
                    "task_add(systq, &sc->init_task)",
                    "iwx_add_task(sc, sc->sc_nswq, &sc->newstate_task)",
                ],
            },
        ],
    },
]


def read_text(project_relative_path):
    path = PROJECT_ROOT / project_relative_path
    if not path.exists():
        raise FileNotFoundError(project_relative_path)
    return path.read_text(encoding="utf-8", errors="replace")


def missing_tokens(project_relative_path, tokens):
    text = read_text(project_relative_path)
    return [token for token in tokens if token not in text]


def parse_contract_names(marker):
    text = read_text("AirportItlwm/TahoeStateMachineClosure.hpp")
    start = text.index(marker)
    body_start = text.index("{", start) + 1
    body_end = text.index("\n};", body_start)
    body = text[body_start:body_end]
    return set(re.findall(r'\{\s*"([^"]+)",', body))


def state_machine_contract_names():
    return parse_contract_names("static const StateMachineContract kStateMachineContracts[]")


def producer_consumer_contract_ids():
    return parse_contract_names("static const ProducerConsumerChain kProducerConsumerChains[]")


def reference_index():
    return {case["id"]: case for case in REFERENCE_CASES}


def collect_mismatches():
    mismatches = []
    expected_machines = {machine["name"] for machine in STATE_MACHINES}
    expected_chains = {chain["id"] for chain in PRODUCER_CONSUMER_CHAINS}
    contract_machines = state_machine_contract_names()
    contract_chains = producer_consumer_contract_ids()
    refs = reference_index()

    for name in sorted(expected_machines - contract_machines):
        mismatches.append({"kind": "missing_state_machine_contract", "name": name})
    for name in sorted(contract_machines - expected_machines):
        mismatches.append({"kind": "unexpected_state_machine_contract", "name": name})
    for chain_id in sorted(expected_chains - contract_chains):
        mismatches.append({"kind": "missing_producer_consumer_contract", "id": chain_id})
    for chain_id in sorted(contract_chains - expected_chains):
        mismatches.append({"kind": "unexpected_producer_consumer_contract", "id": chain_id})

    for case in REFERENCE_CASES:
        missing = missing_tokens(case["path"], case["tokens"])
        if missing:
            mismatches.append({
                "kind": "apple_reference_token_mismatch",
                "id": case["id"],
                "path": case["path"],
                "missing_tokens": missing,
            })

    for machine in STATE_MACHINES:
        for reference_id in machine["reference_ids"]:
            if reference_id not in refs:
                mismatches.append({
                    "kind": "missing_reference_case",
                    "machine": machine["name"],
                    "reference_id": reference_id,
                })
        for check in machine["implementation_checks"]:
            missing = missing_tokens(check["path"], check["tokens"])
            if missing:
                mismatches.append({
                    "kind": "state_machine_implementation_token_mismatch",
                    "machine": machine["name"],
                    "path": check["path"],
                    "missing_tokens": missing,
                })

    for chain in PRODUCER_CONSUMER_CHAINS:
        if not chain["ordered"]:
            mismatches.append({"kind": "ordering_violation", "id": chain["id"]})
        if not chain["backpressure_safe"]:
            mismatches.append({"kind": "backpressure_violation", "id": chain["id"]})
        for check in chain["implementation_checks"]:
            missing = missing_tokens(check["path"], check["tokens"])
            if missing:
                mismatches.append({
                    "kind": "chain_implementation_token_mismatch",
                    "id": chain["id"],
                    "path": check["path"],
                    "missing_tokens": missing,
                })

    for case in RECOVERY_CASES:
        for check in case["checks"]:
            missing = missing_tokens(check["path"], check["tokens"])
            if missing:
                mismatches.append({
                    "kind": "recovery_token_mismatch",
                    "id": case["id"],
                    "path": check["path"],
                    "missing_tokens": missing,
                })

    return mismatches


def build_report():
    mismatches = collect_mismatches()
    ordering_violations = [
        mismatch for mismatch in mismatches
        if mismatch["kind"] in {
            "ordering_violation",
            "missing_state_machine_contract",
            "missing_producer_consumer_contract",
            "state_machine_implementation_token_mismatch",
            "chain_implementation_token_mismatch",
        }
    ]
    backpressure_violations = [
        mismatch for mismatch in mismatches
        if mismatch["kind"] in {
            "backpressure_violation",
            "chain_implementation_token_mismatch",
        }
    ]

    return {
        "schema_version": "itlwm-state-machine-closure-report/v1",
        "selected_step_id": STEP_ID,
        "roadmap_item_id": ROADMAP_ITEM_ID,
        "goal_item_ids": GOAL_ITEM_IDS,
        "input_head": INPUT_HEAD,
        "capture_source": CAPTURE_SOURCE,
        "source_boundary": {
            "basis": "Committed Tahoe contract header, local implementation source, and committed recovered Apple WCL FSM/queue references.",
            "excluded_sources": ["synthetic", "self-reported", "project-gantt", "runtime-db"],
        },
        "state_machines": [
            {
                "name": machine["name"],
                "domain": machine["domain"],
                "states": machine["states"],
                "ordered_events": machine["ordered_events"],
                "apple_reference_case_ids": machine["reference_ids"],
                "implementation_paths": sorted({
                    check["path"] for check in machine["implementation_checks"]
                }),
                "recovery_case_ids": machine["recovery_case_ids"],
            }
            for machine in STATE_MACHINES
        ],
        "producer_consumer_chains": [
            {
                "id": chain["id"],
                "machine": chain["machine"],
                "producer": chain["producer"],
                "queue": chain["queue"],
                "consumer": chain["consumer"],
                "ordered": chain["ordered"],
                "backpressure_safe": chain["backpressure_safe"],
                "implementation_paths": sorted({
                    check["path"] for check in chain["implementation_checks"]
                }),
                "recovery_case_ids": chain["recovery_case_ids"],
            }
            for chain in PRODUCER_CONSUMER_CHAINS
        ],
        "recovery_cases": len(RECOVERY_CASES),
        "recovery_case_details": [
            {
                "id": case["id"],
                "description": case["description"],
                "paths": case["paths"],
            }
            for case in RECOVERY_CASES
        ],
        "apple_reference_cases": [
            {
                "id": case["id"],
                "path": case["path"],
                "assertions_checked": len(case["tokens"]),
            }
            for case in REFERENCE_CASES
        ],
        "ordering_violations": len(ordering_violations),
        "backpressure_violations": len(backpressure_violations),
        "self_evidence": False,
        "mismatch_count": len(mismatches),
        "mismatches": mismatches,
        "metrics": {
            "state_machine_count": len({machine["name"] for machine in STATE_MACHINES}),
            "producer_consumer_chain_count": len({
                chain["id"] for chain in PRODUCER_CONSUMER_CHAINS
            }),
            "recovery_case_count": len(RECOVERY_CASES),
            "apple_reference_case_count": len(REFERENCE_CASES),
        },
        "verification": {
            "write_command": "python3 scripts/state_machine_closure_report.py --write evidence/state/state_machine_closure_report.json",
            "check_command": "python3 scripts/state_machine_closure_report.py --check evidence/state/state_machine_closure_report.json",
        },
    }


def assert_typed_requirements(report):
    errors = []
    if report.get("self_evidence") is not False:
        errors.append("self_evidence must be false")
    if report.get("ordering_violations") != 0:
        errors.append("ordering_violations must be 0")
    if report.get("backpressure_violations") != 0:
        errors.append("backpressure_violations must be 0")
    if report.get("mismatch_count") != 0:
        errors.append("mismatch_count must be 0")
    if len({item["name"] for item in report.get("state_machines", [])}) < 4:
        errors.append("state_machines[].name must have at least 4 distinct values")
    if len({item["id"] for item in report.get("producer_consumer_chains", [])}) < 4:
        errors.append("producer_consumer_chains[].id must have at least 4 distinct values")
    if report.get("recovery_cases", 0) < 3:
        errors.append("recovery_cases must be at least 3")
    if errors:
        raise AssertionError("; ".join(errors))


def write_report(path):
    report = build_report()
    assert_typed_requirements(report)
    output = json.dumps(report, indent=2) + "\n"
    target = PROJECT_ROOT / path
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(output, encoding="utf-8")


def check_report(path):
    report = build_report()
    assert_typed_requirements(report)
    expected = json.dumps(report, indent=2) + "\n"
    actual_path = PROJECT_ROOT / path
    actual = actual_path.read_text(encoding="utf-8")
    if actual != expected:
        print(f"{path} is not the deterministic state-machine closure report", file=sys.stderr)
        return 1
    print(
        "state-machine closure report ok: "
        f"{report['metrics']['state_machine_count']} machines, "
        f"{report['metrics']['producer_consumer_chain_count']} chains, "
        f"{report['metrics']['recovery_case_count']} recovery cases"
    )
    return 0


def main():
    parser = argparse.ArgumentParser(description="Generate or verify Tahoe state-machine closure evidence.")
    parser.add_argument("--write", metavar="PATH", help="write deterministic report JSON")
    parser.add_argument("--check", metavar="PATH", help="check deterministic report JSON")
    args = parser.parse_args()

    if bool(args.write) == bool(args.check):
        parser.error("choose exactly one of --write or --check")
    if args.write:
        write_report(args.write)
        return 0
    return check_report(args.check)


if __name__ == "__main__":
    sys.exit(main())
