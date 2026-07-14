#!/usr/bin/env python3
"""Generate and verify TXPOWER/RATE false-success quarantine evidence."""

import argparse
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/txpower_rate_quarantine_report.json"
NOTE = ROOT / "docs/reference/CR-479-txpower-rate-quarantine-20260714.md"
INVENTORY = ROOT / "docs/tahoe_discrepancy_inventory.md"
SIGNAL_AUDIT = ROOT / "docs/tahoe_signal_chain_audit.md"
CPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.cpp"
HPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.hpp"
IWM_BA = ROOT / "itlwm/hal_iwm/mac80211.cpp"
IWX_BA = ROOT / "itlwm/hal_iwx/ItlIwx.cpp"
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
    txpower = section(
        cpp,
        "setTXPOWER(apple80211_txpower_data *data)",
        "setRATE(apple80211_rate_data *data)",
    )
    rate = section(
        cpp,
        "setRATE(apple80211_rate_data *data)",
        "setIBSS_MODE(apple80211_network_data *data)",
    )
    iwm_ba = IWM_BA.read_text(encoding="utf-8")
    iwx_ba = IWX_BA.read_text(encoding="utf-8")

    return {
        "schema": "itlwm-txpower-rate-quarantine-v1",
        "source_base_revision": "89e97549082163207cf32f3148e85784bda9fe7f",
        "reference": {
            "image_sha256": "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
            "core_vptr_address_point": "0x1003a10e8",
            "core_vtable_symbol": "0x1003a10d8",
            "rate": {
                "core_setter": "0x100120cba",
                "core_vtable_cell": "0x1003a15e8",
                "core_vtable_offset": "0x500",
                "final_status": "raw final bg_rate GET status",
                "firmware_iovar": "bg_rate",
                "firmware_get": "0x10017b780",
                "firmware_set": "0x10017b6e6",
                "infra_wrapper": "0x100018100",
                "request_rate_offset": "+0x8",
                "sequence": "GET/SET/GET",
            },
            "txpower": {
                "core_setter": "0x10012099c",
                "core_vtable_cell": "0x1003a15d0",
                "core_vtable_offset": "0x4e8",
                "firmware_iovar": "qtxpower",
                "firmware_set": "0x10017b6e6",
                "infra_wrapper": "0x1000180b4",
                "payload_bytes": 4,
                "txpower_offset": "+0x8",
                "unit_offset": "+0x4",
            },
        },
        "local": {
            "backend_bg_rate_owner": False,
            "backend_qtxpower_owner": False,
            "request_false_success": False,
            "valid_input_return_is_apple_parity": False,
        },
        "checks": {
            "reference_note": all(
                token in note
                for token in (
                    "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
                    "`0x1000180b4`",
                    "`+0x4e8`",
                    "`0x1003a10d8`",
                    "`+0x10`",
                    "`0x1003a15d0`",
                    "`0x10012099c`",
                    "`qtxpower`",
                    "four-byte",
                    "runIOVarSet(\"qtxpower\")",
                    "`0x10017b6e6`",
                    "`0x100018100`",
                    "`+0x500`",
                    "`0x1003a15e8`",
                    "`0x100120cba`",
                    "`bg_rate` GET",
                    "final GET's raw status",
                    "does not claim Apple null return-code parity",
                    "does not claim Apple valid-input return-code parity",
                )
            ),
            "txpower_null_safety_and_quarantine": all(
                token in txpower
                for token in (
                    "if (data == nullptr)",
                    "return kIOReturnBadArgumentTahoe;",
                    "return kIOReturnUnsupported;",
                )
            )
            and "data->" not in txpower
            and "kIOReturnSuccess" not in txpower,
            "rate_null_safety_and_quarantine": all(
                token in rate
                for token in (
                    "if (data == nullptr)",
                    "return kIOReturnBadArgumentTahoe;",
                    "return kIOReturnUnsupported;",
                )
            )
            and "data->" not in rate
            and "kIOReturnSuccess" not in rate,
            "synthetic_mutations_removed": all(
                token not in cpp + hpp
                for token in (
                    "encodeAppleTahoeQTxpowerFromMw",
                    "setTahoeCachedQTxpowerRaw",
                    "cachedBgRate",
                    "hasCachedBgRate",
                )
            ),
            "ba_qtxpower_producers_retained": all(
                token in iwm_ba
                for token in (
                    "sc->sc_last_qtxpower_raw = ba_notif->reduced_txp;",
                    "sc->sc_has_last_qtxpower_raw = true;",
                )
            )
            and all(
                token in iwx_ba
                for token in (
                    "sc->sc_last_qtxpower_raw = ba_res->reduced_txp;",
                    "sc->sc_has_last_qtxpower_raw = true;",
                )
            ),
            "interface_slots_retained": (
                "virtual IOReturn setTXPOWER(apple80211_txpower_data *) override;" in hpp
                and "virtual IOReturn setRATE(apple80211_rate_data *) override;" in hpp
            ),
            "scoped_backend_absent": all(
                not source_contains(token)
                for token in (
                    'runIOVarSet("qtxpower")',
                    'runIOVarGet("bg_rate")',
                    'runIOVarSet("bg_rate")',
                )
            ),
            "historical_claims_corrected": (
                "TXPOWER/RATE correction:" in inventory
                and "TXPOWER/RATE correction:" in signal_audit
                and "must not acknowledge the operation by changing a getter cache"
                in signal_audit
                and "both non-null setters therefore return" in inventory.lower()
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
        raise ValueError("TXPOWER/RATE quarantine checks failed: " + ", ".join(failed))
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
        print(f"TXPOWER/RATE quarantine validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
