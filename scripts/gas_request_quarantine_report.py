#!/usr/bin/env python3
"""Generate and verify GAS request false-success quarantine evidence."""

import argparse
import json
import sys
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUTPUT = PROJECT_ROOT / "evidence/state/gas_request_quarantine_report.json"
REFERENCE_NOTE = PROJECT_ROOT / "docs/reference/CR-479-gas-request-quarantine-20260712.md"
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
    gas_request = section(
        skywalk,
        "setGAS_REQ(apple80211_gas_query_t *data)",
        "setBTCOEX_PROFILE",
    )
    return {
        "schema": "itlwm-gas-request-quarantine-v1",
        "source_base_revision": "f11cf7f",
        "reference": {
            "core_fileset": "AppleBCMWLANCoreMac",
            "core_setter": "0xffffff8001608d72",
            "null_return": "0xe00002c2",
            "adapter_offset": "0x1560",
            "set_params": "0xffffff800151dab8",
            "start_query": "0xffffff800151dc98",
            "peer_count_offset": "0x210",
        },
        "local": {
            "backend_gas_anqp_adapter": False,
            "request_false_success": False,
            "abort_in_this_change": False,
            "selector": 197,
        },
        "checks": {
            "reference_note": all(
                token in note
                for token in (
                    "AppleBCMWLANCoreMac",
                    "0xffffff8001608d72",
                    "0xe00002c2",
                    "+0x1560",
                    "0xffffff800151dab8",
                    "0xffffff800151dc98",
                    "+0x210",
                    "fragment/completion",
                    "setGAS_ABORT(...)",
                )
            ),
            "request_preserves_null_and_quarantines_nonnull": (
                "if (data == nullptr)" in gas_request
                and "return static_cast<IOReturn>(0xe00002c2);" in gas_request
                and "return kIOReturnUnsupported;" in gas_request
                and "cachedGasQueryIssued" not in gas_request
                and "return kIOReturnSuccess;" not in gas_request
            ),
            "pseudo_state_removed": "cachedGasQueryIssued" not in skywalk
            and "cachedGasQueryIssued" not in skywalk_hpp,
            "abort_remains_separate": (
                "setGAS_ABORT" not in gas_request
            ),
            "intel_source_has_no_gas_backend": not any(
                source_contains(token)
                for token in (
                    "startGASQuery",
                    "setGASQueryParams",
                    "handleGAS_COMPLETE",
                    "handleGAS_FRAGMENT_RX",
                    "anqpo_stop_query",
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
        raise ValueError("GAS request quarantine checks failed: " + ", ".join(failed))
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
        print(f"GAS request quarantine validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
