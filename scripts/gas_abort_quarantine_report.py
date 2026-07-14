#!/usr/bin/env python3
"""Generate and verify GAS abort false-success quarantine evidence."""

import argparse
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/gas_abort_quarantine_report.json"
NOTE = ROOT / "docs/reference/CR-479-gas-abort-quarantine-20260714.md"
SIGNAL_AUDIT = ROOT / "docs/tahoe_signal_chain_audit.md"
INVENTORY = ROOT / "docs/tahoe_discrepancy_inventory.md"
CPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.cpp"
HPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.hpp"
LEGACY_REPORT = ROOT / "scripts/gas_request_quarantine_report.py"
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
    note = NOTE.read_text(encoding="utf-8")
    signal_audit = SIGNAL_AUDIT.read_text(encoding="utf-8")
    inventory = INVENTORY.read_text(encoding="utf-8")
    legacy_report = LEGACY_REPORT.read_text(encoding="utf-8")
    setter = section(cpp, "setGAS_ABORT(void *)", "setWCL_LIMITED_AGGREGATION")

    return {
        "schema": "itlwm-gas-abort-quarantine-v1",
        "source_base_revision": "19cf0e670937cfd209a3d904f3048fea0bfc7177",
        "reference": {
            "image_sha256": "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
            "infra_wrapper": "0x100019b52",
            "infra_core_offset": "0x88",
            "core_virtual_offset": "0x540",
            "core_setter": "0x100137992",
            "gas_adapter_offset": "0x1560",
            "adapter_setter": "0x1001a171a",
            "feature_bit": "0x11",
            "issue_abort": "0x1000205e8",
            "abort_iovar": "anqpo_stop_query",
            "complete_event": "0xdc",
        },
        "local": {
            "backend_gas_adapter": False,
            "false_success": False,
            "input_pointer_is_not_interpreted": True,
            "valid_input_return_is_apple_parity": False,
        },
        "checks": {
            "reference_note": all(
                token in note
                for token in (
                    "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
                    "0x100019b52",
                    "`+0x88`",
                    "`+0x540`",
                    "0x100137992",
                    "`+0x1560`",
                    "0x1001a171a",
                    "`0x11`",
                    "0x1000205e8",
                    "anqpo_stop_query",
                    "`0xdc`",
                    "event-admission status",
                )
            ),
            "setter_quarantines_unconditionally": (
                "return kIOReturnUnsupported;" in setter
                and "return kIOReturnSuccess;" not in setter
                and "if (" not in setter
            ),
            "local_gas_backend_absent": all(
                not source_contains(token)
                for token in (
                    "AppleBCMWLANGASAdapter",
                    "issueGASAbort",
                    "anqpo_stop_query",
                    "sendGasCompleteEvent",
                    "runIOVarSet",
                )
            ),
            "legacy_request_report_decoupled": (
                '"setGAS_ABORT" not in gas_request' in legacy_report
                and '"return kIOReturnSuccess;" in gas_abort' not in legacy_report
            ),
            "historical_claims_corrected": (
                "Q13 correction: `setGAS_ABORT` is GASAdapter-backed" in signal_audit
                and "GAS abort is not a successful no-op" in inventory
                and "GASAdapter" in hpp
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
        raise ValueError("GAS abort quarantine checks failed: " + ", ".join(failed))
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
        print(f"GAS abort quarantine validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
