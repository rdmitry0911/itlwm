#!/usr/bin/env python3
"""Generate and verify BTCOEX public-setter quarantine evidence."""

import argparse
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/btcoex_public_quarantine_report.json"
NOTE = ROOT / "docs/reference/CR-479-btcoex-public-quarantine-20260713.md"
CPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.cpp"
HPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.hpp"
SOURCE_ROOTS = (ROOT / "AirportItlwm", ROOT / "include", ROOT / "itl80211", ROOT / "itlwm")


def section(source, begin, end):
    start = source.index(begin)
    return source[start:source.index(end, start)]


def source_contains(token):
    for root in SOURCE_ROOTS:
        for path in root.rglob("*"):
            if path.suffix not in {".c", ".cc", ".cpp", ".h", ".hpp"}:
                continue
            if token in path.read_text(encoding="utf-8", errors="ignore"):
                return True
    return False


def report():
    cpp = CPP.read_text(encoding="utf-8")
    hpp = HPP.read_text(encoding="utf-8")
    note = NOTE.read_text(encoding="utf-8")
    profile = section(
        cpp,
        "IOReturn AirportItlwmSkywalkInterface::\nsetBTCOEX_PROFILE",
        "IOReturn AirportItlwmSkywalkInterface::\nsetBTCOEX_PROFILE_ACTIVE",
    )
    active = section(
        cpp,
        "IOReturn AirportItlwmSkywalkInterface::\nsetBTCOEX_PROFILE_ACTIVE",
        "IOReturn AirportItlwmSkywalkInterface::\nsetBTCOEX_2G_CHAIN_DISABLE",
    )
    chain = section(
        cpp,
        "IOReturn AirportItlwmSkywalkInterface::\nsetBTCOEX_2G_CHAIN_DISABLE",
        "IOReturn AirportItlwmSkywalkInterface::\nsetRESET_CHIP",
    )
    active_getter = section(
        cpp,
        "IOReturn AirportItlwmSkywalkInterface::\ngetBTCOEX_PROFILE_ACTIVE",
        "IOReturn AirportItlwmSkywalkInterface::\ngetMAX_NSS_FOR_AP",
    )
    chain_getter = section(
        cpp,
        "IOReturn AirportItlwmSkywalkInterface::\ngetBTCOEX_2G_CHAIN_DISABLE",
        "IOReturn AirportItlwmSkywalkInterface::\ngetBSS_BLACKLIST",
    )

    return {
        "schema": "itlwm-btcoex-public-quarantine-v3",
        "source_base_revision": "e04c5dc33ee6bdaa7ba5b2fee7d417927d9f1669",
        "reference": {
            "image_sha256": "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
            "profile": {
                "infra_wrapper": "0x1000186c8",
                "infra_virtual_offset": "0x670",
                "core_setter": "0x100124656",
                "iovar": "btc_profile",
                "record_bytes": 56,
            },
            "active": {
                "infra_wrapper": "0x100018714",
                "infra_virtual_offset": "0x698",
                "core_setter": "0x1001e393a",
                "iovar": "btc_profile_active",
                "payload_bytes": 4,
            },
            "chain_disable": {
                "infra_wrapper": "0x1000187ac",
                "infra_virtual_offset": "0x690",
                "core_setter": "0x1001e3a3e",
                "iovar": "btc_2g_shchain_disable",
                "payload_bytes": 6,
                "header": "0x00060001",
            },
            "invalid_status": "0xe00002c2",
        },
        "local": {
            "backend_btcoex_commander": False,
            "false_success": False,
            "active_getter_false_success": False,
            "chain_disable_getter_false_success": False,
            "valid_input_return_is_apple_parity": False,
        },
        "checks": {
            "reference_note": all(
                token in note
                for token in (
                    "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
                    "0x1000186c8",
                    "`+0x670`",
                    "0x100124656",
                    "`0xe00002c2`",
                    "`+0x03 >= 5`",
                    "`1..4`",
                    "`+0x04 >= 10`",
                    "`0x38`-byte",
                    "`btc_profile`",
                    "0x100018714",
                    "`+0x698`",
                    "0x1001e393a",
                    "`btc_profile_active`",
                    "0x1000187ac",
                    "`+0x690`",
                    "0x1001e3a3e",
                    "`0x00060001`",
                    "`btc_2g_shchain_disable`",
                    "runIOVarSet",
                    "not establish a complete public allocation",
                    "not Apple valid-input return-code parity",
                )
            ),
            "profile_preserves_invalid_gates_and_quarantines_valid": all(
                token in profile
                for token in (
                    "if (data == nullptr || instance == nullptr)",
                    "TahoePayloadBuilders::BtcoexProfilePayload payload;",
                    "!TahoePayloadBuilders::buildBtcoexProfile(data, &payload)",
                    "payload.band >= 5 || payload.mode < 1 || payload.mode > 4",
                    "payload.profileIndex >= 10",
                    "return static_cast<IOReturn>(0xe00002c2);",
                    "return kIOReturnUnsupported;",
                )
            )
            and all(token not in profile for token in ("runSetBTCOEXProfile", "cachedBtcoex")),
            "active_and_chain_quarantine_valid": all(
                "if (data == nullptr || instance == nullptr)" in setter
                and "return static_cast<IOReturn>(0xe00002c2);" in setter
                and "return kIOReturnUnsupported;" in setter
                and "runSetBTCOEX" not in setter
                and "cachedBtcoex" not in setter
                for setter in (active, chain)
            ),
            "dead_profile_pseudo_state_removed": all(
                token not in cpp and token not in hpp
                for token in ("cachedBtcoexProfiles", "cachedBtcoexProfileValidMask")
            ),
            "active_and_chain_getters_fail_closed": (
                all(
                    all(
                        token in getter
                        for token in (
                            "if (data == nullptr)",
                            "return static_cast<IOReturn>(0xe00002c2);",
                            "(void)data;",
                            "return kIOReturnUnsupported;",
                        )
                    )
                    and all(
                        token not in getter
                        for token in (
                            "memset",
                            "reinterpret_cast",
                            cache,
                            "kIOReturnSuccess",
                        )
                    )
                    for getter, cache in (
                        (active_getter, "cachedBtcoexProfileActive"),
                        (chain_getter, "cachedBtcoex2GChainDisable"),
                    )
                )
                and all(
                    cache not in cpp + hpp
                    for cache in (
                        "cachedBtcoexProfileActive",
                        "cachedBtcoex2GChainDisable",
                    )
                )
            ),
            "no_local_btcoex_commander_transport": all(
                not source_contains(token)
                for token in (
                    "AppleBCMWLANCommander",
                    "runIOVarSet(",
                    '"btc_profile"',
                    '"btc_2g_shchain_disable"',
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
        raise ValueError("BTCOEX public quarantine checks failed: " + ", ".join(failed))
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
        print(f"BTCOEX public quarantine validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
