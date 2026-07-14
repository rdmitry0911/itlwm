#!/usr/bin/env python3
"""Generate and verify power debug info quarantine evidence."""

import argparse
import hashlib
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/power_debug_info_quarantine_report.json"
NOTE = ROOT / "docs/reference/CR-479-power-debug-info-quarantine-20260714.md"
RAW = ROOT / "docs/reference/artifacts/power-debug-info-25c56/raw.txt"
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
    getter = section(
        cpp,
        "getPOWER_DEBUG_INFO(apple80211_power_debug_info *data)",
        "getROAM_PROFILE",
    )
    raw_digest = hashlib.sha256(RAW.read_bytes()).hexdigest()

    return {
        "schema": "itlwm-power-debug-info-quarantine-v1",
        "source_base_revision": "407f62cf0e5b9223791e4cdb11a0186178a42af9",
        "reference": {
            "image_sha256": "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
            "image_uuid_x86_64": "149C0AD1-A92F-35BC-AA69-5C8815C5421E",
            "infra_slot": 480,
            "infra_wrapper": "0x100016ffc",
            "core_getter": "0x100133876",
            "core_vtable_offset": "0x358",
            "snapshot_source_offset": "0x2c50",
            "snapshot_bytes": 704,
        },
        "local": {
            "false_success": False,
            "null_return_is_apple_parity": False,
            "valid_input_return_is_apple_parity": False,
            "runtime_selector_invocation": False,
        },
        "checks": {
            "reference_raw_manifest_matches": manifest == f"{raw_digest}  raw.txt\n",
            "reference_raw_has_live_power_pipeline": all(
                token in raw
                for token in (
                    "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
                    "149C0AD1-A92F-35BC-AA69-5C8815C5421E",
                    "0x100016ffc",
                    "0x358(%rax)",
                    "0x100133876",
                    "0x4(%rsi)",
                    "isRejectingCommands",
                    "0x2c50",
                    "$0x2c0",
                    "getPowerStats",
                    "$0x4c",
                    "getInactivityPowerStats",
                    "0x714(%rbx)",
                )
            ),
            "reference_note_has_scope_and_nonclaim": all(
                token in note
                for token in (
                    "slot `[480]`",
                    "0x100016ffc",
                    "0x100133876",
                    "not Apple null, valid-input return-code,\noutput-layout, or feature-branch parity",
                )
            ),
            "active_v2_slot_remains": (
                "// [480]" in hpp
                and "getPOWER_DEBUG_INFO" in hpp
                and "// [480]" in infra
                and "getPOWER_DEBUG_INFO" in infra
                and "fNetIf = new AirportItlwmSkywalkInterface;" in v2
            ),
            "local_null_guard_is_retained": (
                "if (data == nullptr)" in getter
                and "return kIOReturnBadArgumentTahoe;" in getter
            ),
            "nonnull_path_fails_closed_without_synthetic_snapshot": (
                "(void)data;" in getter
                and "return kIOReturnUnsupported;" in getter
                and "kIOReturnSuccess" not in getter
                and "memset" not in getter
                and "0x580" not in getter
            ),
            "no_local_power_snapshot_backend_is_introduced": all(
                token not in getter
                for token in (
                    "getPowerStats",
                    "getInactivityPowerStats",
                    "isRejectingCommands",
                    "featureFlagIsBitSet",
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
        raise ValueError("power debug info quarantine checks failed: " + ", ".join(failed))
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
        print(f"power debug info quarantine validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
