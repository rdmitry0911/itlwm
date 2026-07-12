#!/usr/bin/env python3
"""Generate and verify MIMO configuration false-success quarantine evidence."""

import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/mimo_config_quarantine_report.json"
NOTE = ROOT / "docs/reference/CR-479-mimo-config-quarantine-20260712.md"
CPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.cpp"
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
    note = NOTE.read_text(encoding="utf-8")
    setter = section(
        cpp,
        "setMIMO_CONFIG(apple80211_mimo_config *data)",
        "setFACETIME_WIFICALLING_PARAMS",
    )
    return {
        "schema": "itlwm-mimo-config-quarantine-v1",
        "source_base_revision": "a41530b",
        "reference": {
            "setter": "0xffffff8001617250",
            "owner_helper": "0xffffff800160d426",
            "feature_gate": "0x2c",
            "mimo_power_owner": "Core +0x128, +0x1590",
            "owner_configure": "0xffffff8001551e88",
        },
        "local": {
            "backend_mimo_power_owner": False,
            "request_false_success": False,
        },
        "checks": {
            "reference_note": all(
                token in note
                for token in (
                    "FUN_ffffff8001617250",
                    "FUN_ffffff800160d426",
                    "feature 0x2c",
                    "Core +0x128",
                    "FUN_ffffff8001551e88",
                )
            ),
            "setter_quarantines_nonnull": (
                "if (data == nullptr)" in setter
                and "return kIOReturnBadArgumentTahoe;" in setter
                and "return kIOReturnUnsupported;" in setter
                and "return kIOReturnSuccess;" not in setter
            ),
            "intel_source_has_no_mimo_power_backend": not any(
                source_contains(token)
                for token in (
                    "setMIMOPowerSaveProperties",
                    "mimo_ps_params",
                    "WLC_E_MIMO_PWR_SAVE",
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
        raise ValueError("MIMO configuration quarantine checks failed: " + ", ".join(failed))
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
        print(f"MIMO configuration quarantine validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
