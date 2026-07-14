#!/usr/bin/env python3
"""Generate and verify LE_SCAN_PARAM direct-state correction evidence."""

import argparse
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/le_scan_param_direct_state_report.json"
NOTE = ROOT / "docs/reference/CR-479-le-scan-param-direct-state-20260714.md"
INVENTORY = ROOT / "docs/tahoe_discrepancy_inventory.md"
SIGNAL_AUDIT = ROOT / "docs/tahoe_signal_chain_audit.md"
CPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.cpp"
HPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.hpp"
TEST = ROOT / "tests/tahoe_payload_builders_test.cpp"
CONTRACT_HEADER = ROOT / "AirportItlwm/TahoeLeScanContracts.hpp"
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
    test = TEST.read_text(encoding="utf-8")
    note = " ".join(NOTE.read_text(encoding="utf-8").split())
    inventory = INVENTORY.read_text(encoding="utf-8")
    signal_audit = SIGNAL_AUDIT.read_text(encoding="utf-8")
    setter = section(
        cpp,
        "setLE_SCAN_PARAM(apple80211_le_scan_params *data)",
        "setAP_MODE(apple80211_apmode_data *data)",
    )

    return {
        "schema": "itlwm-le-scan-param-direct-state-v1",
        "source_base_revision": "6e5d2ad8aec905a06831d1fc3f5cd0097159ff7a",
        "reference": {
            "image_sha256": "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
            "infra_wrapper": "0x100019414",
            "core_setter": "0x100140d6c",
            "enable_offset": "+0x0",
            "peak_offset": "+0x4",
            "total_offset": "+0x8",
            "duty_offset": "+0xc",
            "enabled_count_offset": "+0x78c4",
            "disabled_count_offset": "+0x78c8",
            "peak_sum_offset": "+0x78cc",
            "total_sum_offset": "+0x78d0",
            "duty_bucket_offset": "+0x78d4",
            "duty_bucket_max": 6,
            "optional_reporter_offset": "+0x1588",
            "reporter": "0x10002e00c",
        },
        "local": {
            "btle_owner_implemented": False,
            "optional_reporter_implemented": False,
            "nonnull_direct_state_transition": True,
            "null_return_is_apple_parity": False,
        },
        "checks": {
            "reference_note": all(
                token in note
                for token in (
                    "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
                    "`0x100019414`",
                    "`0x100140d6c`",
                    "enable byte `+0`",
                    "peak dword `+0x4`",
                    "total dword `+0x8`",
                    "duty dword `+0xc`",
                    "`+0x78c4`",
                    "`+0x78c8`",
                    "`+0x78cc`",
                    "`+0x78d0`",
                    "`+0x78d4 + duty * 4`",
                    "`0x10002e00c`",
                    "`+0x1588`",
                    "does not claim Apple NULL or valid-input return-code parity",
                )
            ),
            "setter_preserves_safety_and_direct_state": all(
                token in setter
                for token in (
                    "if (data == nullptr)",
                    "return kIOReturnBadArgumentTahoe;",
                    "const auto *raw = reinterpret_cast<const uint8_t *>(data);",
                    "memcpy(&peak, raw + 4, sizeof(peak));",
                    "memcpy(&total, raw + 8, sizeof(total));",
                    "memcpy(&duty, raw + 12, sizeof(duty));",
                    "if (raw[0] != 0)",
                    "++leScanEnabledCount;",
                    "leScanPeakSum += peak;",
                    "leScanTotalSum += total;",
                    "++leScanDisabledCount;",
                    "if (duty <= 6)",
                    "++leScanDutyCount[duty];",
                    "return kIOReturnSuccess;",
                )
            )
            and "TahoeLeScanContracts" not in setter
            and "cachedLeScan" not in setter,
            "synthetic_owner_cache_removed": all(
                token not in cpp + hpp
                for token in (
                    "TahoeLeScanContracts",
                    "cachedLeScanOwnerState",
                    "hasCachedLeScanParams",
                )
            ),
            "synthetic_contract_test_removed": (
                not CONTRACT_HEADER.exists() and "TahoeLeScanContracts" not in test
            ),
            "direct_state_declared": all(
                token in hpp
                for token in (
                    "uint32_t leScanEnabledCount;",
                    "uint32_t leScanDisabledCount;",
                    "uint32_t leScanPeakSum;",
                    "uint32_t leScanTotalSum;",
                    "uint32_t leScanDutyCount[7];",
                )
            ),
            "optional_reporter_absent": not source_contains("reportBTLECnxStats("),
            "historical_claims_corrected": (
                "LE_SCAN_PARAM correction:" in signal_audit
                and "direct BTLE statistics update" in inventory
                and "BTLE reporting owner at core `+0x15a8`" not in signal_audit
                and "six dwords from" not in signal_audit
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
        raise ValueError("LE_SCAN_PARAM direct-state checks failed: " + ", ".join(failed))
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
        print(f"LE_SCAN_PARAM direct-state validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
