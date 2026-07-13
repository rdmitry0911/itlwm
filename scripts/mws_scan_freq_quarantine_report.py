#!/usr/bin/env python3
"""Generate and verify MWS scan-frequency quarantine evidence."""

import argparse
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/mws_scan_freq_quarantine_report.json"
NOTE = ROOT / "docs/reference/CR-479-mws-scan-freq-quarantine-20260713.md"
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
    setter = section(cpp, "setMWS_SCAN_FREQ_WIFI_ENH", "setMWS_SCAN_FREQ_MODE_WIFI_ENH")

    return {
        "schema": "itlwm-mws-scan-freq-quarantine-v1",
        "source_base_revision": "2a42fcab60b129cc1c645283c34f07d034bae9d3",
        "reference": {
            "image_sha256": "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
            "infra_slot": 654,
            "infra_wrapper": "0x100019704",
            "core_setter": "0x100141708",
            "core_vtable": "0x1003a10d8",
            "vtable_entry": "0x1003a1710",
            "terminal_owner": "0x100122806",
            "firmware_iovar": "mws",
            "firmware_command": "0x1018000",
            "firmware_subcommand": 7,
            "firmware_payload_bytes": 40,
        },
        "local": {
            "backend_mws_scan_freq_owner": False,
            "false_success": False,
            "valid_input_return_is_apple_parity": False,
        },
        "checks": {
            "reference_note": all(
                token in note
                for token in (
                    "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
                    "slot-[654]",
                    "0x100019704",
                    "0x100141708",
                    "+0x24",
                    "+0x299c",
                    "+0x29c0",
                    "+0x628",
                    "0x1003a10d8",
                    "0x1003a1710",
                    "0x100122806",
                    "40-byte (`0x28`) `mws`",
                    "command `0x1018000`",
                    "subcommand `7`",
                    "ten low-16-bit",
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
                and "cachedMwsScanFreq" not in setter
            ),
            "pseudo_state_removed": (
                "cachedMwsScanFreq[" not in cpp
                and "cachedMwsScanFreq[" not in hpp
            ),
            "no_local_mws_scan_freq_backend": all(
                not source_contains(token)
                for token in (
                    '"mws"',
                    "setWiFiType4BlankingBitmapsWiFiEnh",
                    "handleMWSWiFiType4BlankCoexBitmapsWiFiEnhAsyncCallback",
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
        raise ValueError("MWS scan-frequency quarantine checks failed: " + ", ".join(failed))
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
        print(f"MWS scan-frequency quarantine validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
