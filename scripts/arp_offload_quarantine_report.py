#!/usr/bin/env python3
"""Generate and verify ARP offload null-owner quarantine evidence."""

import argparse
import json
import sys
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUTPUT = PROJECT_ROOT / "evidence/state/arp_offload_quarantine_report.json"
REFERENCE_NOTE = PROJECT_ROOT / "docs/reference/CR-479-arp-offload-null-owner-quarantine-20260712.md"
SKYWALK = PROJECT_ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.cpp"
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
    note = REFERENCE_NOTE.read_text(encoding="utf-8")
    setter = section(
        skywalk,
        "setOFFLOAD_ARP(apple80211_offload_arp_data *data)",
        "setGAS_REQ",
    )
    ipv4_params = section(
        skywalk,
        "setIPV4_PARAMS(apple80211_ipv4_params *data)",
        "setIPV6_PARAMS",
    )

    return {
        "schema": "itlwm-arp-offload-quarantine-v1",
        "source_base_revision": "cbc7e25",
        "reference": {
            "core_fileset": "AppleBCMWLANCoreMac",
            "core_setter": "0xffffff80015d97f0",
            "missing_owner_return": "0x16",
            "owner_offset": "0x2c20",
            "optional_owner_offset": "0x2c28",
            "owner_call": "0xffffff80022c0ed2",
            "gated_notification": "0xffffff8001595a70",
        },
        "local": {
            "backend_arp_keepalive_owner": False,
            "direct_offload_false_success": False,
            "selector": 557,
        },
        "checks": {
            "reference_note": all(
                token in note
                for token in (
                    "AppleBCMWLANCoreMac",
                    "0xffffff80015d97f0",
                    "0x16",
                    "+0x2c20",
                    "+0x2c28",
                    "0xffffff80022c0ed2",
                    "handleIPv4AddressNotificationGated",
                    "0xffffff8001595a70",
                    "FUN_ffffff80021e502c",
                )
            ),
            "setter_fails_without_cache_mutation": (
                "data == nullptr || instance == nullptr || instance->fNetIf == nullptr" in setter
                and "return kApple80211ErrInvalidArgumentRaw;" in setter
                and "cachedDhcpRenewalData" not in setter
                and "cachedIPv4Address" not in setter
                and "cachedIPv4Gateway" not in setter
            ),
            "ipv4_params_is_separate_quarantine": all(
                token in ipv4_params
                for token in (
                    "if (data == nullptr)",
                    "return kIOReturnBadArgumentTahoe;",
                    "return kIOReturnUnsupported;",
                )
            ) and all(
                token not in ipv4_params
                for token in (
                    "cachedIPv4Address",
                    "cachedIPv4Netmask",
                    "cachedIPv4Gateway",
                    "return kIOReturnSuccess;",
                )
            ),
            "intel_source_has_no_private_arp_backend": not any(
                source_contains(token)
                for token in (
                    "handleIPv4AddressNotificationGated",
                    "setARPKeepalive",
                    "setGARP",
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
        raise ValueError("ARP offload quarantine checks failed: " + ", ".join(failed))
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
        print(f"ARP offload quarantine validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
