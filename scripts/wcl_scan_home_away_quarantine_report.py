#!/usr/bin/env python3
"""Generate and verify WCL scan-home-away false-success quarantine evidence."""

import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/wcl_scan_home_away_quarantine_report.json"
NOTE = ROOT / "docs/reference/CR-479-wcl-scan-home-away-quarantine-20260712.md"
CPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.cpp"
HPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.hpp"
SOURCE_ROOTS = (ROOT / "AirportItlwm", ROOT / "itlwm")


def section(source, begin, end):
    start = source.index(begin)
    return source[start:source.index(end, start)]


def source_contains(token):
    return any(
        token in path.read_text(encoding="utf-8", errors="ignore")
        for root in SOURCE_ROOTS
        for path in root.rglob("*")
        if path.is_file()
    )


def report():
    cpp = CPP.read_text(encoding="utf-8")
    hpp = HPP.read_text(encoding="utf-8")
    note = NOTE.read_text(encoding="utf-8")
    setter = section(
        cpp,
        "setWCL_SET_SCAN_HOME_AWAY_TIME(scanHomeAndAwayTime *data)",
        "setWCL_ULOFDMA_STATE",
    )
    return {
        "schema": "itlwm-wcl-scan-home-away-quarantine-v1",
        "source_base_revision": "688b0a4",
        "reference": {
            "public_bridge": "0xffffff8001522d28",
            "adapter_offset": "0x1530",
            "adapter_setter": "0xffffff80016ac8a6",
            "firmware_iovar": "scan_home_away_time",
        },
        "local": {
            "backend_scan_home_away_adapter": False,
            "request_false_success": False,
            "selector_slot": 604,
        },
        "checks": {
            "reference_note": all(
                token in note
                for token in (
                    "FUN_ffffff8001522d28",
                    "Core +0x1530",
                    "FUN_ffffff80016ac8a6",
                    "scan_home_away_time",
                    "workqueue",
                )
            ),
            "setter_quarantines_nonnull": (
                "if (data == nullptr)" in setter
                and "return kIOReturnBadArgumentTahoe;" in setter
                and "return kIOReturnUnsupported;" in setter
                and "cachedScanHomeAwayTime" not in setter
                and "return kIOReturnSuccess;" not in setter
            ),
            "pseudo_state_removed": "cachedScanHomeAwayTime" not in cpp
            and "cachedScanHomeAwayTime" not in hpp,
            "intel_source_has_no_backend": not any(
                source_contains(token)
                for token in (
                    "scan_home_away_time",
                    "setScanHomeAway",
                    "ScanHomeAwayIovar",
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
        raise ValueError("scan-home-away quarantine checks failed: " + ", ".join(failed))
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
        print(f"WCL scan-home-away quarantine validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
