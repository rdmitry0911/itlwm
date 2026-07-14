#!/usr/bin/env python3
"""Generate and verify PM_MODE false-success quarantine evidence."""

import argparse
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/pm_mode_quarantine_report.json"
NOTE = ROOT / "docs/reference/CR-479-pm-mode-quarantine-20260714.md"
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


def report():
    cpp = CPP.read_text(encoding="utf-8")
    hpp = HPP.read_text(encoding="utf-8")
    note = " ".join(NOTE.read_text(encoding="utf-8").split())
    inventory = INVENTORY.read_text(encoding="utf-8")
    signal_audit = SIGNAL_AUDIT.read_text(encoding="utf-8")
    setter = section(
        cpp,
        "setPM_MODE(apple80211_pm_mode *data)",
        "setLQM_CONFIG(apple80211_lqm_config_t *data)",
    )

    return {
        "schema": "itlwm-pm-mode-quarantine-v1",
        "source_base_revision": "d2438159215a61989cffcef2e4f16d4d7a08b921",
        "reference": {
            "image_sha256": "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
            "infra_wrapper": "0x100018960",
            "core_vtable_symbol": "0x1003a10d8",
            "core_vptr_address_point": "0x1003a10e8",
            "core_vtable_offset": "0x700",
            "core_vtable_cell": "0x1003a17e8",
            "core_setter": "0x100119c4a",
            "carrier_mode_offset": "+0x4",
            "net_adapter_configure": "0x100015e02",
            "mapped_nonzero_request": 2,
            "payload_bytes": 4,
            "ioc": "0x56",
            "commander_send": "0x10017b80c",
            "async_callback": "0x100015fd0",
        },
        "local": {
            "backend_pm_owner": False,
            "request_false_success": False,
            "valid_input_return_is_apple_parity": False,
        },
        "checks": {
            "reference_note": all(
                token in note
                for token in (
                    "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
                    "`0x100018960`",
                    "`+0x700`",
                    "`0x1003a10d8`",
                    "`+0x10`",
                    "`0x1003a17e8`",
                    "`0x100119c4a`",
                    "`+0x4`",
                    "`0x100015e02`",
                    "every nonzero mode to request `2`",
                    "four bytes",
                    "`0x56`",
                    "`0x10017b80c`",
                    "`0x100015fd0`",
                    "raw enqueue/transport status",
                    "does not claim Apple null return-code parity",
                    "does not claim Apple valid-input return-code parity",
                )
            ),
            "setter_preserves_null_and_quarantines_nonnull": all(
                token in setter
                for token in (
                    "if (data == nullptr)",
                    "return kIOReturnBadArgumentTahoe;",
                    "return kIOReturnUnsupported;",
                )
            )
            and "data->" not in setter
            and "setPOWERSAVE" not in setter
            and "kIOReturnSuccess" not in setter,
            "synthetic_cache_removed": "cachedPmMode" not in cpp + hpp,
            "interface_slot_retained": "virtual IOReturn setPM_MODE(apple80211_pm_mode *) override;"
            in hpp,
            "scoped_backend_absent": all(
                not source_contains(token)
                for token in (
                    "configurePM(",
                    "setPowerManagementAsyncCallback",
                    "sendIOCtlSet(",
                )
            ),
            "historical_claims_corrected": (
                "PM_MODE correction:" in inventory
                and "PM_MODE correction:" in signal_audit
                and "cache-only `setPOWERSAVE(...)`" in inventory
                and "reuse already-lifted local owners where available" not in signal_audit
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
        raise ValueError("PM_MODE quarantine checks failed: " + ", ".join(failed))
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
        print(f"PM_MODE quarantine validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
