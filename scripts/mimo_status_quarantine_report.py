#!/usr/bin/env python3
"""Generate and verify MIMO status quarantine evidence."""

import argparse
import hashlib
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/mimo_status_quarantine_report.json"
NOTE = ROOT / "docs/reference/CR-479-mimo-status-quarantine-20260714.md"
RAW = ROOT / "docs/reference/artifacts/mimo-status-25c56/raw.txt"
RAW_MANIFEST = RAW.with_name("SHA256SUMS.txt")
CPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.cpp"
HPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.hpp"
V2 = ROOT / "AirportItlwm/AirportItlwmV2.cpp"
INFRA = ROOT / "include/Airport/IO80211InfraProtocol.h"
TEST = ROOT / "tests/tahoe_payload_builders_test.cpp"
PAYLOAD_REPORT = ROOT / "scripts/payload_parity_report.py"
HELPER = ROOT / "AirportItlwm/TahoeMimoContracts.hpp"


def section(source, begin, end):
    start = source.index(begin)
    return source[start:source.index(end, start)]


def report():
    cpp = CPP.read_text(encoding="utf-8")
    hpp = HPP.read_text(encoding="utf-8")
    v2 = V2.read_text(encoding="utf-8")
    infra = INFRA.read_text(encoding="utf-8")
    test = TEST.read_text(encoding="utf-8")
    payload_report = PAYLOAD_REPORT.read_text(encoding="utf-8")
    note = NOTE.read_text(encoding="utf-8")
    raw = RAW.read_text(encoding="utf-8")
    manifest = RAW_MANIFEST.read_text(encoding="utf-8")
    getter = section(cpp, "getMIMO_STATUS(apple80211_mimo_status *data)", "getWCL_FW_HOT_CHANNELS")
    raw_digest = hashlib.sha256(RAW.read_bytes()).hexdigest()

    return {
        "schema": "itlwm-mimo-status-quarantine-v1",
        "source_base_revision": "6ea70cc4dab9ad32bfd29aa4ec4ed44b3c17a122",
        "reference": {
            "image_sha256": "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
            "image_uuid_x86_64": "149C0AD1-A92F-35BC-AA69-5C8815C5421E",
            "infra_slot": 516,
            "infra_wrapper": "0x100017870",
            "core_getter": "0x10011989c",
            "feature_bit": "0x2c",
            "feature_disabled_status": "0xe00002c7",
            "firmware_iovar": "mimo_ps_status",
            "response_bytes": 10,
        },
        "local": {
            "false_success": False,
            "null_return_is_apple_parity": False,
            "valid_input_return_is_apple_parity": False,
            "runtime_selector_invocation": False,
        },
        "checks": {
            "reference_raw_manifest_matches": manifest == f"{raw_digest}  raw.txt\n",
            "reference_raw_has_feature_and_iovar_contract": all(
                token in raw
                for token in (
                    "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
                    "149C0AD1-A92F-35BC-AA69-5C8815C5421E",
                    "0x100017870",
                    "0x10011989c",
                    "$0x2c",
                    "0xe00002c7",
                    "mimo_ps_status",
                    "runIOVarGet",
                    "0x9(%r14)",
                )
            ),
            "reference_note_has_scope_and_nonclaim": all(
                token in note
                for token in (
                    "slot `[516]`",
                    "0x100017870",
                    "0x10011989c",
                    "mimo_ps_status",
                    "not Apple null or valid-input return-code\nparity",
                )
            ),
            "active_v2_slot_remains": (
                "// [516]" in hpp
                and "getMIMO_STATUS" in hpp
                and "// [516]" in infra
                and "getMIMO_STATUS" in infra
                and "fNetIf = new AirportItlwmSkywalkInterface;" in v2
            ),
            "local_null_guard_is_retained": (
                "if (data == nullptr)" in getter
                and "return static_cast<IOReturn>(0xe00002c2);" in getter
            ),
            "nonnull_path_fails_closed_without_carrier": (
                "(void)data;" in getter
                and "return kIOReturnUnsupported;" in getter
                and "kIOReturnSuccess" not in getter
                and "memset" not in getter
                and "TahoeMimoContracts" not in getter
            ),
            "synthetic_helper_and_test_removed": (
                not HELPER.exists()
                and "TahoeMimoContracts" not in cpp
                and "TahoeMimoContracts" not in hpp
                and "TahoeMimoContracts" not in test
                and "testTahoeMimoContracts" not in test
                and "29 contracts" in test
                and "29 contracts" in payload_report
            ),
            "no_local_mimo_transport_in_getter": (
                "runIOVarGet" not in getter
                and "runIOVarSet" not in getter
                and "dispatchIOVar" not in getter
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
        raise ValueError("MIMO status quarantine checks failed: " + ", ".join(failed))
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
        print(f"MIMO status quarantine validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
