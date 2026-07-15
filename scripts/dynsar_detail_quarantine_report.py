#!/usr/bin/env python3
"""Generate and verify DYNSAR_DETAIL no-producer quarantine evidence."""

import argparse
import hashlib
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/dynsar_detail_quarantine_report.json"
NOTE = ROOT / "docs/reference/CR-494-dynsar-detail-no-producer-quarantine-20260715.md"
RAW = ROOT / "docs/reference/artifacts/dynsar-detail-25c56/raw.txt"
RAW_MANIFEST = RAW.with_name("SHA256SUMS.txt")
SIGNAL_AUDIT = ROOT / "docs/tahoe_signal_chain_audit.md"
INVENTORY = ROOT / "docs/tahoe_discrepancy_inventory.md"
CPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.cpp"
HPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.hpp"
INFRA = ROOT / "include/Airport/IO80211InfraProtocol.h"
CONTRACTS = ROOT / "AirportItlwm/TahoeQosDynsarContracts.hpp"
REGISTRY = ROOT / "AirportItlwm/TahoeOwnerRegistry.hpp"
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
            if not path.is_file() or path.suffix not in {".c", ".cc", ".cpp", ".h", ".hpp"}:
                continue
            if token in path.read_text(encoding="utf-8", errors="ignore"):
                return True
    return False


def report():
    cpp = CPP.read_text(encoding="utf-8")
    hpp = HPP.read_text(encoding="utf-8")
    infra = INFRA.read_text(encoding="utf-8")
    contracts = CONTRACTS.read_text(encoding="utf-8")
    registry = REGISTRY.read_text(encoding="utf-8")
    note = " ".join(NOTE.read_text(encoding="utf-8").split())
    raw = RAW.read_text(encoding="utf-8")
    manifest = RAW_MANIFEST.read_text(encoding="utf-8")
    signal_audit = SIGNAL_AUDIT.read_text(encoding="utf-8")
    inventory = INVENTORY.read_text(encoding="utf-8")
    getter = section(
        cpp,
        "getDYNSAR_DETAIL(apple80211_dynsar_detail *data)",
        "getSLOW_WIFI_FEATURE_ENABLED(apple80211_slow_wifi_feature_enabled *data)",
    )
    raw_digest = hashlib.sha256(RAW.read_bytes()).hexdigest()
    qos_surfaces = cpp + hpp + infra

    return {
        "schema": "itlwm-dynsar-detail-no-producer-quarantine-v1",
        "source_base_revision": "fe31c2e85c5278335ee7c676ce17516062bf2f48",
        "reference": {
            "image_sha256": "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
            "image_uuid_x86_64": "149C0AD1-A92F-35BC-AA69-5C8815C5421E",
            "infra_slot": 518,
            "infra_get_wrapper": "0x1000178e0",
            "core_get_vtable_offset": "0x338",
            "core_getter": "0x1001e11e4",
            "tx_power_manager_from_core": "0x48+0x1598",
            "detail_cur_id": "0x1000b439a",
            "detail_circled": "0x1000b43b2",
            "detail_report": "0x1000b43ca",
            "report_copy_bytes": 11520,
        },
        "local": {
            "detail_producer": False,
            "synthetic_success": False,
            "null_return_is_apple_parity": False,
            "valid_input_return_is_apple_parity": False,
            "runtime_selector_invocation": False,
        },
        "checks": {
            "reference_raw_manifest_matches": manifest == f"{raw_digest}  raw.txt\n",
            "reference_raw_has_manager_backed_detail_anchors": all(
                token in raw
                for token in (
                    "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
                    "149C0AD1-A92F-35BC-AA69-5C8815C5421E",
                    "0x1000178e0",
                    "0x338(%rax)",
                    "0x1001e11e4",
                    "pushq  $0x16",
                    "testq  %rsi",
                    "0x8(%rsi)",
                    "$0x1, (%rbx)",
                    "0x48(%rdi)",
                    "0x1598(%rax)",
                    "0x1000b439a",
                    "getDynSARDetailCurId",
                    "0x1000b43b2",
                    "getDynSARDetailCircled",
                    "0x1000b43ca",
                    "getDynSARDetailReportPerSlicePerAnt",
                    "$0x2d00",
                    "memcpy",
                    "xorl   %eax, %eax",
                )
            ),
            "reference_note_has_scope_and_nonclaim": all(
                token in note
                for token in (
                    "[518]",
                    "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
                    "149C0AD1-A92F-35BC-AA69-5C8815C5421E",
                    "0x1000178e0",
                    "0x338",
                    "0x1001e11e4",
                    "TxPowerManager",
                    "not Apple null-input, valid-input return-code, full carrier-layout, version, TxPowerManager/Core-state, firmware, or runtime-selector parity",
                )
            ),
            "active_slot_and_opaque_abi_remain": (
                "// [518]" in hpp
                and "getDYNSAR_DETAIL" in hpp
                and "fail-closed" in hpp
                and "struct apple80211_dynsar_detail;" in infra
                and "// [518]" in infra
                and "getDYNSAR_DETAIL" in infra
                and "struct tahoeDynsarDetailRequest" in cpp
                and "sizeof(tahoeDynsarDetailRequest) == 0x2d18" in cpp
            ),
            "local_raw_safety_boundary_is_retained": (
                "if (raw == nullptr || raw->entry_index >= 4)" in getter
                and "return static_cast<IOReturn>(kApple80211ErrInvalidArgumentRaw);" in getter
            ),
            "nonnull_getter_fails_closed_without_output": all(
                token in getter
                for token in (
                    "(void)data;",
                    "return kIOReturnUnsupported;",
                )
            )
            and all(
                token not in getter
                for token in (
                    "raw->version",
                    "raw->header0",
                    "raw->header1",
                    "raw->payload",
                    "memcpy",
                    "kIOReturnSuccess",
                    "cachedDynsar",
                )
            ),
            "dead_dynsar_cache_is_removed": all(
                not source_contains(token)
                for token in (
                    "cachedDynsarHeader0",
                    "cachedDynsarHeader1",
                    "cachedDynsarPayload",
                )
            ),
            "no_matching_local_detail_producer": all(
                not source_contains(token)
                for token in (
                    "getDynSARDetailCurId",
                    "getDynSARDetailCircled",
                    "getDynSARDetailReportPerSlicePerAnt",
                )
            ),
            "separate_qos_dynsar_surfaces_are_preserved": (
                "namespace TahoeQosDynsarContracts" in contracts
                and "qosDynsar" in registry
                and all(
                    token in qos_surfaces
                    for token in (
                        "getSLOW_WIFI_FEATURE_ENABLED",
                        "getWCL_LOW_LATENCY_INFO",
                        "getWCL_GET_TX_BLANKING_STATUS",
                    )
                )
            ),
            "historical_cache_classification_is_superseded": (
                "## 2026-07-15 correction: DYNSAR_DETAIL no-producer quarantine"
                in signal_audit
                and "previous local cache-success" in signal_audit
                and "## 2026-07-15 correction: DYNSAR_DETAIL no-producer quarantine"
                in inventory
                and "previous local cache-success" in inventory
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
        raise ValueError("DYNSAR_DETAIL no-producer checks failed: " + ", ".join(failed))
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
        print(f"DYNSAR_DETAIL no-producer validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
