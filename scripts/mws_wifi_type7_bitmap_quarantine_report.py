#!/usr/bin/env python3
"""Generate and verify MWS WiFi Type-7 bitmap quarantine evidence."""

import argparse
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/mws_wifi_type7_bitmap_quarantine_report.json"
NOTE = ROOT / "docs/reference/CR-479-mws-wifi-type7-bitmap-quarantine-20260713.md"
CPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.cpp"
HPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.hpp"
SOURCE_ROOTS = (ROOT / "AirportItlwm", ROOT / "include", ROOT / "itl80211", ROOT / "itlwm")


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
    setter = section(cpp, "setMWS_WIFI_TYPE_7_BITMAP_WIFI_ENH", "setMWS_COEX_BITMAP_WIFI_ENH")

    return {
        "schema": "itlwm-mws-wifi-type7-bitmap-quarantine-v1",
        "source_base_revision": "5ce9d4231f928a22399f48727f8174de1ea3e434",
        "reference": {
            "image_sha256": "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
            "infra_slot": 649,
            "infra_wrapper": "0x1000195ec",
            "core_setter": "0x100140e68",
            "core_vtable": "0x1003a10d8",
            "terminal_owner": "0x100122580",
            "firmware_iovar": "mws",
            "firmware_payload_bytes": 36,
        },
        "local": {
            "backend_mws_wifi_type7_owner": False,
            "false_success": False,
            "valid_input_return_is_apple_parity": False,
        },
        "checks": {
            "reference_note": all(
                token in note
                for token in (
                    "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
                    "slot-[649]",
                    "0x1000195ec",
                    "0x100140e68",
                    "+0x2978",
                    "+0x2998",
                    "+0x620",
                    "0x1003a10d8",
                    "0x1003a1708",
                    "0x100122580",
                    "0x24`-byte `mws`",
                    "opcode `6`",
                    "nine low-16-bit",
                    "sendIOVarSet",
                    "runIOVarSet",
                    "not Apple valid-input return-code parity",
                )
            ),
            "setter_quarantines_nonnull": (
                "if (data == nullptr)" in setter
                and "return kIOReturnBadArgumentTahoe;" in setter
                and "return kIOReturnUnsupported;" in setter
                and "return kIOReturnSuccess;" not in setter
                and "cachedMwsWifiType7Bitmap" not in setter
            ),
            "pseudo_state_removed": (
                "cachedMwsWifiType7Bitmap" not in cpp
                and "cachedMwsWifiType7Bitmap" not in hpp
            ),
            "no_local_mws_wifi_type7_backend": all(
                not source_contains(token)
                for token in (
                    '"mws"',
                    "WiFiType7",
                    "handleMWSWiFiType7CoexBitmapsWiFiEnhAsyncCallback",
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
        raise ValueError("MWS WiFi Type-7 quarantine checks failed: " + ", ".join(failed))
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
        print(f"MWS WiFi Type-7 quarantine validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
