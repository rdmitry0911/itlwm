#!/usr/bin/env python3
"""Generate and verify WCL ULOFDMA false-success quarantine evidence."""

import argparse
import json
import sys
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUTPUT = PROJECT_ROOT / "evidence/state/wcl_ulofdma_quarantine_report.json"
REFERENCE_NOTE = PROJECT_ROOT / "docs/reference/CR-479-wcl-ulofdma-quarantine-20260712.md"
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
        "setWCL_ULOFDMA_STATE(apple80211_wcl_ulofdma_state *data)",
        "setMIMO_CONFIG",
    )

    return {
        "schema": "itlwm-wcl-ulofdma-quarantine-v1",
        "source_base_revision": "f772fda",
        "reference": {
            "core_fileset": "AppleBCMWLANCoreMac",
            "core_setter": "0xffffff8001616876",
            "null_return": "0xe00002bc",
            "adapter_offset": "0x15c8",
            "adapter_setter": "0xffffff80016247b2",
        },
        "local": {
            "backend_ulofdma_adapter": False,
            "request_false_success": False,
            "selector_slot": 608,
        },
        "checks": {
            "reference_note": all(
                token in note
                for token in (
                    "AppleBCMWLANCoreMac",
                    "0xffffff8001616876",
                    "0xe00002bc",
                    "+0x15c8",
                    "0xffffff80016247b2",
                    "firmware-generation-specific",
                    "workqueue",
                )
            ),
            "setter_preserves_null_and_quarantines_nonnull": (
                "if (data == nullptr)" in setter
                and "return kIOReturnBadArgumentTahoe;" in setter
                and "return kIOReturnUnsupported;" in setter
                and "cachedUlofdmaState" not in setter
                and "return kIOReturnSuccess;" not in setter
            ),
            "pseudo_state_removed": "cachedUlofdmaState" not in skywalk
            and "cachedUlofdmaState" not in skywalk_hpp,
            "intel_source_has_no_ulofdma_backend": not any(
                source_contains(token)
                for token in (
                    "setUlofdma",
                    "ULOFDMACommand",
                    "11axAdapter",
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
        raise ValueError("WCL ULOFDMA quarantine checks failed: " + ", ".join(failed))
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
        print(f"WCL ULOFDMA quarantine validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
