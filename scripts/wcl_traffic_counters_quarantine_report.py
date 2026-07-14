#!/usr/bin/env python3
"""Generate and verify WCL traffic counters quarantine evidence."""

import argparse
import hashlib
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/wcl_traffic_counters_quarantine_report.json"
NOTE = ROOT / "docs/reference/CR-479-wcl-traffic-counters-quarantine-20260714.md"
RAW = ROOT / "docs/reference/artifacts/wcl-traffic-counters-25c56/raw.txt"
RAW_MANIFEST = RAW.with_name("SHA256SUMS.txt")
CPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.cpp"
HPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.hpp"
V2 = ROOT / "AirportItlwm/AirportItlwmV2.cpp"
INFRA = ROOT / "include/Airport/IO80211InfraProtocol.h"


def section(source, begin, end):
    start = source.index(begin)
    return source[start:source.index(end, start)]


def report():
    cpp = CPP.read_text(encoding="utf-8")
    hpp = HPP.read_text(encoding="utf-8")
    v2 = V2.read_text(encoding="utf-8")
    infra = INFRA.read_text(encoding="utf-8")
    note = NOTE.read_text(encoding="utf-8")
    raw = RAW.read_text(encoding="utf-8")
    manifest = RAW_MANIFEST.read_text(encoding="utf-8")
    getter = section(cpp, "getWCL_TRAFFIC_COUNTERS", "getRSN_XE")
    raw_digest = hashlib.sha256(RAW.read_bytes()).hexdigest()

    return {
        "schema": "itlwm-wcl-traffic-counters-quarantine-v1",
        "source_base_revision": "8b271f7070db4a803dd6e83ddad31f563d33a5e0",
        "reference": {
            "image_sha256": "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
            "image_uuid_x86_64": "149C0AD1-A92F-35BC-AA69-5C8815C5421E",
            "infra_slot": 527,
            "infra_wrapper": "0x100017ca4",
            "core_getter": "0x100132b8c",
            "null_status": "0xe00002bc",
            "counter_qwords": 7,
        },
        "local": {
            "false_success": False,
            "valid_input_return_is_apple_parity": False,
            "runtime_selector_invocation": False,
        },
        "checks": {
            "reference_raw_manifest_matches": manifest == f"{raw_digest}  raw.txt\n",
            "reference_raw_has_real_counter_anchors": all(
                token in raw
                for token in (
                    "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
                    "149C0AD1-A92F-35BC-AA69-5C8815C5421E",
                    "0x100017ca4",
                    "0x428(%rax)",
                    "0x100132b8c",
                    "0xe00002bc",
                    "0x2c18",
                    "*0x368(%rax)",
                    "0x2a10",
                    "0x2a20",
                    "0x2a28",
                    "0x2c20",
                    "0x1000c80f0",
                    "0x100195ee1",
                    "0x30(%rbx)",
                )
            ),
            "reference_note_has_scope_and_nonclaim": all(
                token in note
                for token in (
                    "slot `[527]`",
                    "0x100017ca4",
                    "0x100132b8c",
                    "seven qwords",
                    "not Apple valid-input return-code or value\nparity",
                )
            ),
            "active_v2_slot_remains": (
                "// [527]" in hpp
                and "getWCL_TRAFFIC_COUNTERS" in hpp
                and "// [527]" in infra
                and "getWCL_TRAFFIC_COUNTERS" in infra
                and "fNetIf = new AirportItlwmSkywalkInterface;" in v2
            ),
            "local_null_guard_is_retained": (
                "if (data == nullptr)" in getter
                and "return kIOReturnBadArgumentTahoe;" in getter
            ),
            "nonnull_path_fails_closed_without_zero_fill": (
                "(void)data;" in getter
                and "return kIOReturnUnsupported;" in getter
                and "kIOReturnSuccess" not in getter
                and "memset" not in getter
            ),
            "no_local_counter_source_is_introduced": all(
                token not in getter
                for token in (
                    "getRealTimeNANTxPktCounter",
                    "get_mach_continuous_nanoseconds",
                    "wclTrafficCounters",
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
        raise ValueError("WCL traffic counters quarantine checks failed: " + ", ".join(failed))
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
        print(f"WCL traffic counters quarantine validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
