#!/usr/bin/env python3
"""Generate and verify legacy V1 BTCOEX_MODE non-null fixed-stub evidence."""

import argparse
import hashlib
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/legacy_btcoex_mode_non_null_fixed_stub_alignment_report.json"
NOTE = ROOT / "docs/reference/CR-518-legacy-btcoex-mode-non-null-fixed-stub-alignment-20260715.md"
RAW = ROOT / "docs/reference/artifacts/legacy-btcoex-mode-public-fixed-stub-bootkc-current/raw.txt"
RAW_MANIFEST = RAW.with_name("SHA256SUMS.txt")
CPP = ROOT / "AirportItlwm/AirportSTAIOCTL.cpp"
HPP = ROOT / "AirportItlwm/AirportItlwm.hpp"
IOCTL = ROOT / "include/Airport/apple80211_ioctl.h"
SKYWALK = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.cpp"
PROJECT = ROOT / "itlwm.xcodeproj/project.pbxproj"


def section(source, begin, end):
    start = source.index(begin)
    return source[start:source.index(end, start)]


def report():
    cpp = CPP.read_text(encoding="utf-8")
    hpp = HPP.read_text(encoding="utf-8")
    ioctl = IOCTL.read_text(encoding="utf-8")
    skywalk = SKYWALK.read_text(encoding="utf-8")
    project = PROJECT.read_text(encoding="utf-8")
    note = " ".join(NOTE.read_text(encoding="utf-8").split())
    raw = RAW.read_text(encoding="utf-8")
    manifest = RAW_MANIFEST.read_text(encoding="utf-8")
    raw_digest = hashlib.sha256(RAW.read_bytes()).hexdigest()

    legacy_dispatch = section(
        cpp,
        "case APPLE80211_IOC_BTCOEX_MODE:",
        "case APPLE80211_IOC_SOFTAP_EXTENDED_CAPABILITIES_IE:",
    )
    legacy_getter = section(
        cpp,
        "getBTCOEX_MODE(OSObject *object, struct apple80211_btc_mode_data *data)",
        "setBTCOEX_MODE(OSObject *object, struct apple80211_btc_mode_data *data)",
    )
    legacy_setter = section(
        cpp,
        "setBTCOEX_MODE(OSObject *object, struct apple80211_btc_mode_data *data)",
        "getBTCOEX_OPTIONS(OSObject *object, struct apple80211_btc_options_data *data)",
    )
    ioct_macro = section(hpp, "#define IOCTL(REQ_TYPE, REQ, DATA_TYPE)", "#define IOCTL_GET")
    carrier = section(
        ioctl,
        "struct apple80211_btc_mode_data",
        "struct apple80211_btc_options_data",
    )
    tahoe_route = section(
        skywalk,
        "case APPLE80211_IOC_BTCOEX_PROFILES:",
        "case APPLE80211_IOC_BGSCAN_CACHE_RESULTS:",
    )
    tahoe_marker = "F8E94CA52B9ABFE20081A3C4 /* Sources */ = {"
    tahoe_start = project.index(tahoe_marker)
    tahoe_phase = project[tahoe_start:project.index("\t\t};", tahoe_start)]

    return {
        "schema": "itlwm-legacy-btcoex-mode-non-null-fixed-stub-alignment-v1",
        "source_base_revision": "4f2a6c0db1e45aed2db17bd1afe000d6283f632e",
        "reference": {
            "bootkc_sha256": "eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d",
            "bootkc_uuid": "F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5",
            "embedded_kext_uuid": "8FB4B7F0-D656-3539-B8D6-C1327A50377C",
            "fixed_status": "0xe082280e",
            "public_wrapper": "0xffffff80021c3fda",
            "next_symbol": "0xffffff80021c3fe5",
            "body_sha256": "9e4580f0175946d7624b2451e6ffda84a93e91e3e2d852d7a2c7998ee2d78576",
        },
        "scope": {
            "deployment": False,
            "radio_or_association": False,
            "runtime_selector_invocation": False,
            "tahoe_source_modified_by_this_layer": False,
            "traffic": False,
        },
        "checks": {
            "reference_raw_manifest_matches": manifest == f"{raw_digest}  raw.txt\n",
            "reference_raw_has_exact_unread_fixed_stub_and_null_boundary": all(
                token in raw
                for token in (
                    "eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d",
                    "F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5",
                    "8FB4B7F0-D656-3539-B8D6-C1327A50377C",
                    "nlist_index=7324",
                    "n_type=0x0f",
                    "n_sect=1",
                    "n_desc=0x0000",
                    "__Z24apple80211setBTCOEX_MODEP23IO80211SkywalkInterfacePv",
                    "symbol_vmaddr=0xffffff80021c3fda",
                    "symbol_vmaddr_end=0xffffff80021c3fe5",
                    "symbol_fileoff=0x20c3fda",
                    "symbol_fileoff_end=0x20c3fe5",
                    "symbol_bytes=0x0b",
                    "next_nlist_index=7169",
                    "__Z21apple80211setWOW_TESTP23IO80211SkywalkInterfaceP24apple80211_wow_test_data",
                    "next_symbol_vmaddr=0xffffff80021c3fe5",
                    "raw=55 48 89 e5 b8 0e 28 82 e0 5d c3",
                    "body_sha256=9e4580f0175946d7624b2451e6ffda84a93e91e3e2d852d7a2c7998ee2d78576",
                    "mov eax,0xe082280e",
                    "unread fixed public stub",
                    "does not establish a null-input contract",
                    "dispatch to a static handler or terminal",
                    "not be relabelled as kIOReturnUnsupported",
                )
            ),
            "note_records_non_null_scope_and_nonclaims": all(
                token in note
                for token in (
                    "IOC 87",
                    "0xffffff80021c3fda",
                    "0xe082280e",
                    "does not claim null-input, carrier layout, ABI, user-client, GET, BTCOEX policy, or Tahoe behavior parity",
                    "does not claim Apple historical behavior",
                    "no deployment, radio, association, APSTA, AWDL, P2P, scan, firmware, event, traffic, CCA, or runtime-execution claim",
                )
            ),
            "legacy_selector_carrier_route_and_null_guard_remain": (
                "#define APPLE80211_IOC_BTCOEX_MODE               87" in ioctl
                and "uint32_t    version;" in carrier
                and "uint32_t    btc_mode;" in carrier
                and "__attribute__((packed))" in carrier
                and "FUNC_IOCTL(BTCOEX_MODE, apple80211_btc_mode_data)" in hpp
                and "if (request_type != SIOCGA80211 && request_type != SIOCSA80211)" in cpp
                and "IOCTL(request_type, BTCOEX_MODE, apple80211_btc_mode_data);" in legacy_dispatch
                and "if (REQ_TYPE == SIOCGA80211)" in ioct_macro
                and "ret = get##REQ" in ioct_macro
                and "ret = set##REQ" in ioct_macro
                and "if (!data)" in legacy_setter
                and "return kIOReturnError;" in legacy_setter
            ),
            "non_null_setter_matches_fixed_status_without_carrier_read_or_cache_write": (
                "(void)object;" in legacy_setter
                and "static_cast<IOReturn>(0xe082280e)" in legacy_setter
                and "return kIOReturnSuccess;" not in legacy_setter
                and "return kIOReturnUnsupported;" not in legacy_setter
                and "data->" not in legacy_setter
                and "object->" not in legacy_setter
                and "btcMode" not in legacy_setter
                and "memcpy" not in legacy_setter
                and "memset" not in legacy_setter
            ),
            "legacy_getter_and_remaining_readback_field_are_separate": (
                "if (!data)" in legacy_getter
                and "data->version = APPLE80211_VERSION;" in legacy_getter
                and "data->btc_mode = btcMode;" in legacy_getter
                and "return kIOReturnSuccess;" in legacy_getter
                and "uint32_t btcMode;" in hpp
                and cpp.count("btcMode") == 1
            ),
            "tahoe_direct_gate_remains_separate": (
                "kApple80211ClassOwnerAbsent =" in skywalk
                and "static_cast<IOReturn>(0xe082280e)" in skywalk
                and "case APPLE80211_IOC_BTCOEX_MODE:" in tahoe_route
                and "return kApple80211ClassOwnerAbsent;" in tahoe_route
            ),
            "legacy_source_is_absent_from_tahoe_phase": (
                "AirportSTAIOCTL.cpp in Sources" not in tahoe_phase
                and "AirportItlwmSkywalkInterface.cpp in Sources" in tahoe_phase
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
    failed = [name for name, passed in value["checks"].items() if not passed]
    if failed:
        raise ValueError("legacy BTCOEX_MODE fixed-stub checks failed: " + ", ".join(failed))
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
        print(f"legacy BTCOEX_MODE fixed-stub validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
