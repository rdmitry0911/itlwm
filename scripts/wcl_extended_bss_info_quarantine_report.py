#!/usr/bin/env python3
"""Generate and verify WCL extended BSS info quarantine evidence."""

import argparse
import hashlib
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/wcl_extended_bss_info_quarantine_report.json"
NOTE = ROOT / "docs/reference/CR-479-wcl-extended-bss-info-quarantine-20260714.md"
RAW = ROOT / "docs/reference/artifacts/wcl-extended-bss-info-25c56/raw.txt"
RAW_MANIFEST = RAW.with_name("SHA256SUMS.txt")
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
    raw = RAW.read_text(encoding="utf-8")
    manifest = RAW_MANIFEST.read_text(encoding="utf-8")
    getter = section(cpp, "getWCL_EXTENDED_BSS_INFO", "getTXPOWER")
    raw_digest = hashlib.sha256(RAW.read_bytes()).hexdigest()

    return {
        "schema": "itlwm-wcl-extended-bss-info-quarantine-v1",
        "source_base_revision": "ea0a9e655fabf32729cc6e5de71a528d777d6f8b",
        "reference": {
            "image_sha256": "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
            "image_uuid_x86_64": "149C0AD1-A92F-35BC-AA69-5C8815C5421E",
            "infra_slot": 533,
            "infra_wrapper": "0x100017c58",
            "core_getter": "0x100132df6",
            "net_adapter_getter": "0x10019de64",
            "null_status": "0xe00002bc",
        },
        "local": {
            "false_success": False,
            "valid_input_return_is_apple_parity": False,
            "runtime_selector_invocation": False,
        },
        "checks": {
            "reference_raw_manifest_matches": manifest == f"{raw_digest}  raw.txt\n",
            "reference_raw_has_net_adapter_pipeline": all(
                token in raw
                for token in (
                    "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
                    "149C0AD1-A92F-35BC-AA69-5C8815C5421E",
                    "0x100017c58",
                    "0x420(%rax)",
                    "0x100132df6",
                    "0xe00002bc",
                    "0x15e0",
                    "0x10019de64",
                    "updateRateSetSync",
                    "0xbc(%rbx)",
                    "updateMCSSetSyc",
                    "0xcc(%rbx)",
                    "0xd4(%rbx)",
                    "getAssociatedWPARSNIESync",
                    "0x113(%rbx)",
                    "$0x101",
                    "$0x73",
                    "getMloContext",
                    "0xdc",
                )
            ),
            "reference_note_has_scope_and_nonclaim": all(
                token in note
                for token in (
                    "slot `[533]`",
                    "0x100017c58",
                    "0x100132df6",
                    "0x10019de64",
                    "not Apple valid-input return-code or\noutput-layout parity",
                )
            ),
            "active_v2_slot_remains": (
                "// [533]" in hpp
                and "getWCL_EXTENDED_BSS_INFO" in hpp
                and "// [533]" in infra
                and "getWCL_EXTENDED_BSS_INFO" in infra
                and "fNetIf = new AirportItlwmSkywalkInterface;" in v2
            ),
            "local_null_guard_is_retained": (
                "if (data == nullptr)" in getter
                and "return kIOReturnBadArgumentTahoe;" in getter
            ),
            "nonnull_path_fails_closed_without_output": (
                "(void)data;" in getter
                and "return kIOReturnUnsupported;" in getter
                and "kIOReturnSuccess" not in getter
                and "memset" not in getter
                and "memcpy" not in getter
            ),
            "no_local_net_adapter_pipeline_is_introduced": all(
                token not in getter
                for token in (
                    "updateRateSetSync",
                    "updateMCSSetSyc",
                    "getAssociatedWPARSNIESync",
                    "getMloContext",
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
        raise ValueError("WCL extended BSS info quarantine checks failed: " + ", ".join(failed))
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
        print(f"WCL extended BSS info quarantine validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
