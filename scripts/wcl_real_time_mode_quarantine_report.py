#!/usr/bin/env python3
"""Generate and verify WCL real-time mode false-success quarantine evidence."""

import argparse
import json
import sys
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUTPUT = PROJECT_ROOT / "evidence/state/wcl_real_time_mode_quarantine_report.json"
REFERENCE_NOTE = PROJECT_ROOT / "docs/reference/CR-479-wcl-real-time-mode-quarantine-20260712.md"
SKYWALK = PROJECT_ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.cpp"
SKYWALK_HPP = PROJECT_ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.hpp"
SOURCE_ROOTS = (PROJECT_ROOT / "AirportItlwm", PROJECT_ROOT / "itlwm")


def section(source, begin, end):
    start = source.index(begin)
    finish = source.index(end, start)
    return source[start:finish]


def source_contains(token):
    return any(
        token in path.read_text(encoding="utf-8", errors="ignore")
        for root in SOURCE_ROOTS
        for path in root.rglob("*")
        if path.is_file()
    )


def build_report():
    skywalk = SKYWALK.read_text(encoding="utf-8")
    skywalk_hpp = SKYWALK_HPP.read_text(encoding="utf-8")
    note = REFERENCE_NOTE.read_text(encoding="utf-8")
    setter = section(
        skywalk,
        "setWCL_REAL_TIME_MODE(apple80211_wcl_real_time_mode *data)",
        "setWCL_ACTION_FRAME",
    )

    return {
        "schema": "itlwm-wcl-real-time-mode-quarantine-v1",
        "source_base_revision": "c7427f1",
        "reference": {
            "core_fileset": "AppleBCMWLANCoreMac",
            "core_setter": "0xffffff800161508c",
            "null_return": "0xe00002bc",
            "adapter_offset": "0x15e0",
            "set_real_time_mode": "0xffffff8001524c5a",
            "set_default_mode": "0xffffff800152499c",
        },
        "local": {
            "backend_real_time_mode_adapter": False,
            "request_false_success": False,
            "selector_slot": 596,
        },
        "checks": {
            "reference_note": all(
                token in note
                for token in (
                    "AppleBCMWLANCoreMac",
                    "0xffffff800161508c",
                    "0xe00002bc",
                    "+0x15e0",
                    "0xffffff8001524c5a",
                    "0xffffff800152499c",
                    "setRealTimeMode",
                    "setDefaultMode",
                )
            ),
            "setter_preserves_null_and_quarantines_nonnull": (
                "if (data == nullptr)" in setter
                and "return kIOReturnBadArgumentTahoe;" in setter
                and "return kIOReturnUnsupported;" in setter
                and "cachedRealTimeMode" not in setter
                and "return kIOReturnSuccess;" not in setter
            ),
            "pseudo_state_removed": "cachedRealTimeMode" not in skywalk
            and "cachedRealTimeMode" not in skywalk_hpp,
            "intel_source_has_no_mode_backend": not any(
                source_contains(token)
                for token in (
                    "setRealTimeMode",
                    "setDefaultMode",
                    "realTimeModeDidStart",
                    "realTimeModeDidEnd",
                )
            ),
        },
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--write", action="store_true", help="write the generated report")
    parser.add_argument("--check", action="store_true", help="compare against the checked-in report")
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    args = parser.parse_args()
    if args.write == args.check:
        parser.error("select exactly one of --write or --check")

    report = build_report()
    failed = [name for name, passed in report["checks"].items() if not passed]
    if failed:
        raise ValueError("WCL real-time mode quarantine checks failed: " + ", ".join(failed))
    rendered = json.dumps(report, indent=2, sort_keys=True) + "\n"

    if args.write:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(rendered, encoding="utf-8")
    else:
        if not args.output.exists():
            raise ValueError(f"missing checked-in report: {args.output}")
        if args.output.read_text(encoding="utf-8") != rendered:
            raise ValueError("checked-in report differs; rerun with --write")

    print(rendered, end="")


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print(f"WCL real-time mode quarantine validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
