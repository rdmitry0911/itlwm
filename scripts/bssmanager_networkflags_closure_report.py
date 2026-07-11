#!/usr/bin/env python3
"""Generate and verify the BssManager network-flags no-producer closure."""

import argparse
import json
import re
import sys
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUTPUT = PROJECT_ROOT / "evidence/state/bssmanager_networkflags_closure_report.json"
REFERENCE_NOTE = (
    PROJECT_ROOT / "docs/reference/CR-479-bssmanager-network-flags-closure-20260711.md"
)
DECLARATION_HEADER = PROJECT_ROOT / "include/Airport/IO80211BssManager.h"
SOURCE_ROOTS = [PROJECT_ROOT / "AirportItlwm", PROJECT_ROOT / "itl80211", PROJECT_ROOT / "include"]
SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hpp"}
CALL_PATTERN = re.compile(r"\bsetNetworkFlags\s*\(")
REFERENCE_TOKENS = [
    "0xffffff8002242562",
    "count=0",
    "0x34c2148",
    "0xffffff80035ef038",
    "does not add a local setNetworkFlags call",
]
DECLARATION_TOKEN = "void setNetworkFlags(bool, unsigned int);"


def source_call_sites():
    sites = []
    for root in SOURCE_ROOTS:
        for path in sorted(root.rglob("*")):
            if not path.is_file() or path.suffix not in SOURCE_SUFFIXES:
                continue
            if path == DECLARATION_HEADER:
                continue
            for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
                if CALL_PATTERN.search(line):
                    sites.append(f"{path.relative_to(PROJECT_ROOT)}:{line_number}")
    return sites


def build_report():
    reference_text = REFERENCE_NOTE.read_text(encoding="utf-8")
    missing_reference_tokens = [token for token in REFERENCE_TOKENS if token not in reference_text]
    declaration_text = DECLARATION_HEADER.read_text(encoding="utf-8")
    return {
        "schema": "itlwm-bssmanager-networkflags-closure-v1",
        "source_revision": "bb75d00446cb7c7916ec296e9bca5a5e0cbb5607",
        "reference": {
            "build": "macOS 26.2 (25C56)",
            "writer": "IO80211BssManager::setNetworkFlags(bool, unsigned int)",
            "writer_address": "0xffffff8002242562",
            "direct_xref_count": 0,
            "only_packed_address_occurrence": {
                "file_offset": "0x34c2148",
                "virtual_address": "0xffffff80035ef038",
                "section": "__LINKEDIT",
            },
        },
        "local": {
            "declaration_present": DECLARATION_TOKEN in declaration_text,
            "runtime_call_sites": source_call_sites(),
        },
        "checks": {
            "missing_reference_tokens": missing_reference_tokens,
            "no_local_runtime_calls": not source_call_sites(),
        },
    }


def validate(report):
    if report["checks"]["missing_reference_tokens"]:
        raise ValueError(
            "reference note lacks required evidence: "
            + ", ".join(report["checks"]["missing_reference_tokens"])
        )
    if not report["local"]["declaration_present"]:
        raise ValueError("IO80211BssManager network-flags declaration is missing")
    if not report["checks"]["no_local_runtime_calls"]:
        raise ValueError(
            "local source fabricates BssManager network-flags producer(s): "
            + ", ".join(report["local"]["runtime_call_sites"])
        )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--write", action="store_true", help="write the generated report")
    parser.add_argument("--check", action="store_true", help="compare against the checked-in report")
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    args = parser.parse_args()
    if args.write == args.check:
        parser.error("select exactly one of --write or --check")

    report = build_report()
    validate(report)
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
        print(f"BssManager network-flags closure validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
