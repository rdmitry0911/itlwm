#!/usr/bin/env python3
"""Generate and verify CONGESTION_CTRL_IND traffic-monitor quarantine evidence."""

import argparse
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/congestion_ctrl_ind_traffic_monitor_quarantine_report.json"
NOTE = ROOT / "docs/reference/CR-479-congestion-ctrl-ind-traffic-monitor-quarantine-20260714.md"
INVENTORY = ROOT / "docs/tahoe_discrepancy_inventory.md"
SIGNAL_AUDIT = ROOT / "docs/tahoe_signal_chain_audit.md"
OFFSET_NOTE = ROOT / "docs/reference/AppleBCMWLAN_qos_dynsar_offsets_2026_04_27.md"
CPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.cpp"
HPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.hpp"
REGISTRY = ROOT / "AirportItlwm/TahoeOwnerRegistry.hpp"
CONTRACTS = ROOT / "AirportItlwm/TahoeQosDynsarContracts.hpp"
INFRA_PROTOCOL = ROOT / "include/Airport/IO80211InfraProtocol.h"
PAYLOAD_BUILDERS_TEST = ROOT / "tests/tahoe_payload_builders_test.cpp"
SOURCE_ROOTS = (
    ROOT / "AirportItlwm",
    ROOT / "include",
    ROOT / "itl80211",
    ROOT / "itlwm",
)


def section(source, begin, end):
    start = source.index(begin)
    return source[start:source.index(end, start)]


def source_contains(token):
    for root in SOURCE_ROOTS:
        for path in root.rglob("*"):
            if path.suffix not in {".c", ".cc", ".cpp", ".h", ".hpp"}:
                continue
            if token in path.read_text(encoding="utf-8", errors="ignore"):
                return True
    return False


def report():
    cpp = CPP.read_text(encoding="utf-8")
    hpp = HPP.read_text(encoding="utf-8")
    registry = REGISTRY.read_text(encoding="utf-8")
    contracts = CONTRACTS.read_text(encoding="utf-8")
    infra_protocol = INFRA_PROTOCOL.read_text(encoding="utf-8")
    payload_builders_test = PAYLOAD_BUILDERS_TEST.read_text(encoding="utf-8")
    note = " ".join(NOTE.read_text(encoding="utf-8").split())
    inventory = INVENTORY.read_text(encoding="utf-8")
    signal_audit = SIGNAL_AUDIT.read_text(encoding="utf-8")
    offset_note = OFFSET_NOTE.read_text(encoding="utf-8")
    setter = section(
        cpp,
        "setCONGESTION_CTRL_IND(apple80211_congestion_control_indication *data)",
        "setLMTPC_CONFIG(apple80211_lmtpc_config *data)",
    )
    local_qos_state = cpp + hpp + registry + contracts + payload_builders_test
    corrected_docs = signal_audit + inventory + offset_note

    return {
        "schema": "itlwm-congestion-ctrl-ind-traffic-monitor-quarantine-v1",
        "source_base_revision": "89e3eed712add8cee44f825811751149caff5e92",
        "reference": {
            "image_sha256": "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
            "infra_wrapper": "0x1000192fc",
            "core_setter": "0x1001429f4",
            "core_log_vtable_offset": "+0x7a0",
            "effective_carrier_byte_offset": "+0x0",
            "core_state_root_offset": "+0x48",
            "traffic_monitor_state_offset": "+0x89d2",
            "collector": "0x10013d482",
            "collector_return": "0x10013d5a9",
            "traffic_monitor_callback": "0x10013d5d6",
            "traffic_monitor_collect_calls": [
                "0x10013d747",
                "0x10013d85b",
            ],
        },
        "local": {
            "opaque_carrier_abi_defined": False,
            "bsd_ioc_route": False,
            "traffic_monitor_backend": False,
            "request_false_success": False,
            "synthetic_registry_state": False,
            "null_return_is_apple_parity": False,
            "valid_input_return_is_apple_parity": False,
        },
        "checks": {
            "reference_note": all(
                token in note
                for token in (
                    "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
                    "0x1000192fc",
                    "0x1001429f4",
                    "no NULL return contract",
                    "virtual +0x7a0",
                    "effective carrier byte +0",
                    "(Core + 0x48) + 0x89d2",
                    "0x10013d482",
                    "0x10013d5a9",
                    "trafficMonitorCallback()",
                    "0x10013d5d6",
                    "0x10013d747",
                    "0x10013d85b",
                    "does not claim Apple NULL or valid-input status parity",
                )
            ),
            "setter_retains_local_null_safety_and_quarantines_without_reading": all(
                token in setter
                for token in (
                    "if (data == nullptr)",
                    "return kIOReturnBadArgumentTahoe;",
                    "(void)data;",
                    "return kIOReturnUnsupported;",
                    "has no matching",
                )
            )
            and all(
                token not in setter
                for token in (
                    "data->",
                    "reinterpret_cast",
                    "tahoeCongestionControlIndication",
                    "syncCongestionControlIndication",
                    "return kIOReturnSuccess;",
                )
            ),
            "synthetic_state_removed": all(
                token not in local_qos_state
                for token in (
                    "tahoeCongestionControlIndication",
                    "congestionControlIndication",
                    "syncCongestionControlIndication",
                    "kCongestionControlIndicationOffset",
                    "boolCarrier",
                )
            ),
            "virtual_slot_and_forward_only_abi_retained": (
                "virtual IOReturn setCONGESTION_CTRL_IND(apple80211_congestion_control_indication *) override;"
                in hpp
                and "struct apple80211_congestion_control_indication;" in infra_protocol
                and not source_contains(
                    "struct apple80211_congestion_control_indication {"
                )
            ),
            "no_bsd_ioc_or_scoped_traffic_backend": all(
                not source_contains(token)
                for token in (
                    "APPLE80211_IOC_CONGESTION_CTRL_IND",
                    "collectRealTimeAppCongestionState(",
                    "trafficMonitorCallback(",
                )
            ),
            "historical_claims_corrected": (
                "2026-07-14 correction:" in signal_audit
                and "traffic-monitor state" in signal_audit
                and "Q13 correction: CONGESTION_CTRL_IND traffic-monitor quarantine"
                in inventory
                and "25C56 correction: congestion indication" in offset_note
                and "+0x79d2" not in corrected_docs
            ),
        },
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--write", action="store_true")
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    if args.write == args.check:
        parser.error("select exactly one of --write or --check")

    value = report()
    failed = [key for key, passed in value["checks"].items() if not passed]
    if failed:
        raise ValueError(
            "CONGESTION_CTRL_IND traffic-monitor quarantine checks failed: "
            + ", ".join(failed)
        )
    rendered = json.dumps(value, indent=2, sort_keys=True) + "\n"
    if args.write:
        OUTPUT.parent.mkdir(parents=True, exist_ok=True)
        OUTPUT.write_text(rendered, encoding="utf-8")
    elif not OUTPUT.exists() or OUTPUT.read_text(encoding="utf-8") != rendered:
        raise ValueError("checked-in report differs; rerun with --write")
    print(rendered, end="")


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print(
            f"CONGESTION_CTRL_IND traffic-monitor quarantine validation failed: {exc}",
            file=sys.stderr,
        )
        sys.exit(1)
