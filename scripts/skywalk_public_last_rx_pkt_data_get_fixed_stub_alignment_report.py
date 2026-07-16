#!/usr/bin/env python3
"""Generate and verify Skywalk public LAST_RX_PKT_DATA GET fixed-stub evidence."""

import argparse
import hashlib
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/skywalk_public_last_rx_pkt_data_get_fixed_stub_alignment_report.json"
NOTE = ROOT / "docs/reference/CR-556-skywalk-public-last-rx-pkt-data-get-fixed-stub-alignment-20260716.md"
RAW = ROOT / "docs/reference/artifacts/skywalk-last-rx-pkt-data-get-public-fixed-stub-bootkc-current/raw.txt"
RAW_MANIFEST = RAW.with_name("SHA256SUMS.txt")
CPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.cpp"
V2 = ROOT / "AirportItlwm/AirportItlwmV2.cpp"
V1 = ROOT / "AirportItlwm/AirportSTAIOCTL.cpp"
VIRTUAL = ROOT / "AirportItlwm/AirportVirtualIOCTL.cpp"
ROUTES = ROOT / "AirportItlwm/TahoeSkywalkIoctlRoutes.hpp"
IOCTL = ROOT / "include/Airport/apple80211_ioctl.h"
PROJECT = ROOT / "itlwm.xcodeproj/project.pbxproj"


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
    tahoe_block = section(
        dispatcher,
        "#if __IO80211_TARGET >= __MAC_26_0\n        case APPLE80211_IOC_COUNTERMEASURES:",
        "        case APPLE80211_IOC_POWER:",
    )
    last_rx_pkt_data = section(
        dispatcher,
        "case APPLE80211_IOC_LAST_RX_PKT_DATA:",
        "        case APPLE80211_IOC_POWER:",
    )
    pre26_dispatcher = dispatcher.replace(tahoe_block, "")
    card = section(v2, "SInt32 AirportItlwm::handleCardSpecific(", "IOReturn AirportItlwm::enableAdapter")
    markers = (
        "F8A028722A4A7FE100C6DE90 /* AirportItlwmSkywalkInterface.cpp in Sources */",
        "F8D94CD22B9ABFE20081A3C4 /* AirportItlwmSkywalkInterface.cpp in Sources */",
        "F8E94CD22B9ABFE20081A3C4 /* AirportItlwmSkywalkInterface.cpp in Sources */",
    )
    return {
        "schema": "itlwm-skywalk-public-last-rx-pkt-data-get-fixed-stub-alignment-v1",
        "source_base_revision": "9760b0e28701f16c87b44519e6b8f4ae020c8a11",
        "reference": {
            "bootkc_sha256": "eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d",
            "bootkc_uuid": "F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5",
            "embedded_kext_uuid": "8FB4B7F0-D656-3539-B8D6-C1327A50377C",
            "nlist_index": 7656,
            "public_wrapper": "0xffffff80021bee57",
            "next_symbol": "0xffffff80021bee62",
            "fixed_status": "0xe082280e",
            "body_sha256": "9e4580f0175946d7624b2451e6ffda84a93e91e3e2d852d7a2c7998ee2d78576",
        },
        "scope": {
            "public_nonnull_request_object_tahoe_bsd_get_only": True,
            "carrier_is_not_observed": True,
            "last_rx_pkt_data_set_modified": False,
            "outer_null_dispatch_modified": False,
            "pre26_route_modified": False,
            "card_specific_route_modified": False,
            "legacy_v1_modified": False,
            "virtual_ioctl_modified": False,
            "carrier_abi_added": False,
            "deployment": False,
            "radio_or_association": False,
            "runtime_selector_invocation": False,
            "traffic": False,
        },
        "checks": {
            "reference_raw_manifest_matches": manifest == f"{raw_digest}  raw.txt\n",
            "reference_raw_has_exact_unread_public_get_fixed_stub": all(token in raw for token in (
                "eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d",
                "F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5",
                "8FB4B7F0-D656-3539-B8D6-C1327A50377C",
                "symbol=__Z29apple80211getLAST_RX_PKT_DATAP23IO80211SkywalkInterfacePv",
                "nlist_index=7656", "n_type=0x0f", "n_sect=1", "n_desc=0x0000",
                "symbol_vmaddr=0xffffff80021bee57", "symbol_vmaddr_end=0xffffff80021bee62",
                "symbol_fileoff=0x20bee57", "symbol_fileoff_end=0x20bee62", "symbol_bytes=0x0b",
                "next_nlist_index=7244", "__Z23apple80211getRADIO_INFOP23IO80211SkywalkInterfacePv",
                "next_symbol_vmaddr=0xffffff80021bee62",
                "body_sha256=9e4580f0175946d7624b2451e6ffda84a93e91e3e2d852d7a2c7998ee2d78576",
                "0xe082280e", "reads neither public argument",
            )),
            "note_records_scope_and_nonclaims": all(token in note for token in (
                "IOC 53", "0xffffff80021bee57", "0xe082280e", "compile-time Tahoe-only case",
                "selector remains absent from the pre-26 switch", "No local LAST_RX_PKT_DATA carrier contract is inferred",
                "card-specific route has no LAST_RX_PKT_DATA entry",
                "does not claim outer-null dispatch behavior, a LAST_RX_PKT_DATA carrier contract, SET behavior, last-received-packet behavior, V1, Virtual IOCTL, card-specific behavior, firmware, runtime-execution, radio, association, traffic, or broader Tahoe behavior parity",
                "No private carrier or selector is constructed or invoked",
            )),
            "public_tahoe_get_returns_exact_unread_status": (
                dispatcher.count("case APPLE80211_IOC_LAST_RX_PKT_DATA:") == 1
                and "if (cmd == SIOCGA80211)" in last_rx_pkt_data
                and "return static_cast<IOReturn>(0xe082280e);" in last_rx_pkt_data
                and "req->req_data" not in last_rx_pkt_data
                and "return kIOReturnSuccess;" not in last_rx_pkt_data
            ),
            "tahoe_nonget_and_pre26_boundaries_remain_explicit": (
                "return kIOReturnUnsupported;" in last_rx_pkt_data
                and "SIOCSA80211" not in last_rx_pkt_data
                and "#endif // __IO80211_TARGET >= __MAC_26_0" in last_rx_pkt_data
                and "case APPLE80211_IOC_LAST_RX_PKT_DATA:" not in pre26_dispatcher
            ),
            "outer_null_and_bsd_boundaries_remain_explicit": (
                "if (req == NULL)\n        return kIOReturnUnsupported;" in dispatcher
                and dispatcher.index("if (req == NULL)\n        return kIOReturnUnsupported;") < dispatcher.index("case APPLE80211_IOC_LAST_RX_PKT_DATA:")
                and "data != NULL" in bsd
                and "IOReturn ret = processApple80211Ioctl(normalizedCmd, req);" in bsd
                and "if (ret != kIOReturnUnsupported)\n            return ret;" in bsd
            ),
            "legacy_v1_virtual_and_card_boundaries_remain_explicit": (
                "APPLE80211_IOC_LAST_RX_PKT_DATA" not in v1
                and "APPLE80211_IOC_LAST_RX_PKT_DATA" not in virtual
                and "kIocLastRxPktData" not in routes
                and "default:\n            return false;" in routes
                and "routeTahoeSkywalkIoctl(interface, &req," in card
            ),
            "selector_and_carrier_boundaries_are_explicit": (
                "#define APPLE80211_IOC_LAST_RX_PKT_DATA          53" in ioctl
                and "req->req_data" not in last_rx_pkt_data
            ),
            "all_active_skywalk_source_phases_and_target_guard_are_explicit": (
                all(marker in project for marker in markers)
                and "case APPLE80211_IOC_LAST_RX_PKT_DATA:" in tahoe_block
                and "#endif // __IO80211_TARGET >= __MAC_26_0" in tahoe_block
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
        raise ValueError("Skywalk public LAST_RX_PKT_DATA GET alignment validation failed: " + ", ".join(failed))
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
        print(f"Skywalk public LAST_RX_PKT_DATA GET alignment validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
