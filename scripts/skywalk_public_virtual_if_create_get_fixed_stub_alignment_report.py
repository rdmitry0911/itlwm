#!/usr/bin/env python3
"""Generate and verify Skywalk public VIRTUAL_IF_CREATE GET fixed-stub evidence."""

import argparse
import hashlib
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/skywalk_public_virtual_if_create_get_fixed_stub_alignment_report.json"
NOTE = ROOT / "docs/reference/CR-567-skywalk-public-virtual-if-create-get-fixed-stub-alignment-20260716.md"
RAW = ROOT / "docs/reference/artifacts/skywalk-virtual-if-create-get-public-fixed-stub-bootkc-current/raw.txt"
RAW_MANIFEST = RAW.with_name("SHA256SUMS.txt")
CPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.cpp"
V2 = ROOT / "AirportItlwm/AirportItlwmV2.cpp"
V1 = ROOT / "AirportItlwm/AirportSTAIOCTL.cpp"
VIRTUAL = ROOT / "AirportItlwm/AirportVirtualIOCTL.cpp"
ROUTES = ROOT / "AirportItlwm/TahoeSkywalkIoctlRoutes.hpp"
IOCTL = ROOT / "include/Airport/apple80211_ioctl.h"
PROJECT = ROOT / "itlwm.xcodeproj/project.pbxproj"

MACRO = "APPLE80211_IOC_VIRTUAL_IF_CREATE"
VALUE = 94


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
    bsd = section(
        cpp,
        "processBSDCommand(ifnet_t interface, UInt cmd, void *data)",
        "processApple80211Ioctl(UInt cmd, apple80211req *req)",
    )
    dispatcher = section(
        cpp,
        "processApple80211Ioctl(UInt cmd, apple80211req *req)",
        "IOReturn AirportItlwmSkywalkInterface::\ngetAUTH_TYPE",
    )
    route = section(
        dispatcher,
        f"case {MACRO}:",
        "        case APPLE80211_IOC_VIRTUAL_IF_DELETE:",
    )
    tahoe_get = section(
        route,
        "#if __IO80211_TARGET >= __MAC_26_0",
        "#endif // __IO80211_TARGET >= __MAC_26_0",
    )
    pre26_route = route.replace(tahoe_get, "")
    card = section(
        v2,
        "SInt32 AirportItlwm::handleCardSpecific(",
        "IOReturn AirportItlwm::enableAdapter",
    )
    markers = (
        "F8A028722A4A7FE100C6DE90 /* AirportItlwmSkywalkInterface.cpp in Sources */",
        "F8D94CD22B9ABFE20081A3C4 /* AirportItlwmSkywalkInterface.cpp in Sources */",
        "F8E94CD22B9ABFE20081A3C4 /* AirportItlwmSkywalkInterface.cpp in Sources */",
    )
    return {
        "schema": "itlwm-skywalk-public-virtual-if-create-get-fixed-stub-alignment-v1",
        "source_base_revision": "32a6add992812489f1d967ae58f8b8eb02bb58b4",
        "reference": {
            "bootkc_sha256": "eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d",
            "bootkc_uuid": "F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5",
            "embedded_kext_uuid": "8FB4B7F0-D656-3539-B8D6-C1327A50377C",
            "fixed_status": "0xe082280e",
            "body_sha256": "9e4580f0175946d7624b2451e6ffda84a93e91e3e2d852d7a2c7998ee2d78576",
            "selector": {
                "name": "VIRTUAL_IF_CREATE",
                "ioc": VALUE,
                "nlist_index": 7724,
                "public_wrapper": "0xffffff80021bf285",
                "fileoff": "0x20bf285",
                "symbol": "__Z30apple80211getVIRTUAL_IF_CREATEP23IO80211SkywalkInterfaceP30apple80211_virt_if_create_data",
                "next_nlist_index": 7725,
                "next_symbol": "__Z30apple80211getVIRTUAL_IF_DELETEP23IO80211SkywalkInterfacePv",
            },
        },
        "scope": {
            "public_nonnull_request_object_tahoe_bsd_get_only": True,
            "carrier_is_not_observed_for_get": True,
            "set_behavior_changed": False,
            "outer_null_dispatch_modified": False,
            "pre26_skywalk_route_modified": False,
            "legacy_v1_set_modified": False,
            "virtual_ioctl_route_created_or_modified": False,
            "card_specific_route_modified": False,
            "deployment": False,
            "radio_or_association": False,
            "runtime_selector_invocation": False,
            "traffic": False,
        },
        "checks": {
            "reference_raw_manifest_matches": manifest == f"{raw_digest}  raw.txt\n",
            "reference_raw_has_exact_unread_public_get_fixed_stub": all(
                token in raw
                for token in (
                    "eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d",
                    "F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5",
                    "8FB4B7F0-D656-3539-B8D6-C1327A50377C",
                    "selector=VIRTUAL_IF_CREATE",
                    "nlist_index=7724",
                    "n_type=0x0f",
                    "n_sect=1",
                    "n_desc=0x0000",
                    "symbol_vmaddr=0xffffff80021bf285",
                    "symbol_vmaddr_end=0xffffff80021bf290",
                    "symbol_fileoff=0x20bf285",
                    "symbol_fileoff_end=0x20bf290",
                    "symbol_bytes=0x0b",
                    "next_nlist_index=7725",
                    "__Z30apple80211getVIRTUAL_IF_DELETEP23IO80211SkywalkInterfacePv",
                    "raw=55 48 89 e5 b8 0e 28 82 e0 5d c3",
                    "body_sha256=9e4580f0175946d7624b2451e6ffda84a93e91e3e2d852d7a2c7998ee2d78576",
                    "reads neither public argument",
                )
            ),
            "note_records_scope_and_nonclaims": all(
                token in note
                for token in (
                    "IOC 94",
                    "0xffffff80021bf285",
                    "0xe082280e",
                    "compile-time Tahoe-only guard",
                    "existing SIOCSA80211 call to setVIRTUAL_IF_CREATE remains",
                    "legacy V1 SET route remains separate and unmodified",
                    "does not claim outer-null dispatch behavior, a carrier contract, virtual-interface creation behavior, SET behavior, V1, Virtual IOCTL, card-specific behavior, firmware, runtime-execution, radio, association, traffic, or broader Tahoe behavior parity",
                    "No private carrier or selector is constructed or invoked",
                )
            ),
            "public_tahoe_get_returns_exact_unread_status": (
                dispatcher.count(f"case {MACRO}:") == 1
                and tahoe_get.count("if (cmd == SIOCGA80211)") == 1
                and tahoe_get.count("return static_cast<IOReturn>(0xe082280e);") == 1
                and "req->req_data" not in tahoe_get
                and "return kIOReturnSuccess;" not in tahoe_get
            ),
            "pre26_and_set_boundaries_remain_explicit": (
                "#endif // __IO80211_TARGET >= __MAC_26_0" in route
                and "static_cast<IOReturn>(0xe082280e)" not in pre26_route
                and "return (cmd == SIOCSA80211) ? setVIRTUAL_IF_CREATE((apple80211_virt_if_create_data *)req->req_data)" in pre26_route
                and "kIOReturnUnsupported" in pre26_route
            ),
            "outer_null_and_bsd_boundaries_remain_explicit": (
                "if (req == NULL)\n        return kIOReturnUnsupported;" in dispatcher
                and dispatcher.index("if (req == NULL)\n        return kIOReturnUnsupported;")
                < dispatcher.index(f"case {MACRO}:")
                and "data != NULL" in bsd
                and "IOReturn ret = processApple80211Ioctl(normalizedCmd, req);" in bsd
                and "if (ret != kIOReturnUnsupported)\n            return ret;" in bsd
            ),
            "legacy_v1_virtual_and_card_boundaries_remain_explicit": (
                f"case {MACRO}:" in v1
                and "IOCTL_SET(request_type, VIRTUAL_IF_CREATE, apple80211_virt_if_create_data);" in v1
                and MACRO not in virtual
                and MACRO not in routes
                and "default:\n            return false;" in routes
                and "routeTahoeSkywalkIoctl(interface, &req," in card
            ),
            "selector_and_active_source_phases_are_explicit": (
                f"#define {MACRO}" in ioctl
                and f"{VALUE}" in ioctl[
                    ioctl.index(f"#define {MACRO}"):
                    ioctl.index("\n", ioctl.index(f"#define {MACRO}"))
                ]
                and all(marker in project for marker in markers)
                and f"case {MACRO}:" in route
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
        raise ValueError(
            "Skywalk public VIRTUAL_IF_CREATE GET alignment validation failed: "
            + ", ".join(failed)
        )
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
        print(
            f"Skywalk public VIRTUAL_IF_CREATE GET alignment validation failed: {exc}",
            file=sys.stderr,
        )
        sys.exit(1)
