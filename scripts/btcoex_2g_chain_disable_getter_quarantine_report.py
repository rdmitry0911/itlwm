#!/usr/bin/env python3
"""Generate and verify BTCOEX 2G chain-disable getter evidence."""

import argparse
import hashlib
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/btcoex_2g_chain_disable_getter_quarantine_report.json"
NOTE = ROOT / "docs/reference/CR-496-btcoex-2g-chain-disable-getter-no-producer-quarantine-20260715.md"
LEGACY_NOTE = ROOT / "docs/reference/CR-479-btcoex-public-quarantine-20260713.md"
RAW = ROOT / "docs/reference/artifacts/btcoex-2g-chain-disable-getter-25c56/raw.txt"
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
ACTIVE_REPORT = ROOT / "scripts/btcoex_profile_active_getter_quarantine_report.py"
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
    active_report = ACTIVE_REPORT.read_text(encoding="utf-8")
    payload_report = PAYLOAD_REPORT.read_text(encoding="utf-8")
    getter = section(
        cpp,
        "getBTCOEX_2G_CHAIN_DISABLE(apple80211_btcoex_2g_chain_disable *data)",
        "getBSS_BLACKLIST(bss_blacklist *data)",
    )
    setter = section(
        cpp,
        "setBTCOEX_2G_CHAIN_DISABLE(apple80211_btcoex_2g_chain_disable *data)",
        "setRESET_CHIP(apple80211_reset_command *)",
    )
    active_getter = section(
        cpp,
        "getBTCOEX_PROFILE_ACTIVE(apple80211_btcoex_profile_active_data *data)",
        "getMAX_NSS_FOR_AP(apple80211_btcoex_max_nss_for_ap_data *data)",
    )
    dispatch = section(
        cpp,
        "case APPLE80211_IOC_BTCOEX_2G_CHAIN_DISABLE:",
        "case APPLE80211_IOC_TKO_PARAMS:",
    )
    payload_chain = section(
        payload_report,
        '"name": "btcoex-2g-chain-disable",',
        '"name": "tx-power-cap-quarantine",',
    )
    raw_digest = hashlib.sha256(RAW.read_bytes()).hexdigest()

    return {
        "schema": "itlwm-btcoex-2g-chain-disable-getter-no-producer-quarantine-v1",
        "source_base_revision": "e04c5dc33ee6bdaa7ba5b2fee7d417927d9f1669",
        "reference": {
            "image_sha256": "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
            "image_uuid_x86_64": "149C0AD1-A92F-35BC-AA69-5C8815C5421E",
            "infra_get_slot": 502,
            "infra_get_wrapper": "0x10001758c",
            "core_get_vtable_offset": "0x3e8",
            "core_getter": "0x1001e57fc",
            "commander_from_core": "0x48+0x1520",
            "get_iovar": "btc_2g_shchain_disable",
            "get_special_status": "0xe00002e3",
            "infra_set_wrapper": "0x1000187ac",
            "core_set_vtable_offset": "0x690",
            "core_setter": "0x1001e3a3e",
        },
        "local": {
            "chain_disable_get_producer": False,
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
                    "0x10001758c",
                    "0x3e8(%rax)",
                    "__ZTV16AppleBCMWLANCore",
                    "0x1003A10D8",
                    "0x1003A14D0         rebase  0x1001E57FC",
                    "0x1001e57fc",
                    "testq  %rsi",
                    "0xe00002c2",
                    "0x48(%rdi)",
                    "0x1520(%rax)",
                    "runIOVarGet",
                    '"btc_2g_shchain_disable"',
                    "0xe00002e3",
                    "0x4(%r13)",
                    "0x5(%r13)",
                    "0x1000187ac",
                    "0x690(%rax)",
                    "0x1003A1778         rebase  0x1001E3A3E",
                    "0x1001e3a3e",
                    "$0x60001",
                    "runIOVarSet",
                )
            ),
            "reference_note_has_scope_and_nonclaim": all(
                token in note
                for token in (
                    "[502]",
                    "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
                    "149C0AD1-A92F-35BC-AA69-5C8815C5421E",
                    "0x10001758c",
                    "0x3e8",
                    "0x1001e57fc",
                    "runIOVarGet",
                    "0xe00002e3",
                    "not Apple null-input, valid-input return-code, value, carrier-layout",
                    "runtime-selector parity",
                )
            ),
            "chain_v2_slot_abi_and_ioc_route_remain": (
                "// [502]" in hpp
                and "getBTCOEX_2G_CHAIN_DISABLE" in hpp
                and "fail closed" in hpp
                and "struct apple80211_btcoex_2g_chain_disable;" in infra
                and "// [502]" in infra
                and "getBTCOEX_2G_CHAIN_DISABLE" in infra
                and "setBTCOEX_2G_CHAIN_DISABLE" in infra
                and "#define APPLE80211_IOC_BTCOEX_2G_CHAIN_DISABLE 260" in ioctl
                and all(
                    token in dispatch
                    for token in (
                        "cmd == SIOCGA80211",
                        "getBTCOEX_2G_CHAIN_DISABLE",
                        "cmd == SIOCSA80211",
                        "setBTCOEX_2G_CHAIN_DISABLE",
                        "return kIOReturnUnsupported;",
                    )
                )
                and "Btcoex2GChainDisablePayload" in builders
                and "buildBtcoex2GChainDisable" in builders
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
                    "cachedBtcoex2GChainDisable",
                    "kIOReturnSuccess",
                )
            ),
            "dead_chain_disable_getter_cache_is_removed": not source_contains(
                "cachedBtcoex2GChainDisable"
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
                    "runSetBTCOEX2GChainDisable",
                    "cachedBtcoex2GChainDisable",
                )
            ),
            "separate_btcoex_surfaces_are_preserved": (
                "btcoex" in registry
                and "getBTCOEX_PROFILE_ACTIVE" in cpp
                and "getMAX_NSS_FOR_AP" in cpp
                and "setBTCOEX_PROFILE" in cpp
                and "setBTCOEX_PROFILE_ACTIVE" in cpp
            ),
            "legacy_and_payload_guards_are_narrowed": (
                "active_and_chain_getters_fail_closed" in legacy_report
                and "cachedBtcoex2GChainDisable" not in payload_chain
                and "return kIOReturnUnsupported;" in payload_chain
                and "separate_btcoex_surfaces_are_preserved" in active_report
            ),
            "historical_cache_classification_is_superseded": (
                "## 2026-07-15 correction: BTCOEX_2G_CHAIN_DISABLE getter no-producer quarantine"
                in signal_audit
                and "cache-success classification is superseded" in signal_audit
                and "## 2026-07-15 correction: BTCOEX_2G_CHAIN_DISABLE getter no-producer quarantine"
                in inventory
                and "cache-success classification is superseded" in inventory
                and "CR-496 the chain-disable portion" in legacy_note
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
            "BTCOEX 2G chain-disable getter no-producer checks failed: " + ", ".join(failed)
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
        print(f"BTCOEX 2G chain-disable getter no-producer validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
