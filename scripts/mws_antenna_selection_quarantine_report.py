#!/usr/bin/env python3
"""Generate and verify MWS antenna-selection quarantine evidence."""

import argparse
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/mws_antenna_selection_quarantine_report.json"
NOTE = ROOT / "docs/reference/CR-479-mws-antenna-selection-quarantine-20260713.md"
CPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.cpp"
HPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.hpp"
SOURCE_ROOTS = (ROOT / "AirportItlwm", ROOT / "include", ROOT / "itl80211", ROOT / "itlwm")
TICK = chr(96)


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
    setter = section(
        cpp,
        "setMWS_ANTENNA_SELECTION_WIFI_ENH",
        "setNDD_REQ",
    )

    return {
        "schema": "itlwm-mws-antenna-selection-quarantine-v1",
        "source_base_revision": "fa71cc1b4e6a57512b7cf1755d956208977e9eec",
        "reference": {
            "image_sha256": "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
            "infra_slot": 657,
            "infra_wrapper": "0x1000197ac",
            "core_setter": "0x100141cbc",
            "core_vtable": "0x1003a10d8",
            "vtable_entry": "0x1003a1670",
            "terminal_owner": "0x10012351c",
            "selector_count": 8,
            "firmware_iovar": "mws",
            "firmware_command": "0x1018000",
            "firmware_subcommand": 4,
            "command_tx_payload_bytes": 116,
            "embedded_body_bytes": 108,
        },
        "local": {
            "backend_mws_antenna_selection_owner": False,
            "false_success": False,
            "valid_input_return_is_apple_parity": False,
        },
        "checks": {
            "reference_note": all(
                token in note
                for token in (
                    "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
                    "slot-[657]",
                    "0x1000197ac",
                    "0x100141cbc",
                    f"raw {TICK}+0x10{TICK}",
                    f"raw {TICK}+0x00{TICK}",
                    f"raw {TICK}+0x0e{TICK}",
                    "+0x28e4",
                    "+0x28e6",
                    "+0x28f4",
                    "+0x588",
                    "0x1003a10d8",
                    "0x1003a1670",
                    "0x10012351c",
                    f"CommandTxPayload length{chr(10)}{TICK}0x74{TICK}",
                    f"embedded body length {TICK}0x6c{TICK}",
                    f"command {TICK}0x1018000{TICK}",
                    f"subcommand {TICK}4{TICK}",
                    f"wifiBand at payload{chr(10)}{TICK}+0x10{TICK}",
                    "eight u16 selectors",
                    f"payload {TICK}+0x12{TICK}",
                    f"{TICK}+0x32{TICK}",
                    f"{TICK}+0x52{TICK}",
                    "sendIOVarSet",
                    "runIOVarSet",
                    "return status is preserved",
                    "not Apple valid-input return-code parity",
                )
            ),
            "setter_quarantines_nonnull": (
                "if (data == nullptr)" in setter
                and "return kIOReturnBadArgumentTahoe;" in setter
                and "return kIOReturnUnsupported;" in setter
                and "return kIOReturnSuccess;" not in setter
                and "cachedMwsAntennaSelection" not in setter
            ),
            "pseudo_state_removed": (
                "cachedMwsAntennaSelection" not in cpp
                and "cachedMwsAntennaSelection" not in hpp
            ),
            "no_local_mws_antenna_selection_backend": all(
                not source_contains(token)
                for token in (
                    '"mws"',
                    "setANTENNA_SELECTION_V3_WiFiEnh",
                    "handleMWSAntSelCoexBitmapsWiFiEnhAsyncCallback",
                    "sendIOVarSet",
                    "runIOVarSet",
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
        raise ValueError("MWS antenna-selection quarantine checks failed: " + ", ".join(failed))
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
        print(f"MWS antenna-selection quarantine validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
