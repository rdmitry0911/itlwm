#!/usr/bin/env python3
"""Generate and verify WOW_TEST false-success quarantine evidence."""

import argparse
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/wow_test_quarantine_report.json"
NOTE = ROOT / "docs/reference/CR-479-wow-test-quarantine-20260714.md"
INVENTORY = ROOT / "docs/tahoe_discrepancy_inventory.md"
SIGNAL_AUDIT = ROOT / "docs/tahoe_signal_chain_audit.md"
CPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.cpp"
HPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.hpp"
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


def local_disposition(mode):
    if mode < 1 or mode > 600:
        return "bad_argument"
    return "unsupported"


def report():
    cpp = CPP.read_text(encoding="utf-8")
    hpp = HPP.read_text(encoding="utf-8")
    note = " ".join(NOTE.read_text(encoding="utf-8").split())
    inventory = INVENTORY.read_text(encoding="utf-8")
    signal_audit = SIGNAL_AUDIT.read_text(encoding="utf-8")
    setter = section(
        cpp,
        "setWOW_TEST(apple80211_wow_test_data *data)",
        "setHT_CAPABILITY",
    )

    return {
        "schema": "itlwm-wow-test-quarantine-v1",
        "source_base_revision": "0e3fea413894832461e3f6225818b4cd2ed643c5",
        "reference": {
            "image_sha256": "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
            "infra_wrapper": "0x10001827c",
            "core_vtable_symbol": "0x1003a10d8",
            "core_vptr_address_point": "0x1003a10e8",
            "core_vtable_offset": "0x518",
            "core_vtable_cell": "0x1003a1600",
            "core_setter": "0x100120ed2",
            "config_entry": "0x100120f20",
            "event_bit": "0x4c",
            "add_event_bit": "0x1001e25ae",
            "write_event_bit_field": "0x10011ef7e",
            "firmware_iovar": "wake_event",
            "firmware_set": "0x10017b6e6",
            "valid_range": "1..600",
            "invalid_status": "0xe00002c2",
            "retries": 5,
            "success_state_offset": "0x2508",
        },
        "local": {
            "backend_wow_test_owner": False,
            "request_false_success": False,
            "valid_input_return_is_apple_parity": False,
        },
        "checks": {
            "reference_note": all(
                token in note
                for token in (
                    "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
                    "`0x10001827c`",
                    "`+0x518`",
                    "`0x1003a10d8`",
                    "`+0x10`",
                    "`0x1003a1600`",
                    "`0x100120ed2`",
                    "`1..600`",
                    "`0xe00002c2`",
                    "`0x100120f20`",
                    "five times",
                    "`0x4c`",
                    "`0x1001e25ae`",
                    "`0x10011ef7e`",
                    "runIOVarSet(\"wake_event\")",
                    "`0x10017b6e6`",
                    "`+0x2508`",
                    "not a claim of exact Apple null parity",
                    "does not claim Apple valid-input return-code parity",
                )
            ),
            "setter_preserves_range_and_quarantines_valid": all(
                token in setter
                for token in (
                    "if (raw == nullptr)",
                    "return static_cast<IOReturn>(0xe00002c2);",
                    "uint32_t mode = *reinterpret_cast<uint32_t *>(raw + 4);",
                    "if (mode < 1 || mode > 600)",
                    "return kIOReturnUnsupported;",
                )
            )
            and "return kIOReturnSuccess;" not in setter
            and "for (int retries" not in setter
            and setter.index("return kIOReturnUnsupported;")
            > setter.index("if (mode < 1 || mode > 600)"),
            "exact_local_range": (
                local_disposition(0) == "bad_argument"
                and local_disposition(1) == "unsupported"
                and local_disposition(600) == "unsupported"
                and local_disposition(601) == "bad_argument"
            ),
            "fake_state_removed": all(
                token not in cpp and token not in hpp
                for token in (
                    "cachedWowTestMode",
                    "cachedWowEnabled",
                )
            )
            and "setWoWEnabled(" not in setter,
            "interface_slot_retained": "virtual IOReturn setWOW_TEST(apple80211_wow_test_data *) override;"
            in hpp,
            "scoped_backend_absent": all(
                not source_contains(token)
                for token in (
                    "wake_event",
                    "configureWoWTestModeEntry",
                    "writeEventBitField",
                )
            ),
            "historical_claims_corrected": (
                "WoW Test correction:" in inventory
                and "WoW Test correction:" in signal_audit
                and "setWOW_TEST` matches the recovered 1..600 gate" not in inventory
                and "setWOW_TEST` already matched the recovered public 1..600 gate"
                not in signal_audit
                and "WOW_TEST` no longer behaves as a scalar-only cache write" not in inventory
                and "setWOW_TEST(...)` now mirrors Apple's externally visible retry/enable"
                not in signal_audit
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
        raise ValueError("WOW_TEST quarantine checks failed: " + ", ".join(failed))
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
        print(f"WOW_TEST quarantine validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
