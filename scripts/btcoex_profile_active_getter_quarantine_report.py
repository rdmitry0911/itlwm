#!/usr/bin/env python3
"""Generate and verify BTCOEX_PROFILE_ACTIVE getter no-producer evidence."""

import argparse
import hashlib
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/btcoex_profile_active_getter_quarantine_report.json"
NOTE = ROOT / "docs/reference/CR-495-btcoex-profile-active-getter-no-producer-quarantine-20260715.md"
LEGACY_NOTE = ROOT / "docs/reference/CR-479-btcoex-public-quarantine-20260713.md"
RAW = ROOT / "docs/reference/artifacts/btcoex-profile-active-getter-25c56/raw.txt"
RAW_MANIFEST = RAW.with_name("SHA256SUMS.txt")
SIGNAL_AUDIT = ROOT / "docs/tahoe_signal_chain_audit.md"
INVENTORY = ROOT / "docs/tahoe_discrepancy_inventory.md"
CPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.cpp"
HPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.hpp"
INFRA = ROOT / "include/Airport/IO80211InfraProtocol.h"
IOCTL = ROOT / "include/Airport/apple80211_ioctl.h"
BUILDERS = ROOT / "AirportItlwm/TahoePayloadBuilders.hpp"
REGISTRY = ROOT / "AirportItlwm/TahoeOwnerRegistry.hpp"
LEGACY_REPORT = ROOT / "scripts/btcoex_public_quarantine_report.py"
PAYLOAD_REPORT = ROOT / "scripts/payload_parity_report.py"
SOURCE_ROOTS = (
    ROOT / "AirportItlwm",
    ROOT / "include",
    ROOT / "itl80211",
    ROOT / "itlwm",
)


def section(source, begin, end):
    start = source.index(begin)
    return source[start:source.index(end, start)]


def source_contains(token):
    for root in SOURCE_ROOTS:
        for path in root.rglob("*"):
            if not path.is_file() or path.suffix not in {".c", ".cc", ".cpp", ".h", ".hpp"}:
                continue
            if token in path.read_text(encoding="utf-8", errors="ignore"):
                return True
    return False


def report():
    cpp = CPP.read_text(encoding="utf-8")
    hpp = HPP.read_text(encoding="utf-8")
    infra = INFRA.read_text(encoding="utf-8")
    ioctl = IOCTL.read_text(encoding="utf-8")
    builders = BUILDERS.read_text(encoding="utf-8")
    registry = REGISTRY.read_text(encoding="utf-8")
    note = " ".join(NOTE.read_text(encoding="utf-8").split())
    legacy_note = LEGACY_NOTE.read_text(encoding="utf-8")
    raw = RAW.read_text(encoding="utf-8")
    manifest = RAW_MANIFEST.read_text(encoding="utf-8")
    signal_audit = SIGNAL_AUDIT.read_text(encoding="utf-8")
    inventory = INVENTORY.read_text(encoding="utf-8")
    legacy_report = LEGACY_REPORT.read_text(encoding="utf-8")
    payload_report = PAYLOAD_REPORT.read_text(encoding="utf-8")
    getter = section(
        cpp,
        "getBTCOEX_PROFILE_ACTIVE(apple80211_btcoex_profile_active_data *data)",
        "getMAX_NSS_FOR_AP(apple80211_btcoex_max_nss_for_ap_data *data)",
    )
    setter = section(
        cpp,
        "setBTCOEX_PROFILE_ACTIVE(apple80211_btcoex_profile_active_data *data)",
        "setBTCOEX_2G_CHAIN_DISABLE(apple80211_btcoex_2g_chain_disable *data)",
    )
    chain_getter = section(
        cpp,
        "getBTCOEX_2G_CHAIN_DISABLE(apple80211_btcoex_2g_chain_disable *data)",
        "getBSS_BLACKLIST(bss_blacklist *data)",
    )
    dispatch = section(
        cpp,
        "case APPLE80211_IOC_BTCOEX_PROFILE_ACTIVE:",
        "case APPLE80211_IOC_MAX_NSS_FOR_AP:",
    )
    payload_active = section(
        payload_report,
        '"name": "btcoex-profile-active",',
        '"name": "btcoex-2g-chain-disable",',
    )
    raw_digest = hashlib.sha256(RAW.read_bytes()).hexdigest()

    return {
        "schema": "itlwm-btcoex-profile-active-getter-no-producer-quarantine-v1",
        "source_base_revision": "a1e4df046d38f4bc95deca7a2b41ea0a71533934",
        "reference": {
            "image_sha256": "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
            "image_uuid_x86_64": "149C0AD1-A92F-35BC-AA69-5C8815C5421E",
            "infra_get_slot": 498,
            "infra_get_wrapper": "0x100017470",
            "core_get_vtable_offset": "0x3b0",
            "core_getter": "0x1001e509a",
            "commander_from_core": "0x48+0x1520",
            "get_iovar": "btc_profile_active",
            "get_special_status": "0xe00002e3",
            "infra_set_wrapper": "0x100018714",
            "core_set_vtable_offset": "0x698",
            "core_setter": "0x1001e393a",
        },
        "local": {
            "active_get_producer": False,
            "synthetic_success": False,
            "null_return_is_apple_parity": False,
            "valid_input_return_is_apple_parity": False,
            "runtime_selector_invocation": False,
        },
        "checks": {
            "reference_raw_manifest_matches": manifest == f"{raw_digest}  raw.txt\n",
            "reference_raw_has_get_and_set_transport_anchors": all(
                token in raw
                for token in (
                    "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
                    "149C0AD1-A92F-35BC-AA69-5C8815C5421E",
                    "0x100017470",
                    "0x3b0(%rax)",
                    "__ZTV16AppleBCMWLANCore",
                    "0x1003A10D8",
                    "0x1003A1498         rebase  0x1001E509A",
                    "0x1001e509a",
                    "testq  %rsi",
                    "0xe00002c2",
                    "0x48(%rdi)",
                    "0x1520(%rax)",
                    "runIOVarGet",
                    '"btc_profile_active"',
                    "0xe00002e3",
                    "0x4(%r15)",
                    "0x100018714",
                    "0x698(%rax)",
                    "0x1003A1780         rebase  0x1001E393A",
                    "0x1001e393a",
                    "runIOVarSet",
                    "$0x4, 0x8(%rdx)",
                )
            ),
            "reference_note_has_scope_and_nonclaim": all(
                token in note
                for token in (
                    "[498]",
                    "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
                    "149C0AD1-A92F-35BC-AA69-5C8815C5421E",
                    "0x100017470",
                    "0x3b0",
                    "0x1001e509a",
                    "runIOVarGet",
                    "0xe00002e3",
                    "not Apple null-input, valid-input return-code, value, carrier-layout",
                    "runtime-selector parity",
                )
            ),
            "active_v2_slot_abi_and_ioc_route_remain": (
                "// [498]" in hpp
                and "getBTCOEX_PROFILE_ACTIVE" in hpp
                and "fail closed" in hpp
                and "struct apple80211_btcoex_profile_active_data;" in infra
                and "// [498]" in infra
                and "getBTCOEX_PROFILE_ACTIVE" in infra
                and "setBTCOEX_PROFILE_ACTIVE" in infra
                and "#define APPLE80211_IOC_BTCOEX_PROFILE_ACTIVE 256" in ioctl
                and all(
                    token in dispatch
                    for token in (
                        "cmd == SIOCGA80211",
                        "getBTCOEX_PROFILE_ACTIVE",
                        "cmd == SIOCSA80211",
                        "setBTCOEX_PROFILE_ACTIVE",
                        "return kIOReturnUnsupported;",
                    )
                )
                and "uint8_t bytes[0x8];" in builders
                and "buildBtcoexProfileActive" in builders
            ),
            "local_null_guard_is_retained_as_safety_boundary": (
                "if (data == nullptr)" in getter
                and "return static_cast<IOReturn>(0xe00002c2);" in getter
            ),
            "nonnull_getter_fails_closed_without_output": all(
                token in getter
                for token in (
                    "(void)data;",
                    "return kIOReturnUnsupported;",
                )
            )
            and all(
                token not in getter
                for token in (
                    "memset",
                    "reinterpret_cast",
                    "cachedBtcoexProfileActive",
                    "kIOReturnSuccess",
                )
            ),
            "dead_active_getter_cache_is_removed": not source_contains(
                "cachedBtcoexProfileActive"
            ),
            "paired_setter_remains_fail_closed_without_getter_cache": all(
                token in setter
                for token in (
                    "if (data == nullptr || instance == nullptr)",
                    "return static_cast<IOReturn>(0xe00002c2);",
                    "return kIOReturnUnsupported;",
                )
            )
            and all(
                token not in setter
                for token in (
                    "runSetBTCOEXProfileActive",
                    "cachedBtcoexProfileActive",
                )
            ),
            "separate_btcoex_and_chain_surfaces_are_preserved": (
                "btcoex" in registry
                and "cachedBtcoex2GChainDisable" in hpp
                and "cachedBtcoex2GChainDisable" in chain_getter
                and "setBTCOEX_2G_CHAIN_DISABLE" in cpp
                and "getMAX_NSS_FOR_AP" in cpp
                and "getBTCOEX_PROFILE(" in cpp
            ),
            "legacy_and_payload_guards_are_narrowed": (
                "active_getter_fails_closed_and_chain_scope_preserved" in legacy_report
                and '"getter_scope_preserved"' not in legacy_report
                and "cachedBtcoexProfileActive" not in payload_active
                and "return kIOReturnUnsupported;" in payload_active
            ),
            "historical_cache_classification_is_superseded": (
                "## 2026-07-15 correction: BTCOEX_PROFILE_ACTIVE getter no-producer quarantine"
                in signal_audit
                and "cache-success classification is superseded" in signal_audit
                and "## 2026-07-15 correction: BTCOEX_PROFILE_ACTIVE getter no-producer quarantine"
                in inventory
                and "cache-success classification is superseded" in inventory
                and "CR-495 supersedes only this record's former active-getter cache scope"
                in legacy_note
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
        raise ValueError(
            "BTCOEX_PROFILE_ACTIVE getter no-producer checks failed: " + ", ".join(failed)
        )
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
        print(f"BTCOEX_PROFILE_ACTIVE getter no-producer validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
