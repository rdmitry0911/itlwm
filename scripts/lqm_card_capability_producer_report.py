#!/usr/bin/env python3
"""Generate and verify the LQM CARD_CAPABILITIES[10] closure evidence."""

import argparse
import json
import sys
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUTPUT = PROJECT_ROOT / "evidence/state/lqm_card_capability_producer_report.json"
REFERENCE_NOTE = (
    PROJECT_ROOT
    / "docs/reference/CR-479-lqm-card-capability-producer-closure-20260711.md"
)
CONTRACTS = PROJECT_ROOT / "AirportItlwm/TahoeCapabilityContracts.hpp"
CARD_PRODUCER = PROJECT_ROOT / "AirportItlwm/AirportItlwmV2.cpp"
LEGACY_PRODUCER = PROJECT_ROOT / "AirportItlwm/AirportSTAIOCTL.cpp"

REFERENCE_TOKENS = [
    "0xffffff800157617e",
    "wlc_ver",
    "0xffffff80015763d0",
    "0xe3ff8117",
    "0xffffff80015c4a9e",
    "0xffffff80015c4f01",
    "0xffffff80015c4f26",
    "0x110c",
    "BCM4364",
    "8086:4060",
    "IO80211PeerMonitor::createLinkQualityMonitor(3511)",
]
SOURCE_TOKENS = {
    "contracts": [
        "kRequiredCardCapabilityBytes = 10",
        "applyAppleConsistentCardCapabilityCluster",
    ],
    "card_producer": [
        "memset(cd, 0, sizeof(struct apple80211_capability_data))",
        "TahoeCapabilityContracts::applyAppleConsistentCardCapabilityCluster",
    ],
    "legacy_producer": [
        "TahoeCapabilityContracts::applyAppleConsistentCardCapabilityCluster",
    ],
}
FORBIDDEN_LOCAL_TOKENS = [
    "kCardCapabilityLqm",
    "kCardCapabilityByte10",
    "kCardCapabilityLqmMask",
    "capabilities[10] |= 0x08",
    "capabilities[10] = 0x08",
]


def missing_tokens(path, tokens):
    text = path.read_text(encoding="utf-8")
    return [token for token in tokens if token not in text]


def present_tokens(paths, tokens):
    matches = []
    for path in paths:
        text = path.read_text(encoding="utf-8")
        matches.extend(token for token in tokens if token in text)
    return sorted(set(matches))


def build_report():
    missing = {
        "reference": missing_tokens(REFERENCE_NOTE, REFERENCE_TOKENS),
        "contracts": missing_tokens(CONTRACTS, SOURCE_TOKENS["contracts"]),
        "card_producer": missing_tokens(CARD_PRODUCER, SOURCE_TOKENS["card_producer"]),
        "legacy_producer": missing_tokens(
            LEGACY_PRODUCER, SOURCE_TOKENS["legacy_producer"]
        ),
    }
    forbidden = present_tokens(
        [CONTRACTS, CARD_PRODUCER, LEGACY_PRODUCER], FORBIDDEN_LOCAL_TOKENS
    )
    return {
        "schema": "itlwm-lqm-card-capability-producer-closure-v1",
        "source_base_revision": "cfc5575e822772b933b90b7a71353d143071210c",
        "reference": {
            "build": "macOS 26.2 (25C56)",
            "setup_firmware": {
                "entry": "0xffffff800157617e",
                "iovar": "wlc_ver",
                "generation_store": "0xffffff80015763d0",
                "unavailable_fallback_error": "0xe3ff8117",
                "unavailable_fallback_generation": 3,
            },
            "card_capability": {
                "entry": "0xffffff80015c4a9e",
                "public_index": 10,
                "mask": "0x08",
                "generation_gt": 5,
                "generation_eq_chip_id": 5,
                "chip_id": "0x110c",
                "chip_name": "BCM4364",
            },
        },
        "local": {
            "active_adapter": "8086:4060 / iwn-6030",
            "apple_wlc_ver_owner_present": False,
            "apple_chip_id_virtual_present": False,
            "lqm_capability_advertised": False,
            "substitute_inputs_forbidden": [
                "pci_id",
                "firmware_name",
                "intel_ucode_api",
                "hardware_revision",
                "nss",
                "chain_masks",
            ],
        },
        "checks": {
            **{key: not value for key, value in missing.items()},
            "no_unsafe_local_lqm_capability": not forbidden,
        },
        "missing_tokens": missing,
        "forbidden_local_tokens": forbidden,
    }


def validate(report):
    missing = [
        f"{scope}: {token}"
        for scope, tokens in report["missing_tokens"].items()
        for token in tokens
    ]
    if missing:
        raise ValueError("missing LQM capability-closure tokens: " + "; ".join(missing))
    if report["forbidden_local_tokens"]:
        raise ValueError(
            "unsafe local LQM capability advertisement present: "
            + ", ".join(report["forbidden_local_tokens"])
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
        print(f"LQM capability-producer closure validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
