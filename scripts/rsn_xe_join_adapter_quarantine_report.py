#!/usr/bin/env python3
"""Generate and verify RSN_XE JoinAdapter no-producer evidence."""

import argparse
import hashlib
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/rsn_xe_join_adapter_quarantine_report.json"
NOTE = ROOT / "docs/reference/CR-497-rsn-xe-join-adapter-no-producer-quarantine-20260715.md"
RAW = ROOT / "docs/reference/artifacts/rsn-xe-join-adapter-25c56/raw.txt"
RAW_MANIFEST = RAW.with_name("SHA256SUMS.txt")
INVENTORY = ROOT / "docs/tahoe_discrepancy_inventory.md"
SIGNAL_AUDIT = ROOT / "docs/tahoe_signal_chain_audit.md"
CPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.cpp"
HPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.hpp"
INFRA = ROOT / "include/Airport/IO80211InfraProtocol.h"
IOCTL = ROOT / "include/Airport/apple80211_ioctl.h"
APSTA_CPP = ROOT / "AirportItlwm/AirportItlwmAPSTAOwner.cpp"
APSTA_HPP = ROOT / "AirportItlwm/AirportItlwmAPSTAInterface.hpp"
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
    apsta_cpp = APSTA_CPP.read_text(encoding="utf-8")
    apsta_hpp = APSTA_HPP.read_text(encoding="utf-8")
    note = " ".join(NOTE.read_text(encoding="utf-8").split())
    raw = RAW.read_text(encoding="utf-8")
    manifest = RAW_MANIFEST.read_text(encoding="utf-8")
    inventory = INVENTORY.read_text(encoding="utf-8")
    signal_audit = SIGNAL_AUDIT.read_text(encoding="utf-8")
    getter = section(
        cpp,
        "getRSN_XE(apple80211_rsn_xe_data *data)",
        "getSIB_COEX_STATUS(apple80211_sib_coex_status *data)",
    )
    setter = section(
        cpp,
        "setRSN_XE(apple80211_rsn_xe_data *data)",
        "setGAS_ABORT(void *)",
    )
    setter_zone = section(
        signal_audit,
        "## Q13 Minimal Setter-Contract Zone:",
        "## Q13 Telemetry/Cache Getter Zone:",
    )
    getter_zone = section(
        signal_audit,
        "## Q13 Telemetry/Cache Getter Zone:",
        "### 2026-07-15 correction: `AWDL_RSDB_CAPS` getter is a no-producer quarantine",
    )
    raw_digest = hashlib.sha256(RAW.read_bytes()).hexdigest()

    return {
        "schema": "itlwm-rsn-xe-join-adapter-no-producer-quarantine-v1",
        "source_base_revision": "121891b21657d2905f3f67057232e25e13de9bab",
        "reference": {
            "image_sha256": "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
            "image_uuid_x86_64": "149C0AD1-A92F-35BC-AA69-5C8815C5421E",
            "infra_get_slot": 531,
            "infra_get_wrapper": "0x100016f18",
            "core_get_vtable_offset": "0x348",
            "core_getter": "0x100107da0",
            "infra_set_slot": 606,
            "infra_set_wrapper": "0x1000181e4",
            "core_set_vtable_offset": "0x510",
            "core_setter": "0x100120e96",
            "join_adapter_from_core": "0x48+0x1528",
            "join_getter": "0x10003f0be",
            "join_setter": "0x10003f2c2",
            "carrier_capacity": "0x101",
        },
        "local": {
            "join_adapter_backend": False,
            "cache_echo": False,
            "numeric_ioc_route": False,
            "null_return_is_apple_parity": False,
            "valid_input_return_is_apple_parity": False,
            "runtime_selector_invocation": False,
        },
        "checks": {
            "reference_raw_manifest_matches": manifest == f"{raw_digest}  raw.txt\n",
            "reference_raw_has_owner_routing_and_gates": all(
                token in raw
                for token in (
                    "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
                    "149C0AD1-A92F-35BC-AA69-5C8815C5421E",
                    "0x100016f18",
                    "0x348(%rax)",
                    "0x1000181e4",
                    "0x510(%rax)",
                    "__ZTV16AppleBCMWLANCore",
                    "0x1003A10D8",
                    "0x1003A1430         rebase  0x100107DA0",
                    "0x1003A15F8         rebase  0x100120E96",
                    "0x100107da0",
                    "0x100120e96",
                    "0x100107da0  pushq  %rbp",
                    "0x100107de2  movw   %cx, 0x4(%rbx)",
                    "0x48(%rdi)",
                    "0x1528(%rax)",
                    "getAssocRSNXE",
                    "setAssocRSNXE",
                    "$0x101",
                    "$0xe00002c2",
                    "$0xe00002db",
                    "0x68(%rsi)",
                    "$0x101, %rbx",
                    "0x70",
                    "0x10003f0e7  je     0x10003f127",
                    "0x10003f0f8  je     0x10003f127",
                    "0x10003f10b  jb     0x10003f127",
                    "0x10003f30b  ja     0x10003f340",
                    "0x10003f315  testq  %rbx, %rbx",
                    "0x10003f318  je     0x10003f327",
                    "0x10003f325  jmp    0x10003f335",
                )
            ),
            "reference_note_has_scope_and_nonclaim": all(
                token in note
                for token in (
                    "[531]",
                    "[606]",
                    "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
                    "149C0AD1-A92F-35BC-AA69-5C8815C5421E",
                    "0x100016f18",
                    "0x1000181e4",
                    "JoinAdapter",
                    "0x101",
                    "not Apple null-input, valid-input return-code, carrier-layout",
                    "runtime-selector parity",
                    "no numeric `APPLE80211_IOC_RSN_XE`",
                )
            ),
            "protocol_slots_remain_without_invented_numeric_ioc": (
                "// [531]" in hpp
                and "getRSN_XE" in hpp
                and "// [606]" in hpp
                and "setRSN_XE" in hpp
                and "JoinAdapter" in hpp
                and "fail closed" in hpp
                and "struct apple80211_rsn_xe_data;" in infra
                and "// [531]" in infra
                and "getRSN_XE" in infra
                and "// [606]" in infra
                and "setRSN_XE" in infra
                and "APPLE80211_IOC_RSN_XE" not in ioctl
                and "APPLE80211_IOC_RSN_XE" not in cpp
            ),
            "local_null_guards_are_retained_as_safety_boundaries": all(
                "if (data == nullptr)" in body
                and "return kIOReturnBadArgumentTahoe;" in body
                for body in (getter, setter)
            ),
            "nonnull_get_and_set_fail_closed_without_cache_or_output": all(
                "(void)data;" in body
                and "return kIOReturnUnsupported;" in body
                and all(
                    token not in body
                    for token in (
                        "memset",
                        "memcpy",
                        "reinterpret_cast",
                        "cachedRsnXe",
                        "hasCachedRsnXe",
                        "kIOReturnSuccess",
                    )
                )
                for body in (getter, setter)
            ),
            "dead_rsnxe_cache_is_removed": all(
                not source_contains(token)
                for token in (
                    "cachedRsnXeLength",
                    "cachedRsnXe",
                    "hasCachedRsnXe",
                )
            ),
            "no_local_join_adapter_backend_is_introduced": all(
                not source_contains(token)
                for token in (
                    "getAssocRSNXE(",
                    "setAssocRSNXE(",
                    "getAssociatedRSNXE(",
                )
            ),
            "apsta_rsnxe_parser_and_distinct_rsn_surfaces_remain": (
                "apsta_copy_rsnxe" in apsta_cpp
                and "kAirportItlwmAPSTARSNXEElementId" in apsta_cpp
                and "0xf4" in apsta_hpp
                and "rsnxe10" in apsta_hpp
                and "getRSN_IE" in cpp
                and "setRSN_IE" in cpp
                and "setRSN_CONF" in cpp
            ),
            "historical_cache_classification_is_superseded": (
                "## 2026-07-15 correction: RSN_XE JoinAdapter no-producer quarantine"
                in inventory
                and "cache-success classification is superseded" in inventory
                and "## 2026-07-15 correction: RSN_XE JoinAdapter no-producer quarantine"
                in signal_audit
                and "cache-carrier classification for `getRSN_XE` and `setRSN_XE`\n"
                "is superseded" in signal_audit
                and "setRSN_XE(...)" not in setter_zone
                and "getRSN_XE(...)" not in getter_zone
                and "`getRSN_XE`, `getBSS_BLACKLIST`" not in getter_zone
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
        raise ValueError("RSN_XE JoinAdapter no-producer checks failed: " + ", ".join(failed))
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
        print(f"RSN_XE JoinAdapter no-producer validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
