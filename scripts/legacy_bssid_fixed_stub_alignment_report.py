#!/usr/bin/env python3
"""Generate and verify legacy V1 BSSID fixed-stub alignment evidence."""

import argparse
import hashlib
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/legacy_bssid_fixed_stub_alignment_report.json"
NOTE = ROOT / "docs/reference/CR-516-legacy-bssid-fixed-stub-alignment-20260715.md"
RAW = ROOT / "docs/reference/artifacts/legacy-bssid-public-fixed-stub-bootkc-current/raw.txt"
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
        "case APPLE80211_IOC_BSSID:  // 9",
        "case APPLE80211_IOC_SCAN_REQ:  // 10",
    )
    legacy_getter = section(
        cpp,
        "getBSSID(OSObject *object,\n                         struct apple80211_bssid_data *bd)",
        "setBSSID(OSObject *object, struct apple80211_bssid_data *data)",
    )
    legacy_setter = section(
        cpp,
        "setBSSID(OSObject *object, struct apple80211_bssid_data *data)",
        "getCARD_CAPABILITIES(OSObject *object,",
    )
    ioct_macro = section(hpp, "#define IOCTL(REQ_TYPE, REQ, DATA_TYPE)", "#define IOCTL_GET")
    carrier = section(
        ioctl,
        "struct apple80211_bssid_data",
        "#if __IO80211_TARGET >= __MAC_26_0",
    )
    tahoe_route = section(
        skywalk,
        "case APPLE80211_IOC_BSSID:",
        "case APPLE80211_IOC_SCAN_RESULT:",
    )
    tahoe_marker = "F8E94CA52B9ABFE20081A3C4 /* Sources */ = {"
    tahoe_start = project.index(tahoe_marker)
    tahoe_phase = project[tahoe_start:project.index("\t\t};", tahoe_start)]

    return {
        "schema": "itlwm-legacy-bssid-fixed-stub-alignment-v1",
        "source_base_revision": "bf52fec42157c1a04ac1eb4ec3a0de511d16bb96",
        "reference": {
            "bootkc_sha256": "eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d",
            "bootkc_uuid": "F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5",
            "embedded_kext_uuid": "8FB4B7F0-D656-3539-B8D6-C1327A50377C",
            "fixed_status": "0xe082280e",
            "public_wrapper": "0xffffff80021c372e",
            "next_symbol": "0xffffff80021c3739",
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
            "reference_raw_has_exact_unread_fixed_stub": all(
                token in raw
                for token in (
                    "eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d",
                    "F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5",
                    "8FB4B7F0-D656-3539-B8D6-C1327A50377C",
                    "nlist_index=7057",
                    "n_type=0x0f",
                    "n_sect=1",
                    "n_desc=0x0000",
                    "__Z18apple80211setBSSIDP23IO80211SkywalkInterfaceP21apple80211_bssid_data",
                    "symbol_vmaddr=0xffffff80021c372e",
                    "symbol_vmaddr_end=0xffffff80021c3739",
                    "symbol_fileoff=0x20c372e",
                    "symbol_fileoff_end=0x20c3739",
                    "symbol_bytes=0x0b",
                    "next_nlist_index=7167",
                    "__Z21apple80211setSCAN_REQP23IO80211SkywalkInterfaceP20apple80211_scan_data",
                    "next_symbol_vmaddr=0xffffff80021c3739",
                    "raw=55 48 89 e5 b8 0e 28 82 e0 5d c3",
                    "body_sha256=9e4580f0175946d7624b2451e6ffda84a93e91e3e2d852d7a2c7998ee2d78576",
                    "mov eax,0xe082280e",
                    "unread fixed public stub",
                    "selector load, gate, metacast, dynamic tail, owner lookup, call, state,",
                    "not be relabelled as kIOReturnUnsupported",
                )
            ),
            "note_records_scope_and_nonclaims": all(
                token in note
                for token in (
                    "IOC 9",
                    "0xffffff80021c372e",
                    "0xe082280e",
                    "does not claim carrier, null-input, ABI, user-client, GET, or Tahoe behavior parity",
                    "does not claim Apple historical behavior",
                    "no deployment, radio, association, APSTA, AWDL, P2P, scan, firmware, event, traffic, CCA, or runtime-execution claim",
                )
            ),
            "legacy_selector_carrier_and_route_remain": (
                "#define APPLE80211_IOC_BSSID                     9" in ioctl
                and "u_int32_t            version;" in carrier
                and "struct ether_addr    bssid;" in carrier
                and "FUNC_IOCTL(BSSID, apple80211_bssid_data)" in hpp
                and "if (request_type != SIOCGA80211 && request_type != SIOCSA80211)" in cpp
                and "IOCTL(request_type, BSSID, apple80211_bssid_data);" in legacy_dispatch
                and "if (REQ_TYPE == SIOCGA80211)" in ioct_macro
                and "ret = get##REQ" in ioct_macro
                and "ret = set##REQ" in ioct_macro
            ),
            "legacy_setter_matches_exact_fixed_status_without_reads": (
                "(void)object;" in legacy_setter
                and "(void)data;" in legacy_setter
                and "static_cast<IOReturn>(0xe082280e)" in legacy_setter
                and "return kIOReturnSuccess;" not in legacy_setter
                and "return kIOReturnUnsupported;" not in legacy_setter
                and "data->" not in legacy_setter
                and "object->" not in legacy_setter
                and "fHalService" not in legacy_setter
                and "memcpy" not in legacy_setter
                and "memset" not in legacy_setter
            ),
            "legacy_getter_is_preserved_separately": (
                "fHalService->get80211Controller();" in legacy_getter
                and "memset(bd, 0, sizeof(*bd));" in legacy_getter
                and "bd->version = APPLE80211_VERSION;" in legacy_getter
                and "ic->ic_state == IEEE80211_S_RUN" in legacy_getter
                and "memcpy(bd->bssid.octet, ic->ic_bss->ni_bssid, APPLE80211_ADDR_LEN);" in legacy_getter
                and "return kIOReturnSuccess;" in legacy_getter
            ),
            "tahoe_get_and_set_rejection_remain_separate": (
                "if (cmd == SIOCGA80211)" in tahoe_route
                and "getTahoeCompactBSSID" in tahoe_route
                and "return getBSSID((apple80211_bssid_data *)req->req_data);" in tahoe_route
                and "return kIOReturnUnsupported;" in tahoe_route
                and "setBSSID(" not in tahoe_route
            ),
            "legacy_source_is_absent_from_tahoe_phase": (
                "AirportSTAIOCTL.cpp in Sources" not in tahoe_phase
                and "AirportItlwmV2.cpp in Sources" in tahoe_phase
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
        raise ValueError("legacy BSSID fixed-stub checks failed: " + ", ".join(failed))
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
        print(f"legacy BSSID fixed-stub validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
