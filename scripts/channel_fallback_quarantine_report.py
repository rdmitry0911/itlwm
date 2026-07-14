#!/usr/bin/env python3
"""Generate and verify Tahoe no-APSTA channel fallback quarantine evidence."""

import argparse
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/channel_fallback_quarantine_report.json"
NOTE = ROOT / "docs/reference/CR-479-channel-fallback-quarantine-20260714.md"
CPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.cpp"
HPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.hpp"
V2 = ROOT / "AirportItlwm/AirportItlwmV2.cpp"
INFRA = ROOT / "include/Airport/IO80211InfraProtocol.h"


def section(source, begin, end):
    start = source.index(begin)
    return source[start:source.index(end, start)]


def report():
    cpp = CPP.read_text(encoding="utf-8")
    hpp = HPP.read_text(encoding="utf-8")
    v2 = V2.read_text(encoding="utf-8")
    infra = INFRA.read_text(encoding="utf-8")
    note = NOTE.read_text(encoding="utf-8")
    setter = section(cpp, "setCHANNEL(apple80211_channel_data *data)", "setTXPOWER(")
    route = section(cpp, "case APPLE80211_IOC_CHANNEL:", "case APPLE80211_IOC_AUTH_TYPE:")
    owner_route = section(v2, "setAPSTA_CHANNEL(OSObject *object,", "setHOST_AP_MODE(")

    return {
        "schema": "itlwm-channel-fallback-quarantine-v1",
        "source_base_revision": "d6fb256eb1d42e03958c9388a08f2ce3c1a4069a",
        "reference": {
            "kdk_sha256": "db163f75110e7e79aafa580396285275df1a7a0105da82cf1a05912a5e24c401",
            "kernel_collections_sha256": "aaf052e7fcb4ee9c9a1e1d76a57e85966a853ea19cf9dc76183959ea416ca5a8",
            "symbol_map_sha256": "1ee52696618d1ed8d14272c077121dccd1fb90c6f6ced6bc737abd2bf099b748",
            "core_setter": "0xffffff8001602b3e",
            "chanspec_helper": "0xffffff8001602f74",
            "null_status": "0xe00002bc",
            "invalid_status": "0x16",
            "zero_chanspec_status": "0xe00002c2",
        },
        "local": {
            "false_success": False,
            "valid_input_return_is_apple_parity": False,
            "runtime_selector_invocation": False,
            "apsta_owner_branch_modified": False,
        },
        "checks": {
            "reference_note_has_core_and_nonclaim": all(
                token in note
                for token in (
                    "db163f75110e7e79aafa580396285275df1a7a0105da82cf1a05912a5e24c401",
                    "aaf052e7fcb4ee9c9a1e1d76a57e85966a853ea19cf9dc76183959ea416ca5a8",
                    "1ee52696618d1ed8d14272c077121dccd1fb90c6f6ced6bc737abd2bf099b748",
                    "0xffffff8001602b3e",
                    "0xffffff8001602f74",
                    "four-byte `chanspec` firmware IOVAR",
                    "not Apple valid-input return-code parity",
                )
            ),
            "active_channel_slot_and_route_remain": (
                "setCHANNEL(apple80211_channel_data *) = 0;" in infra
                and "APPLE80211_IOC_CHANNEL" in route
                and "instance->setAPSTA_CHANNEL(this" in route
            ),
            "internal_input_gates_are_retained": (
                "if (data == nullptr)" in setter
                and "return kIOReturnBadArgumentTahoe;" in setter
                and "if (data->channel.channel >= 0x100)" in setter
                and "return kApple80211ErrInvalidArgumentRaw;" in setter
                and "if (ic == nullptr || data->channel.channel == 0)" in setter
                and "return static_cast<IOReturn>(0xe00002c2);" in setter
            ),
            "known_channel_fails_closed": (
                "ieee80211_chan2ieee" in setter
                and "return kIOReturnUnsupported;" in setter
                and "kIOReturnSuccess" not in setter
            ),
            "dead_cache_removed": (
                "cachedRequestedChannel" not in cpp
                and "hasCachedRequestedChannel" not in cpp
                and "cachedRequestedChannel" not in hpp
                and "hasCachedRequestedChannel" not in hpp
            ),
            "apsta_owner_path_is_retained": (
                "if (fAPSTAOwner == NULL)" in owner_route
                and "interface->setCHANNEL(in);" in owner_route
                and "return fAPSTAOwner->setChannel(in);" in owner_route
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
        raise ValueError("channel fallback quarantine checks failed: " + ", ".join(failed))
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
        print(f"channel fallback quarantine validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
