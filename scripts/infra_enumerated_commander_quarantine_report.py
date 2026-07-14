#!/usr/bin/env python3
"""Generate and verify INFRA_ENUMERATED Commander quarantine evidence."""

import argparse
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/infra_enumerated_commander_quarantine_report.json"
NOTE = ROOT / "docs/reference/CR-479-infra-enumerated-commander-quarantine-20260714.md"
INVENTORY = ROOT / "docs/tahoe_discrepancy_inventory.md"
SIGNAL_AUDIT = ROOT / "docs/tahoe_signal_chain_audit.md"
CPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.cpp"
HPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.hpp"
INFRA_PROTOCOL = ROOT / "include/Airport/IO80211InfraProtocol.h"
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
    infra_protocol = INFRA_PROTOCOL.read_text(encoding="utf-8")
    note = " ".join(NOTE.read_text(encoding="utf-8").split())
    inventory = INVENTORY.read_text(encoding="utf-8")
    signal_audit = SIGNAL_AUDIT.read_text(encoding="utf-8")
    setter = section(
        cpp,
        "setINFRA_ENUMERATED(apple80211_infra_enumerated *data)",
        "setWCL_SCAN_REQ(apple80211ScanRequest *req)",
    )

    return {
        "schema": "itlwm-infra-enumerated-commander-quarantine-v1",
        "source_base_revision": "cf1fc1a8181db9ee9bcf7c77d2ca61f2d3657694",
        "reference": {
            "image_sha256": "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
            "infra_wrapper": "0x10001936c",
            "core_setter": "0x100142bf0",
            "null_return": "0xe00002bc",
            "effective_carrier_offset": "+0x0",
            "core_owner_offset": "+0x48",
            "commander_offset": "+0x1520",
            "commander_terminal": "0x100181e04",
            "commander_state_offset": "+0x40",
            "command_timeout": "0x61a8",
            "command_timeout_state_offset": "+0x10c",
            "factory_state_offset": "+0xa8",
            "factory_boot_arg": "wlan.factory",
        },
        "local": {
            "opaque_carrier_abi_defined": False,
            "bsd_ioc_route": False,
            "commander_backend": False,
            "request_false_success": False,
            "valid_input_return_is_apple_parity": False,
        },
        "checks": {
            "reference_note": all(
                token in note
                for token in (
                    "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
                    "`0x10001936c`",
                    "`0x100142bf0`",
                    "`0xe00002bc`",
                    "byte `+0`",
                    "(Core + 0x48) + 0x1520",
                    "deviceBootStationaryNotification()",
                    "`0x100181e04`",
                    "Commander `+0x40`",
                    "`0x61a8`",
                    "state `+0x10c`",
                    "`wlan.factory`",
                    "`0x1388 + state[+0xa8]`",
                    "does not claim Apple byte-zero",
                    "valid-input return-code parity",
                )
            ),
            "setter_retains_null_and_quarantines_without_reading": all(
                token in setter
                for token in (
                    "if (!data)",
                    "return kIOReturnBadArgumentTahoe;",
                    "(void)data;",
                    "return kIOReturnUnsupported;",
                    "opaque forward declaration",
                )
            )
            and all(
                token not in setter
                for token in (
                    "data->",
                    "reinterpret_cast",
                    "cachedInfraEnumerated",
                    "return kIOReturnSuccess;",
                )
            ),
            "synthetic_state_removed": "cachedInfraEnumerated" not in cpp + hpp,
            "virtual_slot_retained": (
                "virtual IOReturn setINFRA_ENUMERATED(apple80211_infra_enumerated *data) override;"
                in hpp
            ),
            "opaque_abi_remains_forward_only": (
                "struct apple80211_infra_enumerated;" in infra_protocol
                and not source_contains("struct apple80211_infra_enumerated {")
                and not source_contains("APPLE80211_IOC_INFRA_ENUMERATED")
            ),
            "scoped_commander_backend_absent": all(
                not source_contains(token)
                for token in (
                    "AppleBCMWLANCommander",
                    "deviceBootStationaryNotification(",
                    "wlan.factory",
                    "setCommandTimeout(",
                    "configureCommandTimeout(",
                )
            ),
            "historical_claims_corrected": (
                "### 2026-07-14 correction: `INFRA_ENUMERATED` is Commander-backed"
                in signal_audit
                and "INFRA_ENUMERATED correction:" in inventory
                and "This one is a true minimal producer contract" not in signal_audit
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
        raise ValueError("INFRA_ENUMERATED Commander quarantine checks failed: " + ", ".join(failed))
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
        print(f"INFRA_ENUMERATED Commander quarantine validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
