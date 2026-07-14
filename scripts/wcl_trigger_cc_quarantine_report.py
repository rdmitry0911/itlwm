#!/usr/bin/env python3
"""Generate and verify WCL trigger CC quarantine evidence."""

import argparse
import hashlib
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/wcl_trigger_cc_quarantine_report.json"
NOTE = ROOT / "docs/reference/CR-479-wcl-trigger-cc-quarantine-20260714.md"
RAW = ROOT / "docs/reference/artifacts/wcl-trigger-cc-25c56/raw.txt"
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
    getter = section(cpp, "setWCL_TRIGGER_CC(triggerCC *data)", "setOS_FEATURE_FLAGS")
    raw_digest = hashlib.sha256(RAW.read_bytes()).hexdigest()

    return {
        "schema": "itlwm-wcl-trigger-cc-quarantine-v1",
        "source_base_revision": "e45bb13bba708d0714ebf12bbd25e764eda0bcf0",
        "reference": {
            "image_sha256": "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
            "image_uuid_x86_64": "149C0AD1-A92F-35BC-AA69-5C8815C5421E",
            "infra_slot": 599,
            "infra_wrapper": "0x100018c30",
            "core_setter": "0x10013a800",
            "scan_adapter": "0x10018fda2",
            "join_adapter": "0x100040e28",
            "mode_offset": 8,
            "invalid_mode_status": "0xe00002bc",
        },
        "local": {
            "false_success": False,
            "null_return_is_apple_parity": False,
            "valid_mode_return_is_apple_parity": False,
            "runtime_selector_invocation": False,
        },
        "checks": {
            "reference_raw_manifest_matches": manifest == f"{raw_digest}  raw.txt\n",
            "reference_raw_has_adapter_pipeline": all(
                token in raw
                for token in (
                    "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
                    "149C0AD1-A92F-35BC-AA69-5C8815C5421E",
                    "0x100018c30",
                    "0x10013a800",
                    "0x8(%rsi)",
                    "0x10018fda2",
                    "0x100040e28",
                    "0xe00002bc",
                    "CCFaultReporter::reportFault",
                    "dumpEventBitField",
                    "collectJoinTimeoutAwdMetrics",
                    "collectCCAForJoinTimeout",
                )
            ),
            "reference_note_has_scope_and_nonclaim": all(
                token in note
                for token in (
                    "slot `[599]`",
                    "0x100018c30",
                    "0x10013a800",
                    "0x10018fda2",
                    "0x100040e28",
                    "not Apple null, valid-mode return-code,\ninput-size, or adapter-side parity",
                )
            ),
            "active_v2_slot_remains": (
                "// [599]" in hpp
                and "setWCL_TRIGGER_CC" in hpp
                and "// [599]" in infra
                and "setWCL_TRIGGER_CC" in infra
                and "fNetIf = new AirportItlwmSkywalkInterface;" in v2
            ),
            "local_null_and_invalid_boundaries_remain": (
                "if (!data)" in getter
                and "return kIOReturnBadArgument;" in getter
                and "mode != 0 && mode != 1" in getter
                and "return kIOReturnBadArgumentTahoe;" in getter
            ),
            "valid_modes_fail_closed_without_cache": (
                "return kIOReturnUnsupported;" in getter
                and "kIOReturnSuccess" not in getter
                and "cachedTriggerCC" not in getter
                and "triggerCCSnapshot" not in getter
                and "memcpy" not in getter
            ),
            "dead_cache_and_adapter_work_are_absent": (
                "cachedTriggerCC" not in cpp
                and "cachedTriggerCC" not in hpp
                and "triggerCCSnapshot" not in cpp
                and "CCFaultReporter" not in getter
                and "collectJoinTimeoutAwdMetrics" not in getter
                and "dumpEventBitField" not in getter
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
        raise ValueError("WCL trigger CC quarantine checks failed: " + ", ".join(failed))
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
        print(f"WCL trigger CC quarantine validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
