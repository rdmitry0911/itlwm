#!/usr/bin/env python3
"""Generate and verify MWS Condition-ID bitmap quarantine evidence."""

import argparse
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/mws_condition_id_bitmap_quarantine_report.json"
NOTE = ROOT / "docs/reference/CR-479-mws-condition-id-bitmap-quarantine-20260713.md"
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
        "setMWS_CONDITION_ID_BITMAP_WIFI_ENH",
        "setMWS_ANTENNA_SELECTION_WIFI_ENH",
    )

    return {
        "schema": "itlwm-mws-condition-id-bitmap-quarantine-v1",
        "source_base_revision": "2fdfe22c80bf0a3bdf7b439719f72e4cdcac3dd3",
        "reference": {
            "image_sha256": "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
            "infra_slot": 656,
            "infra_wrapper": "0x100019774",
            "core_setter": "0x100141a2e",
            "core_vtable": "0x1003a10d8",
            "vtable_entry": "0x1003a1720",
            "terminal_owner": "0x100123df8",
            "record_stride_bytes": 40,
            "bitmaps_per_record": 9,
            "firmware_iovar": "mws",
            "firmware_command": "0x1018000",
            "firmware_subcommand": 10,
            "command_tx_payload_bytes": 36,
            "embedded_body_bytes": 28,
        },
        "local": {
            "backend_mws_condition_id_owner": False,
            "false_success": False,
            "valid_input_return_is_apple_parity": False,
        },
        "checks": {
            "reference_note": all(
                token in note
                for token in (
                    "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
                    "slot-[656]",
                    "0x100019774",
                    "0x100141a2e",
                    "data[2] == 0",
                    f"raw {TICK}+0x28 + 0x28*i{TICK}",
                    f"raw {TICK}+0x04 + 0x28*i{TICK}",
                    f"raw {TICK}+0x24 + 0x28*i{TICK}",
                    "+0x29c4",
                    "+0x292c",
                    "+0x294c",
                    "+0x638",
                    "0x1003a10d8",
                    "0x1003a1720",
                    "0x100123df8",
                    f"CommandTxPayload length{chr(10)}{TICK}0x24{TICK}",
                    f"embedded body length {TICK}0x1c{TICK}",
                    f"command {TICK}0x1018000{TICK}",
                    f"subcommand {TICK}10{TICK}",
                    f"condition ID is at payload {TICK}+0x11{TICK}",
                    "nine bitmap low-16-bit",
                    "sendIOVarSet",
                    "runIOVarSet",
                    "return status is preserved",
                    "not Apple valid-input return-code parity",
                )
            ),
            "setter_preserves_rejections_and_quarantines_valid": (
                "if (data == nullptr)" in setter
                and "if (raw[2] == 0)" in setter
                and setter.count("return kIOReturnBadArgumentTahoe;") == 2
                and "return kIOReturnUnsupported;" in setter
                and "return kIOReturnSuccess;" not in setter
                and "cachedMwsConditionId" not in setter
            ),
            "pseudo_state_removed": all(
                token not in cpp and token not in hpp
                for token in (
                    "cachedMwsConditionIdConfig",
                    "cachedMwsConditionIdCount",
                    "hasCachedMwsConditionIdConfig",
                )
            ),
            "no_local_mws_condition_id_backend": all(
                not source_contains(token)
                for token in (
                    '"mws"',
                    "setWiFiConditionIdBitmapsWiFiEnh",
                    "handleMWSWiFiConditionIdCoexBitmapsWiFiEnhAsyncCallback",
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
        raise ValueError("MWS Condition-ID bitmap quarantine checks failed: " + ", ".join(failed))
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
        print(f"MWS Condition-ID bitmap quarantine validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
