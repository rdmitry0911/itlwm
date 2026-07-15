#!/usr/bin/env python3
"""Generate and verify POWER_BUDGET no-producer quarantine evidence."""

import argparse
import hashlib
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/power_budget_quarantine_report.json"
NOTE = ROOT / "docs/reference/CR-492-power-budget-no-producer-quarantine-20260715.md"
LEGACY_NOTE = ROOT / "docs/reference/CR-479-power-budget-quarantine-20260714.md"
RAW = ROOT / "docs/reference/artifacts/power-budget-25c56/raw.txt"
RAW_MANIFEST = RAW.with_name("SHA256SUMS.txt")
INVENTORY = ROOT / "docs/tahoe_discrepancy_inventory.md"
SIGNAL_AUDIT = ROOT / "docs/tahoe_signal_chain_audit.md"
CPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.cpp"
HPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.hpp"
V2 = ROOT / "AirportItlwm/AirportItlwmV2.cpp"
INFRA = ROOT / "include/Airport/IO80211InfraProtocol.h"
ABI = ROOT / "include/Airport/apple80211_ioctl.h"
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
    abi = ABI.read_text(encoding="utf-8")
    note = " ".join(NOTE.read_text(encoding="utf-8").split())
    legacy_note = LEGACY_NOTE.read_text(encoding="utf-8")
    raw = RAW.read_text(encoding="utf-8")
    manifest = RAW_MANIFEST.read_text(encoding="utf-8")
    inventory = INVENTORY.read_text(encoding="utf-8")
    signal_audit = SIGNAL_AUDIT.read_text(encoding="utf-8")
    getter = section(
        cpp,
        "getPOWER_BUDGET(apple80211_power_budget_t *data)",
        "getOFFLOAD_TCPKA_ENABLE",
    )
    setter = section(
        cpp,
        "setPOWER_BUDGET(apple80211_power_budget_t *data)",
        "setUSB_HOST_NOTIFICATION",
    )
    dispatch = section(
        cpp,
        "case APPLE80211_IOC_POWER_BUDGET:",
        "case APPLE80211_IOC_LQM_CONFIG:",
    )
    core_get_raw = section(
        raw,
        "(lldb) disassemble -b -s 0x10010712c",
        "(lldb) disassemble -b -s 0x1000187f8",
    )
    raw_digest = hashlib.sha256(RAW.read_bytes()).hexdigest()

    return {
        "schema": "itlwm-power-budget-no-producer-quarantine-v2",
        "source_base_revision": "ac1a41603eadfb1ba3cf0e416099807cdc40f73b",
        "reference": {
            "image_sha256": "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
            "image_uuid_x86_64": "149C0AD1-A92F-35BC-AA69-5C8815C5421E",
            "infra_slot": 503,
            "infra_get_wrapper": "0x1000175d8",
            "core_get_vtable_offset": "0x2f8",
            "core_getter": "0x10010712c",
            "core_state_scalar_from_core": "0x48+0x4",
            "infra_set_wrapper": "0x1000187f8",
            "core_set_vtable_offset": "0x4f8",
            "core_setter": "0x100120790",
            "feature_bit": "0x3b",
            "firmware_iovar": "tvpm",
            "firmware_set": "0x10017b6e6",
            "special_commit_status": "0xe3ff8117",
        },
        "local": {
            "power_budget_owner_backend": False,
            "synthetic_success": False,
            "null_return_is_apple_parity": False,
            "valid_input_return_is_apple_parity": False,
            "runtime_selector_invocation": False,
        },
        "checks": {
            "reference_raw_manifest_matches": manifest == f"{raw_digest}  raw.txt\n",
            "reference_raw_has_core_scalar_and_tvpm_context": all(
                token in raw
                for token in (
                    "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
                    "149C0AD1-A92F-35BC-AA69-5C8815C5421E",
                    "0x1000175d8",
                    "0x2f8(%rax)",
                    "0x10010712c",
                    "0x48(%rdi)",
                    "0x4(%rax)",
                    "0x4(%rsi)",
                    "xorl   %eax, %eax",
                    "0x1000187f8",
                    "0x4f8(%rax)",
                    "0x100120790",
                    "$0x3b",
                    "\"tvpm\"",
                    "runIOVarSet",
                    "$0xe3ff8117",
                    "0x4(%rcx)",
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
                    "slot `[503]`",
                    "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
                    "149C0AD1-A92F-35BC-AA69-5C8815C5421E",
                    "`0x1000175d8`",
                    "`0x2f8`",
                    "`0x10010712c`",
                    "(Core + 0x48) + 0x4",
                    "`0x1000187f8`",
                    "`0x4f8`",
                    "`0x100120790`",
                    'runIOVarSet("tvpm")',
                    "not Apple null-input, valid-input return-code, full carrier-layout, version, Core-state, setter, or runtime-selector parity",
                )
            ),
            "active_v2_slot_and_bsd_routes_remain": (
                "// [503]" in hpp
                and "getPOWER_BUDGET" in hpp
                and "Keep this slot fail-closed" in hpp
                and "// [503]" in infra
                and "getPOWER_BUDGET" in infra
                and "fNetIf = new AirportItlwmSkywalkInterface;" in v2
                and all(
                    token in dispatch
                    for token in (
                        "cmd == SIOCGA80211",
                        "getPOWER_BUDGET",
                        "cmd == SIOCSA80211",
                        "setPOWER_BUDGET",
                        "return kIOReturnUnsupported;",
                    )
                )
            ),
            "local_null_guard_is_retained_as_safety_boundary": (
                "if (data == nullptr)" in getter
                and "return kIOReturnBadArgument;" in getter
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
                    "APPLE80211_VERSION",
                    "cachedPowerBudget",
                    "return kIOReturnSuccess;",
                )
            ),
            "dead_power_budget_cache_is_removed": not source_contains("cachedPowerBudget"),
            "setter_boundary_remains_without_consuming_carrier": all(
                token in setter
                for token in (
                    "if (data == nullptr)",
                    "return kIOReturnBadArgumentTahoe;",
                    "((cachedOSFeatureFlags >> 0x3b) & 1ULL) == 0",
                    "if (data->power_budget == 0 || data->power_budget >= 101)",
                    "return kIOReturnUnsupported;",
                )
            )
            and all(
                token not in setter
                for token in (
                    "cachedPowerBudget",
                    "return kIOReturnSuccess;",
                )
            ),
            "no_matching_local_power_budget_producer": all(
                not source_contains(token)
                for token in (
                    'runIOVarSet("tvpm")',
                    "configurePowerBudget",
                    "setPowerBudgetToFirmware",
                )
            ),
            "abi_preserves_declared_local_layout": (
                "must preserve the declared local 8-byte carrier layout" in abi
            ),
            "historical_default_cache_claim_is_superseded": (
                "### 2026-07-15 correction: POWER_BUDGET getter is a no-producer quarantine"
                in signal_audit
                and "`Q13 correction: POWER_BUDGET getter no-producer quarantine`"
                in inventory
                and "Superseded on 2026-07-15" in legacy_note
                and "getter's default-only cache remain outside this narrow setter change"
                not in legacy_note
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
        raise ValueError("POWER_BUDGET no-producer checks failed: " + ", ".join(failed))
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
        print(f"POWER_BUDGET no-producer validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
