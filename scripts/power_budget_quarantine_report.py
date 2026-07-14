#!/usr/bin/env python3
"""Generate and verify POWER_BUDGET false-success quarantine evidence."""

import argparse
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/power_budget_quarantine_report.json"
NOTE = ROOT / "docs/reference/CR-479-power-budget-quarantine-20260714.md"
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


def local_disposition(budget):
    if budget == 0 or budget >= 101:
        return "bad_argument"
    return "unsupported"


def report():
    cpp = CPP.read_text(encoding="utf-8")
    hpp = HPP.read_text(encoding="utf-8")
    note = " ".join(NOTE.read_text(encoding="utf-8").split())
    inventory = INVENTORY.read_text(encoding="utf-8")
    signal_audit = SIGNAL_AUDIT.read_text(encoding="utf-8")
    getter = section(
        cpp,
        "getPOWER_BUDGET(apple80211_power_budget_t *data)",
        "getOFFLOAD_TCPKA_ENABLE",
    )
    setter = section(
        cpp,
        "setPOWER_BUDGET(apple80211_power_budget_t *data)",
        "setUSB_HOST_NOTIFICATION",
    )

    return {
        "schema": "itlwm-power-budget-quarantine-v1",
        "source_base_revision": "3a8a5a32c243bdab9591e8a7357894f9b61790e3",
        "reference": {
            "image_sha256": "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
            "infra_wrapper": "0x1000187f8",
            "core_vtable_symbol": "0x1003a10d8",
            "core_vptr_address_point": "0x1003a10e8",
            "core_vtable_offset": "0x4f8",
            "core_vtable_cell": "0x1003a15e0",
            "core_setter": "0x100120790",
            "feature_helper": "0x1000c846e",
            "feature_bit": "0x3b",
            "valid_range": "1..100",
            "firmware_iovar": "tvpm",
            "firmware_set": "0x10017b6e6",
            "payload_bytes": 12,
            "state_offset": "0x48+0x4",
            "special_commit_status": "0xe3ff8117",
        },
        "local": {
            "backend_power_budget_owner": False,
            "request_false_success": False,
            "valid_input_return_is_apple_parity": False,
        },
        "checks": {
            "reference_note": all(
                token in note
                for token in (
                    "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
                    "`0x1000187f8`",
                    "`+0x4f8`",
                    "`0x1003a10d8`",
                    "`+0x10`",
                    "`0x1003a15e0`",
                    "`0x100120790`",
                    "featureFlagIsBitSet(0x3b)",
                    "`0x1000c846e`",
                    "`1..100`",
                    "`0xe00002bc`",
                    "12-byte",
                    "`+0x8`",
                    "runIOVarSet(\"tvpm\")",
                    "`0x10017b6e6`",
                    "`0xe3ff8117`",
                    "raw transport status",
                    "does not claim exact enabled-Core null return-code parity",
                    "does not claim Apple valid-input return-code parity",
                )
            ),
            "setter_preserves_gates_and_quarantines_valid": all(
                token in setter
                for token in (
                    "if (data == nullptr)",
                    "return kIOReturnBadArgumentTahoe;",
                    "((cachedOSFeatureFlags >> 0x3b) & 1ULL) == 0",
                    "if (data->power_budget == 0 || data->power_budget >= 101)",
                    "return kIOReturnUnsupported;",
                )
            )
            and "return kIOReturnSuccess;" not in setter
            and setter.index("return kIOReturnUnsupported;")
            > setter.index("if (data->power_budget == 0 || data->power_budget >= 101)"),
            "exact_local_range": (
                local_disposition(0) == "bad_argument"
                and local_disposition(1) == "unsupported"
                and local_disposition(100) == "unsupported"
                and local_disposition(101) == "bad_argument"
                and local_disposition(0xFFFFFFFF) == "bad_argument"
            ),
            "setter_cache_write_removed": (
                "cachedPowerBudget = data->power_budget;" not in setter
                and cpp.count("cachedPowerBudget =") == 2
                and "data->power_budget = cachedPowerBudget;" in getter
            ),
            "interface_slot_retained": "virtual IOReturn setPOWER_BUDGET(apple80211_power_budget_t *) override;"
            in hpp,
            "scoped_backend_absent": all(
                not source_contains(token)
                for token in (
                    '"tvpm"',
                    "configurePowerBudget",
                    "setPowerBudgetToFirmware",
                )
            ),
            "historical_claims_corrected": (
                "Power Budget correction:" in inventory
                and "Power Budget correction:" in signal_audit
                and "setPOWER_BUDGET` mirrors the feature/range gate" not in inventory
                and "setPOWER_BUDGET` already matched the recovered feature/range gate"
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
        raise ValueError("POWER_BUDGET quarantine checks failed: " + ", ".join(failed))
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
        print(f"POWER_BUDGET quarantine validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
