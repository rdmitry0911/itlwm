#!/usr/bin/env python3
"""Generate and verify POWER_PROFILE false-success quarantine evidence."""

import argparse
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/power_profile_quarantine_report.json"
NOTE = ROOT / "docs/reference/CR-479-power-profile-quarantine-20260714.md"
SIGNAL_AUDIT = ROOT / "docs/tahoe_signal_chain_audit.md"
INVENTORY = ROOT / "docs/tahoe_discrepancy_inventory.md"
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


def report():
    cpp = CPP.read_text(encoding="utf-8")
    hpp = HPP.read_text(encoding="utf-8")
    note = NOTE.read_text(encoding="utf-8")
    signal_audit = SIGNAL_AUDIT.read_text(encoding="utf-8")
    inventory = INVENTORY.read_text(encoding="utf-8")
    normalized_inventory = " ".join(inventory.split())
    setter = section(
        cpp,
        "setPOWER_PROFILE(apple80211_power_profile *data)",
        "setIPV4_PARAMS",
    )
    audit_section = section(signal_audit, "### `setPOWER_PROFILE`", "### `setIPV4_PARAMS`")
    return {
        "schema": "itlwm-power-profile-quarantine-v1",
        "source_base_revision": "4f07316dd4d3ff709992dc8ecf4cadf1d0bec66d",
        "reference": {
            "image_sha256": "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
            "infra_wrapper": "0x100018fb4",
            "core_setter": "0x100142686",
            "null_status": "0xe00002bc",
            "core_state_offset": "0x29e8",
            "core_virtual_offset": "0x560",
            "core_vtable": "0x1003a10d8",
            "vtable_entry": "0x1003a1648",
            "core_power_profile": "0x100124398",
            "core_config_manager_offset": "0x1558",
            "config_manager_setter": "0x10008b53e",
        },
        "local": {
            "matching_config_manager_backend_implemented": False,
            "request_false_success": False,
            "complete_public_carrier_layout_proven": False,
            "valid_input_or_error_is_apple_parity": False,
        },
        "checks": {
            "reference_note": all(
                token in note
                for token in (
                    "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
                    "0x100018fb4",
                    "0x100142686",
                    "`0xe00002bc`",
                    "`+0x29e8`",
                    "`+0x560`",
                    "0x1003a10d8",
                    "0x1003a1648",
                    "0x100124398",
                    "`+0x1558`",
                    "0x10008b53e",
                    "complete public carrier",
                )
            ),
            "setter_quarantines_nonnull": all(
                token in setter
                for token in (
                    "if (!data)",
                    "return kIOReturnBadArgumentTahoe;",
                    "return kIOReturnUnsupported;",
                )
            )
            and all(
                token not in setter
                for token in (
                    "cachedPowerProfile",
                    "reinterpret_cast",
                    "return kIOReturnSuccess;",
                )
            ),
            "pseudo_state_removed": "cachedPowerProfile" not in cpp
            and "cachedPowerProfile" not in hpp,
            "scoped_config_manager_backend_absent": all(
                not source_contains(token)
                for token in (
                    "setPowerProfile(",
                    "AppleBCMWLANConfigManager::setPowerProfile",
                    "cachedPowerProfile",
                )
            ),
            "historical_claims_corrected": "ConfigManager-backed quarantine" in signal_audit
            and "`+0x29e8`" in audit_section
            and "`+0x29f0`" not in audit_section
            and "POWER_PROFILE` is intentionally excluded" in normalized_inventory,
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
        raise ValueError("POWER_PROFILE quarantine checks failed: " + ", ".join(failed))
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
        print(f"POWER_PROFILE quarantine validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
