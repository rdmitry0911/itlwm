#!/usr/bin/env python3
"""Generate and verify LMTPC configuration false-success evidence."""

import argparse
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/lmtpc_config_quarantine_report.json"
NOTE = ROOT / "docs/reference/CR-479-lmtpc-config-quarantine-20260713.md"
CPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.cpp"
HPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.hpp"
COMMANDER = ROOT / "AirportItlwm/TahoeCommanderV2.hpp"
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
    commander = COMMANDER.read_text(encoding="utf-8")
    note = NOTE.read_text(encoding="utf-8")
    setter = section(cpp, "setLMTPC_CONFIG", "setLE_SCAN_PARAM")
    transport = section(commander, "IOReturn dispatchTransport", "IOReturn dispatchIOVarSet")

    return {
        "schema": "itlwm-lmtpc-config-quarantine-v1",
        "source_base_revision": "3f053b5213f023887376c835ed62f77065cddddb",
        "reference": {
            "image_sha256": "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
            "core_setter": "0x100142c22",
            "owner": "0x1000fe4c0",
            "firmware_iovar": "lpc",
            "firmware_gate": "0x1123",
        },
        "local": {
            "backend_lmtpc_owner": False,
            "false_success": False,
            "valid_input_return_is_apple_parity": False,
        },
        "checks": {
            "reference_note": all(
                token in note
                for token in (
                    "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
                    "0x100142c22",
                    "0x1000fe4c0",
                    "lpc",
                    "0x1123",
                    "not Apple valid-input return-code parity",
                )
            ),
            "setter_quarantines_nonnull": (
                "if (data == nullptr)" in setter
                and "return kIOReturnBadArgumentTahoe;" in setter
                and "return kIOReturnUnsupported;" in setter
                and "return kIOReturnSuccess;" not in setter
                and "cachedLmtpcValue" not in setter
                and "tahoeLmtpcConfig" not in setter
            ),
            "pseudo_state_removed": (
                "cachedLmtpcValue" not in cpp
                and "cachedLmtpcValue" not in hpp
                and "tahoeLmtpcConfig" not in cpp
            ),
            "no_local_lmtpc_backend": (
                not source_contains("runSetLMTPC")
                and not source_contains('"lpc"')
            ),
            "generic_commander_is_not_hardware": (
                "TahoeCompletion::complete(asyncContext, 0);" in transport
                and "return kIOReturnSuccess;" in transport
                and "fHalService" not in transport
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
        raise ValueError("LMTPC configuration quarantine checks failed: " + ", ".join(failed))
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
        print(f"LMTPC configuration quarantine validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
