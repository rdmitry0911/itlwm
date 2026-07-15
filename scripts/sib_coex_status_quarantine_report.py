#!/usr/bin/env python3
"""Generate and verify SIB coex status quarantine evidence."""

import argparse
import hashlib
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/sib_coex_status_quarantine_report.json"
NOTE = ROOT / "docs/reference/CR-479-sib-coex-status-quarantine-20260715.md"
RAW = ROOT / "docs/reference/artifacts/sib-coex-status-25c56/raw.txt"
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
    getter = section(
        cpp,
        "getSIB_COEX_STATUS(apple80211_sib_coex_status *data)",
        "getWCL_LOW_LATENCY_INFO_STATS",
    )
    raw_digest = hashlib.sha256(RAW.read_bytes()).hexdigest()

    return {
        "schema": "itlwm-sib-coex-status-quarantine-v1",
        "source_base_revision": "66c5e46757f8d0e7c4e49cbbd5079e8878ed2779",
        "reference": {
            "image_sha256": "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
            "image_uuid_x86_64": "149C0AD1-A92F-35BC-AA69-5C8815C5421E",
            "infra_slot": 532,
            "infra_wrapper": "0x100017b28",
            "core_getter": "0x100119a7a",
            "core_vtable_offset": "0x458",
            "core_state_dword_0": "0x8b10",
            "core_state_dword_1": "0x8b14",
            "null_status": "0xe00002c2",
        },
        "local": {
            "false_success": False,
            "null_return_is_apple_parity": False,
            "valid_input_return_is_apple_parity": False,
            "runtime_selector_invocation": False,
        },
        "checks": {
            "reference_raw_manifest_matches": manifest == f"{raw_digest}  raw.txt\n",
            "reference_raw_has_live_core_snapshot": all(
                token in raw
                for token in (
                    "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
                    "149C0AD1-A92F-35BC-AA69-5C8815C5421E",
                    "0x100017b28",
                    "0x458(%rax)",
                    "0x100119a7a",
                    "0x8b10(%rax)",
                    "movl   %eax, (%rsi)",
                    "0x8b14(%rax)",
                    "movl   %eax, 0x4(%rsi)",
                    "$0xe00002c2",
                    "getting sib coex mode %d , timeToTST %d",
                )
            ),
            "reference_note_has_scope_and_nonclaim": all(
                token in note
                for token in (
                    "slot `[532]`",
                    "0x100017b28",
                    "0x100119a7a",
                    "0x8b10",
                    "0x8b14",
                    "not Apple null, valid-input return-code,\nstruct-layout, Core-state, BTCOEX-equivalence, or runtime-selector parity",
                )
            ),
            "active_v2_slot_remains": (
                "// [532]" in hpp
                and "getSIB_COEX_STATUS" in hpp
                and "// [532]" in infra
                and "getSIB_COEX_STATUS" in infra
                and "fNetIf = new AirportItlwmSkywalkInterface;" in v2
            ),
            "local_null_guard_is_retained": (
                "if (data == nullptr)" in getter
                and "return static_cast<IOReturn>(kApple80211ErrInvalidArgumentRaw);" in getter
            ),
            "nonnull_path_fails_closed_without_synthetic_snapshot": (
                "(void)data;" in getter
                and "return kIOReturnUnsupported;" in getter
                and "kIOReturnSuccess" not in getter
                and "memset" not in getter
                and "APPLE80211_VERSION" not in getter
                and "reinterpret_cast" not in getter
            ),
            "no_local_core_or_btcoex_substitution_is_introduced": all(
                token not in getter
                for token in (
                    "btcMode",
                    "btcOptions",
                    "instance->",
                    "0x8b10",
                    "0x8b14",
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
        raise ValueError("SIB coex status quarantine checks failed: " + ", ".join(failed))
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
        print(f"SIB coex status quarantine validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
