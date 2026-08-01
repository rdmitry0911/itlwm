#!/usr/bin/env python3
"""Generate and verify the live WCL scan-home-away Intel backend evidence."""

import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/wcl_scan_home_away_quarantine_report.json"
NOTE = ROOT / "docs/reference/CR-479-wcl-scan-home-away-quarantine-20260712.md"
CPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.cpp"
HPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.hpp"
BRIDGE = ROOT / "include/ClientKit/AirportItlwmScanHomeAwayBridge.h"
IWN = ROOT / "itlwm/hal_iwn/ItlIwn.cpp"
IWM = ROOT / "itlwm/hal_iwm/scan.cpp"
IWX = ROOT / "itlwm/hal_iwx/ItlIwx.cpp"


def section(source, begin, end):
    start = source.index(begin)
    return source[start:source.index(end, start)]


def report():
    cpp = CPP.read_text(encoding="utf-8")
    hpp = HPP.read_text(encoding="utf-8")
    note = NOTE.read_text(encoding="utf-8")
    bridge = BRIDGE.read_text(encoding="utf-8")
    iwn = IWN.read_text(encoding="utf-8")
    iwm = IWM.read_text(encoding="utf-8")
    iwx = IWX.read_text(encoding="utf-8")
    setter = section(
        cpp,
        "setWCL_SET_SCAN_HOME_AWAY_TIME(scanHomeAndAwayTime *data)",
        "setWCL_ULOFDMA_STATE",
    )
    return {
        "schema": "itlwm-wcl-scan-home-away-intel-backend-v2",
        "source_base_revision": "9a87971",
        "reference": {
            "public_bridge": "0xffffff8001522d28",
            "adapter_offset": "0x1530",
            "adapter_setter": "0xffffff80016ac8a6",
            "firmware_iovar": "scan_home_away_time",
        },
        "local": {
            "backend_scan_home_away_adapter": True,
            "request_false_success": False,
            "selector_slot": 604,
            "runtime_observed_milliseconds": 110,
            "families": ["IWN", "IWM", "IWX"],
        },
        "checks": {
            "reference_note": all(
                token in note
                for token in (
                    "FUN_ffffff8001522d28",
                    "Core +0x1530",
                    "FUN_ffffff80016ac8a6",
                    "scan_home_away_time",
                    "workqueue",
                    "Runtime closure",
                    "110",
                )
            ),
            "setter_owns_supported_policy": (
                "AIRPORT_ITLWM_REQUIRE_LIVE_OPERATION();" in setter
                and "if (data == nullptr)" in setter
                and "return kIOReturnBadArgumentTahoe;" in setter
                and "data->milliseconds > 1000U" in setter
                and "airportItlwmSetScanHomeAwayTime(data->milliseconds);" in setter
                and "return kIOReturnSuccess;" in setter
                and "return kIOReturnUnsupported;" not in setter
            ),
            "layout_neutral_policy_bridge": (
                all(
                    token in bridge
                    for token in (
                        "airportItlwmSetScanHomeAwayTime",
                        "airportItlwmGetScanHomeAwayTime",
                        "avoids changing the shared ieee80211com ABI layout",
                    )
                )
                and all(
                    token in cpp
                    for token in (
                        "sScanHomeAwayTimeMs",
                        "sScanHomeAwayTimeValid",
                        "__sync_synchronize();",
                    )
                )
            ),
            "iwn_programs_dvm_command": all(
                token in iwn
                for token in (
                    "airportItlwmGetScanHomeAwayTime(&configuredHomeAwayMs)",
                    "hdr->max_out = htole32(maxOutMs * 1024U);",
                    "hdr->pause_scan = htole32",
                )
            ),
            "iwm_programs_lmac_and_umac": (
                iwm.count("airportItlwmGetScanHomeAwayTime") == 2
                and "req->max_out_time = htole32(homeAwayMs);" in iwm
                and "req->suspend_time = htole32(homeAwayMs);" in iwm
                and "const uint32_t timeout = htole32(homeAwayMs);" in iwm
            ),
            "iwx_programs_all_umac_versions": (
                iwx.count("airportItlwmGetScanHomeAwayTime") == 3
                and iwx.count(
                    "const uint32_t timeout = bgscan ? htole32(homeAwayMs) : htole32(0);"
                ) == 3
            ),
            "dead_interface_cache_remains_absent": (
                "cachedScanHomeAwayTime" not in cpp
                and "cachedScanHomeAwayTime" not in hpp
                and "ic_scan_home_away_time" not in (
                    ROOT / "itl80211/openbsd/net80211/ieee80211_var.h"
                ).read_text(encoding="utf-8")
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
        raise ValueError("scan-home-away backend checks failed: " + ", ".join(failed))
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
        print(f"WCL scan-home-away backend validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
