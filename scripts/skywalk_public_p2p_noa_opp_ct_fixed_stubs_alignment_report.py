#!/usr/bin/env python3
"""Generate and verify Skywalk public P2P NOA/OPP/CT fixed-stub evidence."""

import argparse
import hashlib
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/skywalk_public_p2p_noa_opp_ct_fixed_stubs_alignment_report.json"
NOTE = ROOT / "docs/reference/CR-571-skywalk-public-p2p-noa-opp-ct-fixed-stubs-alignment-20260716.md"
RAW = ROOT / "docs/reference/artifacts/skywalk-p2p-noa-opp-ct-public-fixed-stubs-bootkc-current/raw.txt"
RAW_MANIFEST = RAW.with_name("SHA256SUMS.txt")
CPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.cpp"
V2 = ROOT / "AirportItlwm/AirportItlwmV2.cpp"
V1 = ROOT / "AirportItlwm/AirportSTAIOCTL.cpp"
VIRTUAL = ROOT / "AirportItlwm/AirportVirtualIOCTL.cpp"
HPP = ROOT / "AirportItlwm/AirportItlwm.hpp"
ROUTES = ROOT / "AirportItlwm/TahoeSkywalkIoctlRoutes.hpp"
IOCTL = ROOT / "include/Airport/apple80211_ioctl.h"
PROJECT = ROOT / "itlwm.xcodeproj/project.pbxproj"

SELECTORS = (
    ("P2P_NOA_LIST", 99),
    ("P2P_OPP_PS", 100),
    ("P2P_CT_WINDOW", 101),
)


def section(source, begin, end):
    start = source.index(begin)
    return source[start:source.index(end, start)]


def report():
    cpp, v2, v1, virtual, hpp, routes, ioctl, project = (
        path.read_text(encoding="utf-8")
        for path in (CPP, V2, V1, VIRTUAL, HPP, ROUTES, IOCTL, PROJECT)
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
        "#if __IO80211_TARGET >= __MAC_26_0\n        case APPLE80211_IOC_P2P_NOA_LIST:",
        "        case APPLE80211_IOC_SOFTAP_WIFI_NETWORK_INFO_IE:",
    )
    group = section(
        dispatcher,
        "case APPLE80211_IOC_P2P_NOA_LIST:",
        "        case APPLE80211_IOC_SOFTAP_WIFI_NETWORK_INFO_IE:",
    )
    pre26_dispatcher = dispatcher.replace(tahoe_block, "")
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
    macros = tuple(f"APPLE80211_IOC_{name}" for name, _ in SELECTORS)
    return {
        "schema": "itlwm-skywalk-public-p2p-noa-opp-ct-fixed-stubs-alignment-v1",
        "source_base_revision": "b668b753be2cbe27e2764ea74ea0a95d5aa1373c",
        "reference": {
            "bootkc_sha256": "eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d",
            "bootkc_uuid": "F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5",
            "embedded_kext_uuid": "8FB4B7F0-D656-3539-B8D6-C1327A50377C",
            "fixed_status": "0xe082280e",
            "body_sha256": "9e4580f0175946d7624b2451e6ffda84a93e91e3e2d852d7a2c7998ee2d78576",
            "selectors": [
                {"name": name, "ioc": value}
                for name, value in SELECTORS
            ],
        },
        "scope": {
            "public_nonnull_request_object_tahoe_bsd_get_set_only": True,
            "carrier_is_not_observed_for_get_or_set": True,
            "outer_null_dispatch_modified": False,
            "pre26_skywalk_route_modified": False,
            "legacy_v1_route_created_or_modified": False,
            "virtual_route_created_or_modified": False,
            "typed_set_declaration_created_or_modified": False,
            "card_specific_route_modified": False,
            "deployment": False,
            "radio_or_association": False,
            "runtime_selector_invocation": False,
            "traffic": False,
        },
        "checks": {
            "reference_raw_manifest_matches": manifest == f"{raw_digest}  raw.txt\n",
            "reference_raw_has_six_exact_unread_public_fixed_stubs": (
                all(
                    token in raw
                    for token in (
                        "eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d",
                        "F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5",
                        "8FB4B7F0-D656-3539-B8D6-C1327A50377C",
                        "selector=P2P_NOA_LIST",
                        "selector=P2P_OPP_PS",
                        "selector=P2P_CT_WINDOW",
                        "get_nlist_index=7378",
                        "set_nlist_index=7405",
                        "get_nlist_index=7242",
                        "set_nlist_index=7269",
                        "get_nlist_index=7446",
                        "set_nlist_index=7477",
                        "get_raw=55 48 89 e5 b8 0e 28 82 e0 5d c3",
                        "set_raw=55 48 89 e5 b8 0e 28 82 e0 5d c3",
                        "reads neither public argument",
                    )
                )
                and raw.count("get_raw=55 48 89 e5 b8 0e 28 82 e0 5d c3") == 3
                and raw.count("set_raw=55 48 89 e5 b8 0e 28 82 e0 5d c3") == 3
            ),
            "note_records_scope_and_nonclaims": all(
                token in note
                for token in (
                    "IOC 99",
                    "IOC 100",
                    "IOC 101",
                    "compile-time Tahoe-only switch group",
                    "There are no existing V1 or Virtual dispatcher routes or typed SET declarations",
                    "does not claim outer-null dispatch behavior, a carrier contract, P2P NOA, opportunistic power-save, or CT-window behavior, V1 or Virtual IOCTL behavior, card-specific behavior, firmware, runtime-execution, radio, association, traffic, or broader Tahoe behavior parity",
                    "No private carrier or selector is constructed or invoked",
                )
            ),
            "public_tahoe_group_returns_exact_unread_status": (
                all(dispatcher.count(f"case {macro}:") == 1 for macro in macros)
                and group.count("if (cmd == SIOCGA80211 || cmd == SIOCSA80211)") == 1
                and group.count("return static_cast<IOReturn>(0xe082280e);") == 1
                and "req->req_data" not in group
                and "return kIOReturnSuccess;" not in group
            ),
            "tahoe_non_get_set_and_pre26_boundaries_remain_explicit": (
                "return kIOReturnUnsupported;" in group
                and "#endif // __IO80211_TARGET >= __MAC_26_0" in group
                and all(f"case {macro}:" not in pre26_dispatcher for macro in macros)
            ),
            "outer_null_and_bsd_boundaries_remain_explicit": (
                "if (req == NULL)\n        return kIOReturnUnsupported;" in dispatcher
                and dispatcher.index("if (req == NULL)\n        return kIOReturnUnsupported;")
                < dispatcher.index("case APPLE80211_IOC_P2P_NOA_LIST:")
                and "data != NULL" in bsd
                and "IOReturn ret = processApple80211Ioctl(normalizedCmd, req);" in bsd
                and "if (ret != kIOReturnUnsupported)\n            return ret;" in bsd
            ),
            "legacy_virtual_declarations_and_card_boundaries_remain_explicit": (
                all(macro not in v1 for macro in macros)
                and all(macro not in virtual for macro in macros)
                and all(f"FUNC_IOCTL_SET({name}," not in hpp for name, _ in SELECTORS)
                and all(macro not in routes for macro in macros)
                and "default:\n            return false;" in routes
                and "routeTahoeSkywalkIoctl(interface, &req," in card
            ),
            "selector_and_active_source_phases_are_explicit": (
                all(
                    f"#define {macro}" in ioctl
                    and f"{value}" in ioctl[
                        ioctl.index(f"#define {macro}"):
                        ioctl.index("\n", ioctl.index(f"#define {macro}"))
                    ]
                    for macro, (_, value) in zip(macros, SELECTORS)
                )
                and all(marker in project for marker in markers)
                and all(f"case {macro}:" in tahoe_block for macro in macros)
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
            "Skywalk public P2P NOA/OPP/CT fixed-stub alignment validation failed: "
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
            f"Skywalk public P2P NOA/OPP/CT fixed-stub alignment validation failed: {exc}",
            file=sys.stderr,
        )
        sys.exit(1)
