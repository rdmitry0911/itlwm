#!/usr/bin/env python3
"""Generate and verify raw BPF TX false-success quarantine evidence."""

import argparse
import json
import re
import sys
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUTPUT = PROJECT_ROOT / "evidence/state/raw_bpf_tx_quarantine_report.json"
REFERENCE_NOTE = (
    PROJECT_ROOT / "docs/reference/CR-479-raw-bpf-tx-quarantine-20260711.md"
)
CONTROLLER = PROJECT_ROOT / "AirportItlwm/AirportItlwm.cpp"

FUNCTIONS = [
    "outputRaw80211Packet",
    "outputActionFrame",
    "bpfOutput80211Radio",
]
REFERENCE_TOKENS = [
    "AppleBCMWLANNetAdapter::sendActionFrame",
    "AppleBCMWLANNetAdapter::sendActionFrameV2",
    "actframe",
    "no local Intel firmware action-frame injector",
]


def function_body(source, name):
    match = re.search(
        rf"(?:int|UInt32) AirportItlwm::\s*{re.escape(name)}\([^)]*\)\s*"
        r"\{(?P<body>.*?)\n\}",
        source,
        re.DOTALL,
    )
    if match is None:
        raise ValueError(f"cannot find AirportItlwm::{name}")
    return match.group("body")


def build_report():
    source = CONTROLLER.read_text(encoding="utf-8")
    note = REFERENCE_NOTE.read_text(encoding="utf-8")
    bodies = {name: function_body(source, name) for name in FUNCTIONS}
    return {
        "schema": "itlwm-raw-bpf-tx-quarantine-v1",
        "source_base_revision": "ef52aa6c2a62ac58d4452348c3953f6c40ff429d",
        "reference": {
            "apple_actframe_owner": "AppleBCMWLANNetAdapter",
            "v1": "sendActionFrame",
            "v2": "sendActionFrameV2",
            "transport": "actframe",
        },
        "local": {
            "backend_action_frame_injector": False,
            "raw_bpf_success_after_free": False,
            "quarantined_entry_points": FUNCTIONS,
        },
        "checks": {
            "reference_note": all(token in note for token in REFERENCE_TOKENS),
            "raw_packet_drops": "freePacket(m);" in bodies["outputRaw80211Packet"]
            and "return kIOReturnOutputDropped;" in bodies["outputRaw80211Packet"],
            "action_frame_drops": "mbuf_freem(m);" in bodies["outputActionFrame"]
            and "return kIOReturnOutputDropped;" in bodies["outputActionFrame"]
            and "return 0;" not in bodies["outputActionFrame"],
            "radio_frame_drops": "mbuf_freem(m);" in bodies["bpfOutput80211Radio"]
            and "return kIOReturnOutputDropped;" in bodies["bpfOutput80211Radio"]
            and "return 0;" not in bodies["bpfOutput80211Radio"],
            "bpf_routes_are_explicit": "return bpfOutput80211Radio(object, m);" in source
            and "return outputActionFrame(object, m);" in source,
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
        raise ValueError("raw BPF TX quarantine checks failed: " + ", ".join(failed))
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
        print(f"raw BPF TX quarantine validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
