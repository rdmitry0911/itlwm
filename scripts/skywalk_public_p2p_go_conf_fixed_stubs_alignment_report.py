#!/usr/bin/env python3
"""Generate and verify Skywalk public P2P_GO_CONF GET/SET fixed-stub evidence."""

import argparse
import hashlib
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/skywalk_public_p2p_go_conf_fixed_stubs_alignment_report.json"
NOTE = ROOT / "docs/reference/CR-570-skywalk-public-p2p-go-conf-fixed-stubs-alignment-20260716.md"
RAW = ROOT / "docs/reference/artifacts/skywalk-p2p-go-conf-public-fixed-stubs-bootkc-current/raw.txt"
RAW_MANIFEST = RAW.with_name("SHA256SUMS.txt")
CPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.cpp"
V2 = ROOT / "AirportItlwm/AirportItlwmV2.cpp"
V1 = ROOT / "AirportItlwm/AirportSTAIOCTL.cpp"
VIRTUAL = ROOT / "AirportItlwm/AirportVirtualIOCTL.cpp"
AWDL = ROOT / "AirportItlwm/AirportAWDL.cpp"
ROUTES = ROOT / "AirportItlwm/TahoeSkywalkIoctlRoutes.hpp"
IOCTL = ROOT / "include/Airport/apple80211_ioctl.h"
PROJECT = ROOT / "itlwm.xcodeproj/project.pbxproj"

MACRO = "APPLE80211_IOC_P2P_GO_CONF"
VALUE = 98


def section(source, begin, end):
    start = source.index(begin)
    return source[start:source.index(end, start)]


def report():
    cpp, v2, v1, virtual, awdl, routes, ioctl, project = (
        path.read_text(encoding="utf-8")
        for path in (CPP, V2, V1, VIRTUAL, AWDL, ROUTES, IOCTL, PROJECT)
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
        "#if __IO80211_TARGET >= __MAC_26_0\n        case APPLE80211_IOC_P2P_GO_CONF:",
        "        case APPLE80211_IOC_SOFTAP_TRIGGER_CSA:",
    )
    route = section(
        dispatcher,
        "case APPLE80211_IOC_P2P_GO_CONF:",
        "        case APPLE80211_IOC_SOFTAP_TRIGGER_CSA:",
    )
    pre26_dispatcher = dispatcher.replace(tahoe_block, "")
    card = section(
        v2,
        "SInt32 AirportItlwm::handleCardSpecific(",
        "IOReturn AirportItlwm::enableAdapter",
    )
    legacy_setter = awdl[
        awdl.index("IOReturn AirportItlwm::\nsetP2P_GO_CONF("):
    ]
    markers = (
        "F8A028722A4A7FE100C6DE90 /* AirportItlwmSkywalkInterface.cpp in Sources */",
        "F8D94CD22B9ABFE20081A3C4 /* AirportItlwmSkywalkInterface.cpp in Sources */",
        "F8E94CD22B9ABFE20081A3C4 /* AirportItlwmSkywalkInterface.cpp in Sources */",
    )
    return {
        "schema": "itlwm-skywalk-public-p2p-go-conf-fixed-stubs-alignment-v1",
        "source_base_revision": "3f969984af3ece14f147f6b2fad3a1bad17e6c9f",
        "reference": {
            "bootkc_sha256": "eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d",
            "bootkc_uuid": "F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5",
            "embedded_kext_uuid": "8FB4B7F0-D656-3539-B8D6-C1327A50377C",
            "fixed_status": "0xe082280e",
            "body_sha256": "9e4580f0175946d7624b2451e6ffda84a93e91e3e2d852d7a2c7998ee2d78576",
            "selector": {
                "name": "P2P_GO_CONF",
                "ioc": VALUE,
                "get_nlist_index": 7309,
                "get_wrapper": "0xffffff80021bf2b1",
                "get_fileoff": "0x20bf2b1",
                "get_symbol": "__Z24apple80211getP2P_GO_CONFP23IO80211SkywalkInterfacePv",
                "get_next_nlist_index": 7378,
                "get_next_symbol": "__Z25apple80211getP2P_NOA_LISTP23IO80211SkywalkInterfacePv",
                "set_nlist_index": 7344,
                "set_wrapper": "0xffffff80021c417b",
                "set_fileoff": "0x20c417b",
                "set_symbol": "__Z24apple80211setP2P_GO_CONFP23IO80211SkywalkInterfacePv",
                "set_next_nlist_index": 7405,
                "set_next_symbol": "__Z25apple80211setP2P_NOA_LISTP23IO80211SkywalkInterfacePv",
            },
        },
        "scope": {
            "public_nonnull_request_object_tahoe_bsd_get_set_only": True,
            "carrier_is_not_observed_for_get_or_set": True,
            "outer_null_dispatch_modified": False,
            "pre26_skywalk_route_modified": False,
            "legacy_v1_set_modified": False,
            "virtual_set_modified": False,
            "historical_awdl_setter_modified": False,
            "card_specific_route_modified": False,
            "deployment": False,
            "radio_or_association": False,
            "runtime_selector_invocation": False,
            "traffic": False,
        },
        "checks": {
            "reference_raw_manifest_matches": manifest == f"{raw_digest}  raw.txt\n",
            "reference_raw_has_exact_unread_public_get_set_fixed_stubs": all(
                token in raw
                for token in (
                    "eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d",
                    "F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5",
                    "8FB4B7F0-D656-3539-B8D6-C1327A50377C",
                    "selector=P2P_GO_CONF",
                    "ioc=98",
                    "get_nlist_index=7309",
                    "get_symbol_vmaddr=0xffffff80021bf2b1",
                    "get_symbol_fileoff=0x20bf2b1",
                    "get_symbol_bytes=0x0b",
                    "get_next_nlist_index=7378",
                    "__Z25apple80211getP2P_NOA_LISTP23IO80211SkywalkInterfacePv",
                    "set_nlist_index=7344",
                    "set_symbol_vmaddr=0xffffff80021c417b",
                    "set_symbol_fileoff=0x20c417b",
                    "set_symbol_bytes=0x0b",
                    "set_next_nlist_index=7405",
                    "__Z25apple80211setP2P_NOA_LISTP23IO80211SkywalkInterfacePv",
                    "get_raw=55 48 89 e5 b8 0e 28 82 e0 5d c3",
                    "set_raw=55 48 89 e5 b8 0e 28 82 e0 5d c3",
                    "reads neither public argument",
                )
            ),
            "note_records_scope_and_nonclaims": all(
                token in note
                for token in (
                    "IOC 98",
                    "0xffffff80021bf2b1",
                    "0xffffff80021c417b",
                    "compile-time Tahoe-only case",
                    "historical V1 and Virtual SET routes and their typed carriers remain separate and unmodified",
                    "does not claim outer-null dispatch behavior, a carrier contract, P2P GO behavior, V1 or Virtual IOCTL behavior, historical AWDL behavior, card-specific behavior, firmware, runtime-execution, radio, association, traffic, or broader Tahoe behavior parity",
                    "No private carrier or selector is constructed or invoked",
                )
            ),
            "public_tahoe_get_set_return_exact_unread_status": (
                dispatcher.count(f"case {MACRO}:") == 1
                and route.count("if (cmd == SIOCGA80211 || cmd == SIOCSA80211)") == 1
                and route.count("return static_cast<IOReturn>(0xe082280e);") == 1
                and "req->req_data" not in route
                and "return kIOReturnSuccess;" not in route
            ),
            "tahoe_non_get_set_and_pre26_boundaries_remain_explicit": (
                "return kIOReturnUnsupported;" in route
                and "#endif // __IO80211_TARGET >= __MAC_26_0" in route
                and f"case {MACRO}:" not in pre26_dispatcher
            ),
            "outer_null_and_bsd_boundaries_remain_explicit": (
                "if (req == NULL)\n        return kIOReturnUnsupported;" in dispatcher
                and dispatcher.index("if (req == NULL)\n        return kIOReturnUnsupported;")
                < dispatcher.index(f"case {MACRO}:")
                and "data != NULL" in bsd
                and "IOReturn ret = processApple80211Ioctl(normalizedCmd, req);" in bsd
                and "if (ret != kIOReturnUnsupported)\n            return ret;" in bsd
            ),
            "legacy_virtual_awdl_and_card_set_boundaries_remain_explicit": (
                f"case {MACRO}:" in v1
                and "IOCTL_SET(request_type, P2P_GO_CONF, apple80211_p2p_go_conf_data);" in v1
                and f"case {MACRO}:" in virtual
                and "IOCTL_SET(request_type, P2P_GO_CONF, apple80211_p2p_go_conf_data);" in virtual
                and "(void)object;" in legacy_setter
                and "(void)data;" in legacy_setter
                and "return kP2PPublicFixedStubStatus;" in legacy_setter
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
        raise ValueError(
            "Skywalk public P2P_GO_CONF fixed-stub alignment validation failed: "
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
            f"Skywalk public P2P_GO_CONF fixed-stub alignment validation failed: {exc}",
            file=sys.stderr,
        )
        sys.exit(1)
