#!/usr/bin/env python3
"""Generate and verify NDP offload null-owner quarantine evidence."""

import argparse
import json
import sys
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUTPUT = PROJECT_ROOT / "evidence/state/ndp_offload_quarantine_report.json"
REFERENCE_NOTE = PROJECT_ROOT / "docs/reference/CR-479-ndp-offload-null-owner-quarantine-20260712.md"
OWNERS = PROJECT_ROOT / "AirportItlwm/TahoeOwners.hpp"
COMMANDER = PROJECT_ROOT / "AirportItlwm/TahoeCommander.hpp"
COMMANDER_V2 = PROJECT_ROOT / "AirportItlwm/TahoeCommanderV2.hpp"
SKYWALK = PROJECT_ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.cpp"
SOURCE_ROOTS = (PROJECT_ROOT / "AirportItlwm", PROJECT_ROOT / "itlwm")


def section(source, begin, end):
    start = source.index(begin)
    finish = source.index(end, start)
    return source[start:finish]


def all_source():
    return "\n".join(
        path.read_text(encoding="utf-8", errors="ignore")
        for root in SOURCE_ROOTS
        for path in root.rglob("*")
        if path.is_file()
    )


def build_report():
    owners = OWNERS.read_text(encoding="utf-8")
    commander = COMMANDER.read_text(encoding="utf-8")
    commander_v2 = COMMANDER_V2.read_text(encoding="utf-8")
    skywalk = SKYWALK.read_text(encoding="utf-8")
    note = REFERENCE_NOTE.read_text(encoding="utf-8")

    owner = section(owners, "class TahoeNdpOwner", "class TahoeActionFrameOwner")
    generic = section(commander, "IOReturn runSetOFFLOADNDP", "IOReturn runSetUSBHostNotification")
    v2 = section(commander_v2, "IOReturn runSetOFFLOADNDP", "IOReturn runSetUSBHostNotification")
    skywalk_setter = section(
        skywalk,
        "setOFFLOAD_NDP(apple80211_offload_ndp_data *data)",
        "setOFFLOAD_ARP",
    )
    source = all_source()

    return {
        "schema": "itlwm-ndp-offload-quarantine-v1",
        "source_base_revision": "0fc11de",
        "reference": {
            "core_fileset": "AppleBCMWLANCoreMac",
            "core_setter": "0xffffff80015d9bbe",
            "missing_owner_return": "0x16",
            "owner_offset": "0x2c20",
            "owner_call": "0xffffff80022c0f14",
            "gated_notification": "0xffffff80015960ca",
        },
        "local": {
            "backend_ndp_owner": False,
            "generic_false_success": False,
            "owner_false_success": False,
            "synthetic_v2_transport": False,
            "selector": 554,
        },
        "checks": {
            "reference_note": all(
                token in note
                for token in (
                    "AppleBCMWLANCoreMac",
                    "0xffffff80015d9bbe",
                    "0x16",
                    "+0x2c20",
                    "0xffffff80022c0f14",
                    "handleIPv6AddressNotificationGated",
                    "0xffffff80015960ca",
                )
            ),
            "owner_fails_without_mutation": (
                "data == nullptr || registry == nullptr" in owner
                and "return TahoeErrorMap::kAppleInvalidArgumentRaw;" in owner
                and "registry->ndp." not in owner
                and "completeSync(" not in owner
                and "buildOffloadNdp" not in owner
            ),
            "generic_fails_without_mutation": (
                "data == nullptr || registry == nullptr" in generic
                and "return TahoeErrorMap::kAppleInvalidArgumentRaw;" in generic
                and "registry->ndp." not in generic
                and "asyncContext->completed" not in generic
                and "buildOffloadNdp" not in generic
            ),
            "v2_only_propagates_owner_result": (
                "return ndpOwner.apply(data, asyncContext);" in v2
                and "dispatchIOVarSet(554" not in v2
                and "dispatchVirtualIOCtlSet(554" not in v2
                and "dispatchIssueCommand(554" not in v2
                and "dispatchHiddenCallback(554" not in v2
                and "registry->ndp" not in v2
            ),
            "skywalk_has_no_ndp_cache_copy": (
                "runSetOFFLOADNDP(data, &asyncContext)" in skywalk_setter
                and "cachedIPv6Count" not in skywalk_setter
                and "cachedIPv6Addresses" not in skywalk_setter
                and "cachedIPv6LinkLocalAddress" not in skywalk_setter
            ),
            "fabricated_ndp_state_has_no_consumer": "registry->ndp." not in source,
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
        raise ValueError("NDP offload quarantine checks failed: " + ", ".join(failed))
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
        print(f"NDP offload quarantine validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
