#!/usr/bin/env python3
"""Generate and verify IBSS mode quarantine evidence."""

import argparse
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/ibss_mode_quarantine_report.json"
NOTE = ROOT / "docs/reference/CR-479-ibss-mode-quarantine-20260713.md"
BSS_NOTE = ROOT / "docs/reference/CR-479-bssmanager-ad-hoc-created-teardown-20260708.md"
CAPABILITY_NOTE = ROOT / "docs/reference/ITLWM_APGO_BACKEND_CAPABILITY_CENSUS_2026_05_09.md"
CPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.cpp"
HPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.hpp"
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
    note = NOTE.read_text(encoding="utf-8")
    bss_note = BSS_NOTE.read_text(encoding="utf-8")
    capability_note = CAPABILITY_NOTE.read_text(encoding="utf-8")
    setter = section(
        cpp,
        "IOReturn AirportItlwmSkywalkInterface::\nsetIBSS_MODE",
        "IOReturn AirportItlwmSkywalkInterface::\nsetIE",
    )

    return {
        "schema": "itlwm-ibss-mode-quarantine-v1",
        "source_base_revision": "b7196beac22e540694abb1c0d402e2759a07e34d",
        "reference": {
            "image_sha256": "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
            "apple80211_ioc": 24,
            "infra_wrapper": "0x10001814c",
            "core_setter": "0x10011c94c",
            "adhoc_creator": "0x10003d7ea",
            "positive_bssmanager_edge_requires_creator": True,
        },
        "local": {
            "backend_ibss_creator": False,
            "false_success": False,
            "valid_input_return_is_apple_parity": False,
        },
        "checks": {
            "reference_note": all(
                token in note
                for token in (
                    "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
                    "0x10001814c",
                    "0x10011c94c",
                    "createAdhocNetwork",
                    "0x10003d7ea",
                    "Proximity, NAN, and NAN-data",
                    "not Apple valid-input return-code parity",
                )
            )
            and all(
                token in bss_note
                for token in (
                    "setAdHocCreated(true)",
                    "createAdhocNetwork",
                    "setIBSS_MODE(...)",
                    "does not create an IBSS",
                )
            )
            and all(
                token in capability_note
                for token in (
                    "IEEE80211_STA_ONLY",
                    "ieee80211_create_ibss",
                )
            ),
            "ioctl_route_present": (
                "APPLE80211_IOC_IBSS_MODE" in cpp
                and "(cmd == SIOCSA80211) ? setIBSS_MODE" in cpp
            ),
            "setter_quarantines_nonnull": (
                "if (data == nullptr)" in setter
                and "return kIOReturnBadArgumentTahoe;" in setter
                and "return kIOReturnUnsupported;" in setter
                and "return kIOReturnSuccess;" not in setter
                and "cachedIbss" not in setter
            ),
            "pseudo_state_removed": all(
                token not in cpp and token not in hpp
                for token in (
                    "cachedIbssMode",
                    "cachedIbssAuthLower",
                    "cachedIbssAuthUpper",
                    "cachedIbssChannel",
                    "cachedIbssSsidLen",
                    "cachedIbssSsid",
                    "hasCachedIbssNetwork",
                )
            ),
            "no_local_ibss_creator": not source_contains("createAdhocNetwork"),
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
        raise ValueError("IBSS mode quarantine checks failed: " + ", ".join(failed))
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
        print(f"IBSS mode quarantine validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
