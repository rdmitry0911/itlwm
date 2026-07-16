#!/usr/bin/env python3
"""Generate and verify Skywalk public P2P_SCAN GET fixed-stub evidence."""

import argparse
import hashlib
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/skywalk_public_p2p_scan_get_fixed_stub_alignment_report.json"
NOTE = ROOT / "docs/reference/CR-566-skywalk-public-p2p-scan-get-fixed-stub-alignment-20260716.md"
RAW = ROOT / "docs/reference/artifacts/skywalk-p2p-scan-get-public-fixed-stub-bootkc-current/raw.txt"
RAW_MANIFEST = RAW.with_name("SHA256SUMS.txt")
CPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.cpp"
V2 = ROOT / "AirportItlwm/AirportItlwmV2.cpp"
V1 = ROOT / "AirportItlwm/AirportSTAIOCTL.cpp"
VIRTUAL = ROOT / "AirportItlwm/AirportVirtualIOCTL.cpp"
ROUTES = ROOT / "AirportItlwm/TahoeSkywalkIoctlRoutes.hpp"
IOCTL = ROOT / "include/Airport/apple80211_ioctl.h"
PROJECT = ROOT / "itlwm.xcodeproj/project.pbxproj"

MACRO = "APPLE80211_IOC_P2P_SCAN"
VALUE = 93


def section(source, begin, end):
    start = source.index(begin)
    return source[start:source.index(end, start)]


def report():
    cpp, v2, v1, virtual, routes, ioctl, project = (
        path.read_text(encoding="utf-8")
        for path in (CPP, V2, V1, VIRTUAL, ROUTES, IOCTL, PROJECT)
    )
    note = " ".join(NOTE.read_text(encoding="utf-8").split())
    raw = RAW.read_text(encoding="utf-8")
    manifest = RAW_MANIFEST.read_text(encoding="utf-8")
    raw_digest = hashlib.sha256(RAW.read_bytes()).hexdigest()
    bsd = section(cpp, "processBSDCommand(ifnet_t interface, UInt cmd, void *data)", "processApple80211Ioctl(UInt cmd, apple80211req *req)")
    dispatcher = section(cpp, "processApple80211Ioctl(UInt cmd, apple80211req *req)", "IOReturn AirportItlwmSkywalkInterface::\ngetAUTH_TYPE")
    tahoe_block = section(dispatcher, "#if __IO80211_TARGET >= __MAC_26_0\n        case APPLE80211_IOC_P2P_SCAN:", "        case APPLE80211_IOC_PEER_CACHE_CONTROL:")
    group = section(dispatcher, "case APPLE80211_IOC_P2P_SCAN:", "        case APPLE80211_IOC_PEER_CACHE_CONTROL:")
    pre26_dispatcher = dispatcher.replace(tahoe_block, "")
    card = section(v2, "SInt32 AirportItlwm::handleCardSpecific(", "IOReturn AirportItlwm::enableAdapter")
    markers = (
        "F8A028722A4A7FE100C6DE90 /* AirportItlwmSkywalkInterface.cpp in Sources */",
        "F8D94CD22B9ABFE20081A3C4 /* AirportItlwmSkywalkInterface.cpp in Sources */",
        "F8E94CD22B9ABFE20081A3C4 /* AirportItlwmSkywalkInterface.cpp in Sources */",
    )
    return {
        "schema": "itlwm-skywalk-public-p2p-scan-get-fixed-stub-alignment-v1",
        "source_base_revision": "02636ae32f8d42992ed60baec7c81e2e0632ec43",
        "reference": {
            "bootkc_sha256": "eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d",
            "bootkc_uuid": "F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5",
            "embedded_kext_uuid": "8FB4B7F0-D656-3539-B8D6-C1327A50377C",
            "fixed_status": "0xe082280e",
            "body_sha256": "9e4580f0175946d7624b2451e6ffda84a93e91e3e2d852d7a2c7998ee2d78576",
            "selector": {"name": "P2P_SCAN", "ioc": VALUE, "nlist_index": 7143, "public_wrapper": "0xffffff80021bf27a", "fileoff": "0x20bf27a", "symbol": "__Z21apple80211getP2P_SCANP23IO80211SkywalkInterfacePv", "next_nlist_index": 7724, "next_symbol": "__Z30apple80211getVIRTUAL_IF_CREATEP23IO80211SkywalkInterfaceP30apple80211_virt_if_create_data"},
        },
        "scope": {
            "public_nonnull_request_object_tahoe_bsd_get_only": True,
            "carrier_is_not_observed_for_get": True,
            "set_behavior_changed": False,
            "outer_null_dispatch_modified": False,
            "pre26_skywalk_route_modified": False,
            "legacy_v1_set_modified": False,
            "virtual_set_modified": False,
            "card_specific_route_modified": False,
            "deployment": False,
            "radio_or_association": False,
            "runtime_selector_invocation": False,
            "traffic": False,
        },
        "checks": {
            "reference_raw_manifest_matches": manifest == f"{raw_digest}  raw.txt\n",
            "reference_raw_has_exact_unread_public_get_fixed_stub": all(token in raw for token in (
                "eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d",
                "F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5", "8FB4B7F0-D656-3539-B8D6-C1327A50377C",
                "selector=P2P_SCAN", "nlist_index=7143", "symbol_vmaddr=0xffffff80021bf27a",
                "symbol_fileoff=0x20bf27a", "symbol_bytes=0x0b", "next_nlist_index=7724",
                "raw=55 48 89 e5 b8 0e 28 82 e0 5d c3",
                "body_sha256=9e4580f0175946d7624b2451e6ffda84a93e91e3e2d852d7a2c7998ee2d78576",
                "reads neither public argument",
            )),
            "note_records_scope_and_nonclaims": all(token in note for token in (
                "IOC 93", "0xffffff80021bf27a", "0xe082280e", "compile-time Tahoe-only case",
                "Tahoe selector remains absent from the pre-26 Skywalk route",
                "Legacy V1 and Virtual `P2P_SCAN` SET routes remain separate and unmodified",
                "No local GET carrier contract is inferred",
                "does not claim outer-null dispatch behavior, a carrier contract, P2P-scan behavior, SET behavior, P2P listen behavior, V1, Virtual IOCTL, card-specific behavior, firmware, runtime-execution, radio, association, traffic, or broader Tahoe behavior parity",
                "No private carrier or selector is constructed or invoked",
            )),
            "public_tahoe_get_returns_exact_unread_status": (
                dispatcher.count(f"case {MACRO}:") == 1
                and group.count("if (cmd == SIOCGA80211)") == 1
                and group.count("return static_cast<IOReturn>(0xe082280e);") == 1
                and "req->req_data" not in group
                and "return kIOReturnSuccess;" not in group
            ),
            "tahoe_nonget_and_pre26_boundaries_remain_explicit": (
                "return kIOReturnUnsupported;" in group
                and "SIOCSA80211" not in group
                and "#endif // __IO80211_TARGET >= __MAC_26_0" in group
                and f"case {MACRO}:" not in pre26_dispatcher
            ),
            "outer_null_and_bsd_boundaries_remain_explicit": (
                "if (req == NULL)\n        return kIOReturnUnsupported;" in dispatcher
                and dispatcher.index("if (req == NULL)\n        return kIOReturnUnsupported;") < dispatcher.index(f"case {MACRO}:")
                and "data != NULL" in bsd
                and "IOReturn ret = processApple80211Ioctl(normalizedCmd, req);" in bsd
                and "if (ret != kIOReturnUnsupported)\n            return ret;" in bsd
            ),
            "legacy_virtual_and_card_set_boundaries_remain_explicit": (
                f"case {MACRO}:" in v1
                and "IOCTL_SET(request_type, P2P_SCAN, apple80211_scan_data);" in v1
                and f"case {MACRO}:" in virtual
                and "IOCTL_SET(request_type, P2P_SCAN, apple80211_scan_data);" in virtual
                and MACRO not in routes
                and "default:\n            return false;" in routes
                and "routeTahoeSkywalkIoctl(interface, &req," in card
            ),
            "selector_and_active_source_phases_are_explicit": (
                f"#define {MACRO}" in ioctl
                and f"{VALUE}" in ioctl[ioctl.index(f"#define {MACRO}"):ioctl.index("\n", ioctl.index(f"#define {MACRO}"))]
                and all(marker in project for marker in markers)
                and f"case {MACRO}:" in tahoe_block
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
        raise ValueError("Skywalk public P2P_SCAN GET alignment validation failed: " + ", ".join(failed))
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
        print(f"Skywalk public P2P_SCAN GET alignment validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
