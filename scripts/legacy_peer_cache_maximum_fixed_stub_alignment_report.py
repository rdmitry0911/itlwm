#!/usr/bin/env python3
"""Generate and verify legacy peer-cache fixed-stub alignment evidence."""

import argparse
import hashlib
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/legacy_peer_cache_maximum_fixed_stub_alignment_report.json"
NOTE = ROOT / "docs/reference/CR-503-legacy-peer-cache-maximum-fixed-stub-alignment-20260715.md"
RAW = ROOT / "docs/reference/artifacts/peer-cache-maximum-public-fixed-stub-bootkc-current/raw.txt"
RAW_MANIFEST = RAW.with_name("SHA256SUMS.txt")
CPP = ROOT / "AirportItlwm/AirportVirtualIOCTL.cpp"
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
        "case APPLE80211_IOC_PEER_CACHE_MAXIMUM_SIZE:",
        "case APPLE80211_IOC_AWDL_ELECTION_ID:",
    )
    legacy_getter = section(
        cpp,
        "getPEER_CACHE_MAXIMUM_SIZE(OSObject *object, struct apple80211_peer_cache_maximum_size *data)",
        "setPEER_CACHE_MAXIMUM_SIZE(OSObject *object, struct apple80211_peer_cache_maximum_size *data)",
    )
    legacy_setter = section(
        cpp,
        "setPEER_CACHE_MAXIMUM_SIZE(OSObject *object, struct apple80211_peer_cache_maximum_size *data)",
        "getAWDL_MASTER_CHANNEL(OSObject *object, struct apple80211_awdl_master_channel *data)",
    )
    virtual_request = section(
        cpp,
        "apple80211VirtualRequest(UInt request_type, int request_number,",
        "setAWDL_PEER_TRAFFIC_REGISTRATION(OSObject *object,",
    )
    ioct_macro = section(hpp, "#define IOCTL(REQ_TYPE, REQ, DATA_TYPE)", "#define IOCTL_GET")
    carrier = section(
        ioctl,
        "struct apple80211_peer_cache_maximum_size",
        "struct apple80211_awdl_election_id",
    )
    tahoe_route = section(
        skywalk,
        "case APPLE80211_IOC_PEER_CACHE_MAXIMUM_SIZE:",
        "case APPLE80211_IOC_CUR_PMK:",
    )
    tahoe_marker = "F8E94CA52B9ABFE20081A3C4 /* Sources */ = {"
    tahoe_start = project.index(tahoe_marker)
    tahoe_phase = project[tahoe_start:project.index("\t\t};", tahoe_start)]

    return {
        "schema": "itlwm-legacy-peer-cache-maximum-fixed-stub-alignment-v1",
        "source_base_revision": "5c7c3bb448b120ec7ab0a65575e9059a7d09f447",
        "reference": {
            "bootkc_sha256": "eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d",
            "bootkc_uuid": "F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5",
            "embedded_kext_uuid": "8FB4B7F0-D656-3539-B8D6-C1327A50377C",
            "fixed_status": "0xe082280e",
            "public_wrapper": "0xffffff80021c4575",
            "next_symbol": "0xffffff80021c4580",
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
                    "symbol_vmaddr=0xffffff80021c4575",
                    "next_symbol_vmaddr=0xffffff80021c4580",
                    "raw=55 48 89 e5 b8 0e 28 82 e0 5d c3",
                    "reads neither public argument",
                    "no gate, owner lookup, call, state",
                    "not be relabelled as kIOReturnUnsupported",
                )
            ),
            "note_records_scope_and_nonclaims": all(
                token in note
                for token in (
                    "IOC 130",
                    "0xffffff80021c4575",
                    "0xe082280e",
                    "not claim Apple historical behavior",
                    "no deployment, radio, association, APSTA, AWDL, P2P, scan, firmware, event, traffic, or runtime-execution claim",
                )
            ),
            "legacy_selector_carrier_and_route_remain": (
                "#define APPLE80211_IOC_PEER_CACHE_MAXIMUM_SIZE 130" in ioctl
                and "uint32_t    version;" in carrier
                and "uint32_t    max_peers;" in carrier
                and "FUNC_IOCTL(PEER_CACHE_MAXIMUM_SIZE, apple80211_peer_cache_maximum_size)" in hpp
                and "if (request_type != SIOCGA80211 && request_type != SIOCSA80211)" in virtual_request
                and "IOCTL(request_type, PEER_CACHE_MAXIMUM_SIZE, apple80211_peer_cache_maximum_size);" in legacy_dispatch
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
                and "memcpy" not in legacy_setter
                and "memset" not in legacy_setter
            ),
            "legacy_getter_is_preserved_separately": (
                "data->version = APPLE80211_VERSION;" in legacy_getter
                and "data->max_peers = 255;" in legacy_getter
                and "return kIOReturnSuccess;" in legacy_getter
            ),
            "tahoe_get_and_set_rejection_remain_separate": (
                "if (cmd == SIOCGA80211)" in tahoe_route
                and "getAPSTA_PEER_CACHE_MAXIMUM_SIZE" in tahoe_route
                and "data->max_peers = 255;" in tahoe_route
                and "return kIOReturnUnsupported;" in tahoe_route
                and "setPEER_CACHE_MAXIMUM_SIZE" not in tahoe_route
            ),
            "legacy_source_is_absent_from_tahoe_phase": (
                project.count("AirportVirtualIOCTL.cpp in Sources") >= 6
                and "AirportVirtualIOCTL.cpp" not in tahoe_phase
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
        raise ValueError("peer-cache fixed-stub checks failed: " + ", ".join(failed))
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
        print(f"peer-cache fixed-stub validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
