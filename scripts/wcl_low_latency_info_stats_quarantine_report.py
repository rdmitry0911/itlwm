#!/usr/bin/env python3
"""Generate and verify WCL low-latency stats quarantine evidence."""

import argparse
import hashlib
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/wcl_low_latency_info_stats_quarantine_report.json"
NOTE = ROOT / "docs/reference/CR-479-wcl-low-latency-info-stats-quarantine-20260714.md"
RAW = ROOT / "docs/reference/artifacts/wcl-low-latency-info-stats-25c56/raw.txt"
RAW_MANIFEST = RAW.with_name("SHA256SUMS.txt")
CPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.cpp"
HPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.hpp"
INFRA = ROOT / "include/Airport/IO80211InfraProtocol.h"
REGISTRY = ROOT / "AirportItlwm/TahoeOwnerRegistry.hpp"


def section(source, begin, end):
    start = source.index(begin)
    return source[start:source.index(end, start)]


def report():
    cpp = CPP.read_text(encoding="utf-8")
    hpp = HPP.read_text(encoding="utf-8")
    infra = INFRA.read_text(encoding="utf-8")
    registry = REGISTRY.read_text(encoding="utf-8")
    note = NOTE.read_text(encoding="utf-8")
    raw = RAW.read_text(encoding="utf-8")
    manifest = RAW_MANIFEST.read_text(encoding="utf-8")
    getter = section(cpp, "getWCL_LOW_LATENCY_INFO_STATS", "getDYNSAR_DETAIL")
    config_getter = section(cpp, "getWCL_LOW_LATENCY_INFO(", "getWCL_GET_TX_BLANKING_STATUS")
    raw_digest = hashlib.sha256(RAW.read_bytes()).hexdigest()

    return {
        "schema": "itlwm-wcl-low-latency-info-stats-quarantine-v1",
        "source_base_revision": "bb76264e271dd36e8d85344534b8aab1b169ff99",
        "reference": {
            "image_sha256": "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
            "image_uuid_x86_64": "149C0AD1-A92F-35BC-AA69-5C8815C5421E",
            "infra_slot": 534,
            "infra_wrapper": "0x100017d60",
            "core_getter": "0x100141f06",
            "null_status": "0xe00002bc",
            "carrier_bytes": 124,
        },
        "local": {
            "false_success": False,
            "valid_input_return_is_apple_parity": False,
            "runtime_selector_invocation": False,
        },
        "checks": {
            "reference_raw_manifest_matches": manifest == f"{raw_digest}  raw.txt\n",
            "reference_raw_has_real_snapshot_anchors": all(
                token in raw
                for token in (
                    "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
                    "149C0AD1-A92F-35BC-AA69-5C8815C5421E",
                    "0x100017d60",
                    "0x100141f06",
                    "0xe00002bc",
                    "0x2c18",
                    "*0x288",
                    "0x4798",
                    "0x76c4",
                    "0x7500",
                    "0x7504",
                    "0x77b8",
                    "0x77bc",
                    "0x78c0",
                    "0x78fc",
                    "0x6c(%rbx)",
                )
            ),
            "reference_note_has_scope_and_nonclaim": all(
                token in note
                for token in (
                    "slot `[534]`",
                    "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
                    "0x100017d60",
                    "0x100141f06",
                    "not Apple valid-input return-code parity",
                )
            ),
            "active_slot_declarations_remain": (
                "// [534]" in hpp
                and "getWCL_LOW_LATENCY_INFO_STATS" in hpp
                and "// [534]" in infra
                and "getWCL_LOW_LATENCY_INFO_STATS" in infra
            ),
            "local_null_guard_is_retained": (
                "if (data == nullptr)" in getter
                and "return kIOReturnBadArgumentTahoe;" in getter
            ),
            "nonnull_path_fails_closed_without_zero_fill": (
                "(void)data;" in getter
                and "return kIOReturnUnsupported;" in getter
                and "memset" not in getter
                and "kIOReturnSuccess" not in getter
            ),
            "stats_owner_is_not_invented": (
                "lowLatencyStats" not in cpp
                and "LowLatencyInfoStats" not in registry
            ),
            "distinct_configuration_carrier_is_preserved": all(
                token in registry and token in config_getter
                for token in (
                    "lowLatencyEnabled",
                    "lowLatencyPowerSave",
                    "lowLatencyWindow",
                )
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
        raise ValueError("WCL low-latency stats quarantine checks failed: " + ", ".join(failed))
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
        print(f"WCL low-latency stats quarantine validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
