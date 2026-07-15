#!/usr/bin/env python3
"""Generate and verify AWDL_RSDB_CAPS no-producer quarantine evidence."""

import argparse
import hashlib
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/awdl_rsdb_caps_quarantine_report.json"
NOTE = ROOT / "docs/reference/CR-493-awdl-rsdb-caps-no-producer-quarantine-20260715.md"
RAW = ROOT / "docs/reference/artifacts/awdl-rsdb-caps-25c56/raw.txt"
RAW_MANIFEST = RAW.with_name("SHA256SUMS.txt")
INVENTORY = ROOT / "docs/tahoe_discrepancy_inventory.md"
SIGNAL_AUDIT = ROOT / "docs/tahoe_signal_chain_audit.md"
CPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.cpp"
HPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.hpp"
V2 = ROOT / "AirportItlwm/AirportItlwmV2.cpp"
INFRA = ROOT / "include/Airport/IO80211InfraProtocol.h"
IOCTL = ROOT / "include/Airport/apple80211_ioctl.h"
PEER_MANAGER = ROOT / "include/Airport/IO80211PeerManager.h"
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
            if path.suffix not in {".c", ".cc", ".cpp", ".h", ".hpp"}:
                continue
            if token in path.read_text(encoding="utf-8", errors="ignore"):
                return True
    return False


def report():
    cpp = CPP.read_text(encoding="utf-8")
    hpp = HPP.read_text(encoding="utf-8")
    v2 = V2.read_text(encoding="utf-8")
    infra = INFRA.read_text(encoding="utf-8")
    ioctl = IOCTL.read_text(encoding="utf-8")
    peer_manager = PEER_MANAGER.read_text(encoding="utf-8")
    note = " ".join(NOTE.read_text(encoding="utf-8").split())
    raw = RAW.read_text(encoding="utf-8")
    manifest = RAW_MANIFEST.read_text(encoding="utf-8")
    inventory = INVENTORY.read_text(encoding="utf-8")
    signal_audit = SIGNAL_AUDIT.read_text(encoding="utf-8")
    getter = section(
        cpp,
        "getAWDL_RSDB_CAPS(apple80211_rsdb_capability *data)",
        "getTKO_PARAMS(apple80211_tko_params *data)",
    )
    dispatch = section(
        cpp,
        "case APPLE80211_IOC_AWDL_RSDB_CAPS:",
        "case APPLE80211_IOC_TKO_DUMP:",
    )
    core_get_raw = section(
        raw,
        "(lldb) disassemble -b -s 0x1001328fa",
        "(lldb) disassemble -b -s 0x10008b716",
    )
    telemetry_zone = section(
        signal_audit,
        "## Q13 Telemetry/Cache Getter Zone:",
        "## Q13 First Confirmed Apple-Unsupported Setter Batch",
    )
    closed_zone = section(
        telemetry_zone,
        "Closed in this zone:",
        "Recovered Apple behavior splits into three public buckets:",
    )
    state_backed_zone = section(
        telemetry_zone,
        "- state-backed telemetry carriers:",
        "This batch intentionally stops at the public Apple80211 boundary:",
    )
    raw_digest = hashlib.sha256(RAW.read_bytes()).hexdigest()

    return {
        "schema": "itlwm-awdl-rsdb-caps-no-producer-quarantine-v1",
        "source_base_revision": "d374793f7938b1a1ea184edf95341f0e0a3e5019",
        "reference": {
            "image_sha256": "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
            "image_uuid_x86_64": "149C0AD1-A92F-35BC-AA69-5C8815C5421E",
            "infra_slot": 493,
            "infra_get_wrapper": "0x100017a20",
            "core_get_vtable_offset": "0x388",
            "core_getter": "0x1001328fa",
            "core_state_window_from_core": "0x48+0x436",
            "config_query": "0x10008b716",
            "commander_rsdb_get": "0x10017b780",
            "core_update": "0x1000d9a70",
            "observed_update_start": "0x438",
        },
        "local": {
            "rsdb_query_owner_backend": False,
            "synthetic_success": False,
            "null_return_is_apple_parity": False,
            "valid_input_return_is_apple_parity": False,
            "runtime_selector_invocation": False,
        },
        "checks": {
            "reference_raw_manifest_matches": manifest == f"{raw_digest}  raw.txt\n",
            "reference_raw_has_getter_and_observed_producer_context": all(
                token in raw
                for token in (
                    "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
                    "149C0AD1-A92F-35BC-AA69-5C8815C5421E",
                    "0x100017a20",
                    "0x388(%rax)",
                    "0x1001328fa",
                    "0x48(%rdi)",
                    "0x436(%rax)",
                    "0x4(%rsi)",
                    "xorl   %eax, %eax",
                    "0x10008b716",
                    "checkForSDBSupport",
                    "\"rsdb\"",
                    "runIOVarGet",
                    "0x1000d9a70",
                    "updateRSDBCaps",
                    "0x438(%rcx)",
                )
            ),
            "reference_getter_has_no_observed_null_guard": (
                "testq  %rsi" not in core_get_raw
                and "testl  %esi" not in core_get_raw
                and "0x4(%rsi)" in core_get_raw
            ),
            "reference_note_has_scope_and_nonclaim": all(
                token in note
                for token in (
                    "slot `[493]`",
                    "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
                    "149C0AD1-A92F-35BC-AA69-5C8815C5421E",
                    "`0x100017a20`",
                    "`0x388`",
                    "`0x1001328fa`",
                    "(Core + 0x48) + 0x436",
                    "`0x10008b716`",
                    'runIOVarGet("rsdb")',
                    "`0x1000d9a70`",
                    "not Apple null-input, valid-input return-code, full carrier-layout, version, Core-state, AWDL-feature, or runtime-selector parity",
                )
            ),
            "active_v2_slot_selector_and_get_route_remain": (
                "// [493]" in hpp
                and "getAWDL_RSDB_CAPS" in hpp
                and "Keep this slot fail-closed" in hpp
                and "struct apple80211_rsdb_capability;" in infra
                and "// [493]" in infra
                and "getAWDL_RSDB_CAPS" in infra
                and "#define APPLE80211_IOC_AWDL_RSDB_CAPS 246" in ioctl
                and "fNetIf = new AirportItlwmSkywalkInterface;" in v2
                and all(
                    token in dispatch
                    for token in (
                        "cmd == SIOCGA80211",
                        "getAWDL_RSDB_CAPS",
                        "kIOReturnUnsupported",
                    )
                )
                and "SIOCSA80211" not in dispatch
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
                    "data->",
                    "memset",
                    "reinterpret_cast",
                    "cachedAwdlRsdbCaps",
                    "return kIOReturnSuccess;",
                )
            ),
            "dead_rsdb_cache_is_removed": not source_contains("cachedAwdlRsdbCaps"),
            "no_matching_local_operational_rsdb_producer": all(
                not source_contains(token)
                for token in (
                    "querySDBPolicies",
                    "updateRSDBCaps",
                    'runIOVarGet("rsdb")',
                )
            ),
            "opaque_carrier_and_separate_rsdb_surfaces_are_preserved": (
                "struct apple80211_rsdb_capability;" in infra
                and "sizeof(apple80211_rsdb_capability)" not in cpp + hpp + infra + ioctl
                and "unsigned int getRsdbCap(void);" in peer_manager
                and "bool isRsdbSupported(void);" in peer_manager
                and "setSDB_ENABLE" in cpp
                and "setSDB_ENABLE" in hpp
            ),
            "historical_cache_classification_is_superseded": (
                "### 2026-07-15 correction: `AWDL_RSDB_CAPS` getter is a no-producer quarantine"
                in signal_audit
                and "getAWDL_RSDB_CAPS" not in closed_zone
                and "getAWDL_RSDB_CAPS" not in state_backed_zone
                and "`Q13 correction: AWDL_RSDB_CAPS getter no-producer quarantine`"
                in inventory
                and "no longer included in that closed cache/state group" in inventory
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
        raise ValueError("AWDL_RSDB_CAPS no-producer checks failed: " + ", ".join(failed))
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
        print(f"AWDL_RSDB_CAPS no-producer validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
