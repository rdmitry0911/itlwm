#!/usr/bin/env python3
"""Generate and verify DBG guard-time false-success quarantine evidence."""

import argparse
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/dbg_guard_time_quarantine_report.json"
NOTE = ROOT / "docs/reference/CR-479-dbg-guard-time-quarantine-20260714.md"
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
    getter = section(
        cpp,
        "getDBG_GUARD_TIME_PARAMS(apple80211_dbg_guard_time_params *data)",
        "getAWDL_RSDB_CAPS",
    )
    setter = section(
        cpp,
        "setDBG_GUARD_TIME_PARAMS(apple80211_dbg_guard_time_params *data)",
        "setPRIVATE_MAC",
    )

    return {
        "schema": "itlwm-dbg-guard-time-quarantine-v1",
        "source_base_revision": "f3bcc3a0a8c5ef32c0e808e01de608b9600dea59",
        "reference": {
            "image_sha256": "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
            "getter_infra_wrapper": "0x100017210",
            "getter_core_virtual_offset": "0x2d0",
            "getter_vtable_entry": "0x1003a13b8",
            "getter_core_handler": "0x100106ce8",
            "getter_transport": "0x10017b780",
            "getter_copy_status": "0xe00002e3",
            "setter_infra_wrapper": "0x100018490",
            "core_vtable": "0x1003a10d8",
            "setter_core_virtual_offset": "0x4d8",
            "setter_vtable_entry": "0x1003a15c0",
            "setter_core_setter": "0x1001203d4",
            "setter_transport": "0x10017b6e6",
            "iovar": "forced_pm",
        },
        "local": {
            "backend_forced_pm_owner": False,
            "getter_false_success": False,
            "setter_false_success": False,
            "valid_input_return_is_apple_parity": False,
        },
        "checks": {
            "reference_note": all(
                token in note
                for token in (
                    "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
                    "0x100017210",
                    "`+0x2d0`",
                    "0x1003a13b8",
                    "0x100106ce8",
                    "0x10017b780",
                    "`0xe00002e3`",
                    "0x100018490",
                    "0x1003a10d8",
                    "0x1003a15c0",
                    "0x1001203d4",
                    "0x10017b6e6",
                    "forced_pm",
                    "0xaa",
                    "null-input contract",
                )
            ),
            "getter_preserves_local_null_guard_and_quarantines_nonnull": (
                "if (data == nullptr)" in getter
                and "return static_cast<IOReturn>(0xe00002c2);" in getter
                and "return kIOReturnUnsupported;" in getter
                and "return kIOReturnSuccess;" not in getter
                and "cachedDbgGuardTimeParams" not in getter
                and "memcpy(raw" not in getter
            ),
            "setter_preserves_local_null_guard_and_quarantines_nonnull": (
                "if (data == nullptr)" in setter
                and "return kIOReturnBadArgumentTahoe;" in setter
                and "return kIOReturnUnsupported;" in setter
                and "return kIOReturnSuccess;" not in setter
                and "cachedDbgGuardTimeParams" not in setter
            ),
            "pseudo_cache_removed": all(
                token not in cpp and token not in hpp
                for token in (
                    "cachedDbgGuardTimeParams",
                    "hasCachedDbgGuardTimeParams",
                )
            ),
            "local_transport_absent": all(
                not source_contains(token)
                for token in (
                    "forced_pm",
                    "setGuardTime(",
                    "getGuardTime(",
                )
            ),
            "historical_claims_corrected": (
                "Q13 correction: DBG guard time is commander-backed" in signal_audit
                and "DBG guard time is not cache-backed" in inventory
                and "fifteen remaining setters" not in inventory
                and "private command owner" in hpp
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
        raise ValueError("DBG guard-time quarantine checks failed: " + ", ".join(failed))
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
        print(f"DBG guard-time quarantine validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
