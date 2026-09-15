#!/usr/bin/env python3
"""Generate and verify THERMAL_INDEX nominal-identity прослойка evidence.

Supersedes the CR-491/CR-479 no-producer quarantine. The reference sources the
thermal index from TVPM (Broadcom thermal/voltage/power-management) Core state
whose OWN default after AppleBCMWLANCore::resetTVPMIndicies is index 100 (full
budget / no throttle). Intel is not TVPM-throttling, so 100 is both Intel's
nominal state and the reference default output -- a functionally-equivalent
contact-surface identity. Returning kIOReturnUnsupported where the reference
returns a value would itself be a non-identity, so this slot now emits 100.
"""

import argparse
import hashlib
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/thermal_index_rejected_state_report.json"
NOTE = ROOT / "docs/reference/CR-622-thermal-index-nominal-identity-20260915.md"
PREDECESSOR_NOTE = ROOT / "docs/reference/CR-491-thermal-index-no-producer-quarantine-20260715.md"
RAW = ROOT / "docs/reference/artifacts/thermal-index-25c56/raw.txt"
RAW_MANIFEST = RAW.with_name("SHA256SUMS.txt")
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
    raw = RAW.read_text(encoding="utf-8")
    manifest = RAW_MANIFEST.read_text(encoding="utf-8")
    getter = section(
        cpp,
        "getTHERMAL_INDEX(apple80211_thermal_index_t *data)",
        "getPOWER_BUDGET(apple80211_power_budget_t *data)",
    )
    setter = section(
        cpp,
        "setTHERMAL_INDEX(apple80211_thermal_index_t *data)",
        "setDYNAMIC_RSSI_WINDOW_CONFIG",
    )
    dispatch = section(
        cpp,
        "case APPLE80211_IOC_THERMAL_INDEX:",
        "case APPLE80211_IOC_POWER_BUDGET:",
    )
    core_get_raw = section(
        raw,
        "(lldb) disassemble -b -s 0x100106eda",
        "(lldb) disassemble -b -s 0x100018760",
    )
    raw_digest = hashlib.sha256(RAW.read_bytes()).hexdigest()

    return {
        "schema": "itlwm-thermal-index-nominal-identity-v3",
        "supersedes": ["CR-491", "CR-479"],
        "reference": {
            "image_sha256": "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
            "image_uuid_x86_64": "149C0AD1-A92F-35BC-AA69-5C8815C5421E",
            "infra_slot": 500,
            "infra_get_wrapper": "0x1000174f4",
            "core_get_vtable_offset": "0x2e8",
            "core_getter": "0x100106eda",
            "core_state_scalar_from_core": "0x48+0x0",
            "infra_set_wrapper": "0x100018760",
            "core_set_vtable_offset": "0x4f0",
            "core_setter": "0x100120586",
            "feature_bit": "0x3b",
            "firmware_iovar": "tvpm",
            "firmware_set": "0x10017b6e6",
            "special_commit_status": "0xe3ff8117",
            "reset_symbol": "AppleBCMWLANCore::resetTVPMIndicies",
            "tvpm_reset_default_index": 100,
            "tvpm_index_full_budget": 100,
        },
        "local": {
            "thermal_tvpm_backend": False,
            "emits_reference_nominal_index": True,
            "nominal_index": 100,
            "fail_closed": False,
            "null_guard_retained": True,
            "prosloyka_identity": True,
        },
        "checks": {
            "reference_raw_manifest_matches": manifest == f"{raw_digest}  raw.txt\n",
            "reference_raw_has_core_scalar_and_tvpm_path": all(
                token in raw
                for token in (
                    "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
                    "149C0AD1-A92F-35BC-AA69-5C8815C5421E",
                    "0x1000174f4",
                    "0x2e8(%rax)",
                    "0x100106eda",
                    "0x48(%rdi)",
                    "movl   (%rax), %eax",
                    "0x4(%rsi)",
                    "xorl   %eax, %eax",
                    "0x100018760",
                    "0x4f0(%rax)",
                    "$0x3b",
                    "\"tvpm\"",
                    "runIOVarSet",
                    "$0xe3ff8117",
                    "movl   %eax, (%rcx)",
                )
            ),
            "reference_getter_has_no_observed_null_guard": (
                "testq  %rsi" not in core_get_raw
                and "testl  %esi" not in core_get_raw
                and "0x4(%rsi)" in core_get_raw
            ),
            "getter_emits_reference_nominal_index": all(
                token in getter
                for token in (
                    "if (data == nullptr)",
                    "return kIOReturnBadArgument;",
                    "memset(data, 0, sizeof(*data));",
                    "data->version = APPLE80211_VERSION;",
                    "data->thermal_index = 100;",
                    "return kIOReturnSuccess;",
                )
            ),
            "getter_no_longer_fails_closed": (
                "return kIOReturnUnsupported;" not in getter
                and "(void)data;" not in getter
            ),
            "active_v2_slot_and_get_dispatch_remain": (
                "// [500]" in hpp
                and "getTHERMAL_INDEX" in hpp
                and "// [500]" in infra
                and "getTHERMAL_INDEX" in infra
                and "fNetIf = new AirportItlwmSkywalkInterface;" in v2
                and "getTHERMAL_INDEX" in dispatch
                and "cmd == SIOCGA80211" in dispatch
            ),
            "setter_boundary_remains_fail_closed": (
                "(void)data;" in setter
                and "return kIOReturnBadArgumentTahoe;" in setter
                and all(
                    token not in setter
                    for token in (
                        "data->",
                        "cachedThermalIndex",
                        "return kIOReturnSuccess;",
                    )
                )
            ),
            "no_matching_local_tvpm_producer": all(
                not source_contains(token)
                for token in (
                    'runIOVarSet("tvpm")',
                    "setThermalIndexToFirmware",
                    "configureThermalIndex",
                    "cachedThermalIndex",
                )
            ),
            "abi_preserves_declared_local_layout": (
                "must preserve the declared local 8-byte carrier layout" in abi
            ),
            "superseding_note_cites_prosloyka_and_reset_default": all(
                token in note
                for token in (
                    "slot `[500]`",
                    "прослойка",
                    "resetTVPMIndicies",
                    "full budget",
                    "functionally equivalent",
                    "100",
                    "supersedes CR-491",
                )
            ),
            "predecessor_note_exists": PREDECESSOR_NOTE.exists(),
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
        raise ValueError("THERMAL_INDEX nominal-identity checks failed: " + ", ".join(failed))
    rendered = json.dumps(value, indent=2, sort_keys=True, ensure_ascii=False) + "\n"
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
        print(f"THERMAL_INDEX nominal-identity validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
