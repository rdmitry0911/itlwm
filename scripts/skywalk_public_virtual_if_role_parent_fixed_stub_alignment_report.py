#!/usr/bin/env python3
"""Generate and verify public Skywalk VIRTUAL_IF_ROLE/PARENT fixed-stub evidence."""

import argparse
import hashlib
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/skywalk_public_virtual_if_role_parent_fixed_stub_alignment_report.json"
NOTE = ROOT / "docs/reference/CR-527-skywalk-public-virtual-if-role-parent-fixed-stub-alignment-20260715.md"
RAW = ROOT / "docs/reference/artifacts/skywalk-virtual-if-role-parent-public-fixed-stub-bootkc-current/raw.txt"
RAW_MANIFEST = RAW.with_name("SHA256SUMS.txt")
CPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.cpp"
V2 = ROOT / "AirportItlwm/AirportItlwmV2.cpp"
ROUTES = ROOT / "AirportItlwm/TahoeSkywalkIoctlRoutes.hpp"
IOCTL = ROOT / "include/Airport/apple80211_ioctl.h"
PROJECT = ROOT / "itlwm.xcodeproj/project.pbxproj"


def section(source, begin, end):
    start = source.index(begin)
    return source[start:source.index(end, start)]


def after_tahoe_guard(source):
    marker = "#endif // __IO80211_TARGET >= __MAC_26_0"
    return source[source.index(marker) + len(marker):]


def report():
    cpp = CPP.read_text(encoding="utf-8")
    v2 = V2.read_text(encoding="utf-8")
    routes = ROUTES.read_text(encoding="utf-8")
    ioctl = IOCTL.read_text(encoding="utf-8")
    project = PROJECT.read_text(encoding="utf-8")
    note = " ".join(NOTE.read_text(encoding="utf-8").split())
    raw = RAW.read_text(encoding="utf-8")
    manifest = RAW_MANIFEST.read_text(encoding="utf-8")
    raw_digest = hashlib.sha256(RAW.read_bytes()).hexdigest()

    bsd_bridge = section(
        cpp,
        "processBSDCommand(ifnet_t interface, UInt cmd, void *data)",
        "processApple80211Ioctl(UInt cmd, apple80211req *req)",
    )
    dispatcher = section(
        cpp,
        "processApple80211Ioctl(UInt cmd, apple80211req *req)",
        "IOReturn AirportItlwmSkywalkInterface::\ngetAUTH_TYPE",
    )
    role_route = section(
        dispatcher,
        "case APPLE80211_IOC_VIRTUAL_IF_ROLE:",
        "case APPLE80211_IOC_VIRTUAL_IF_PARENT:",
    )
    parent_route = section(
        dispatcher,
        "case APPLE80211_IOC_VIRTUAL_IF_PARENT: {",
        "case APPLE80211_IOC_STATE:",
    )
    role_tahoe = section(
        role_route,
        "#if __IO80211_TARGET >= __MAC_26_0",
        "#endif // __IO80211_TARGET >= __MAC_26_0",
    )
    parent_tahoe = section(
        parent_route,
        "#if __IO80211_TARGET >= __MAC_26_0",
        "#endif // __IO80211_TARGET >= __MAC_26_0",
    )
    role_after_guard = after_tahoe_guard(role_route)
    parent_after_guard = after_tahoe_guard(parent_route)
    card_specific = section(
        v2,
        "SInt32 AirportItlwm::handleCardSpecific(",
        "IOReturn AirportItlwm::enableAdapter",
    )
    card_route_helper = section(
        v2,
        "static bool shouldRouteTahoeSkywalkIoctlReq",
        "namespace {",
    )
    role_parent_policy = section(
        routes,
        "case kIocSsid:",
        "case kIocBtCoexFlags:",
    )
    active_source_markers = (
        "F8A028722A4A7FE100C6DE90 /* AirportItlwmSkywalkInterface.cpp in Sources */",
        "F8D94CD22B9ABFE20081A3C4 /* AirportItlwmSkywalkInterface.cpp in Sources */",
        "F8E94CD22B9ABFE20081A3C4 /* AirportItlwmSkywalkInterface.cpp in Sources */",
    )

    return {
        "schema": "itlwm-skywalk-public-virtual-if-role-parent-fixed-stub-alignment-v2",
        "source_base_revision": "3cfa60e95871e9771a6f7fc89ed04c5a14ebb3b3",
        "reference": {
            "bootkc_sha256": "eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d",
            "bootkc_uuid": "F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5",
            "embedded_kext_uuid": "8FB4B7F0-D656-3539-B8D6-C1327A50377C",
            "fixed_status": "0xe082280e",
            "body_sha256": "9e4580f0175946d7624b2451e6ffda84a93e91e3e2d852d7a2c7998ee2d78576",
            "wrappers": {
                "get_role": {"nlist_index": 7598, "vm": "0xffffff80021bf29b", "next": "0xffffff80021bf2a6"},
                "get_parent": {"nlist_index": 7726, "vm": "0xffffff80021bf2a6", "next": "0xffffff80021bf2b1"},
                "set_role": {"nlist_index": 7635, "vm": "0xffffff80021c4165", "next": "0xffffff80021c4170"},
                "set_parent": {"nlist_index": 7757, "vm": "0xffffff80021c4170", "next": "0xffffff80021c417b"},
            },
        },
        "scope": {
            "outer_request_public_tahoe_get_set_only": True,
            "null_fallback_modified": False,
            "unknown_command_fallback_modified": False,
            "pre26_route_modified": False,
            "carrier_or_abi_modified": False,
            "card_specific_route_modified": False,
            "legacy_v1_modified": False,
            "deployment": False,
            "radio_or_association": False,
            "runtime_selector_invocation": False,
            "traffic": False,
        },
        "checks": {
            "reference_raw_manifest_matches": manifest == f"{raw_digest}  raw.txt\n",
            "reference_raw_has_all_four_exact_unread_public_fixed_stubs": all(
                token in raw
                for token in (
                    "eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d",
                    "F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5",
                    "8FB4B7F0-D656-3539-B8D6-C1327A50377C",
                    "n_type=0x0f",
                    "n_sect=1",
                    "n_desc=0x0000",
                    "get_role_nlist_index=7598",
                    "get_parent_nlist_index=7726",
                    "set_role_nlist_index=7635",
                    "set_parent_nlist_index=7757",
                    "__Z28apple80211getVIRTUAL_IF_ROLEP23IO80211SkywalkInterfacePv",
                    "__Z30apple80211getVIRTUAL_IF_PARENTP23IO80211SkywalkInterfacePv",
                    "__Z28apple80211setVIRTUAL_IF_ROLEP23IO80211SkywalkInterfacePv",
                    "__Z30apple80211setVIRTUAL_IF_PARENTP23IO80211SkywalkInterfacePv",
                    "get_role_symbol_vmaddr=0xffffff80021bf29b",
                    "get_role_symbol_vmaddr_end=0xffffff80021bf2a6",
                    "get_role_symbol_fileoff=0x20bf29b",
                    "get_role_symbol_fileoff_end=0x20bf2a6",
                    "get_role_next_nlist_index=7726",
                    "get_role_next_symbol=__Z30apple80211getVIRTUAL_IF_PARENTP23IO80211SkywalkInterfacePv",
                    "get_role_next_symbol_vmaddr=0xffffff80021bf2a6",
                    "get_parent_symbol_vmaddr=0xffffff80021bf2a6",
                    "get_parent_symbol_vmaddr_end=0xffffff80021bf2b1",
                    "get_parent_symbol_fileoff=0x20bf2a6",
                    "get_parent_symbol_fileoff_end=0x20bf2b1",
                    "get_parent_next_nlist_index=7309",
                    "get_parent_next_symbol=__Z24apple80211getP2P_GO_CONFP23IO80211SkywalkInterfacePv",
                    "get_parent_next_symbol_vmaddr=0xffffff80021bf2b1",
                    "set_role_symbol_vmaddr=0xffffff80021c4165",
                    "set_role_symbol_vmaddr_end=0xffffff80021c4170",
                    "set_role_symbol_fileoff=0x20c4165",
                    "set_role_symbol_fileoff_end=0x20c4170",
                    "set_role_next_nlist_index=7757",
                    "set_role_next_symbol=__Z30apple80211setVIRTUAL_IF_PARENTP23IO80211SkywalkInterfacePv",
                    "set_role_next_symbol_vmaddr=0xffffff80021c4170",
                    "set_parent_symbol_vmaddr=0xffffff80021c4170",
                    "set_parent_symbol_vmaddr_end=0xffffff80021c417b",
                    "set_parent_symbol_fileoff=0x20c4170",
                    "set_parent_symbol_fileoff_end=0x20c417b",
                    "set_parent_next_nlist_index=7344",
                    "set_parent_next_symbol=__Z24apple80211setP2P_GO_CONFP23IO80211SkywalkInterfacePv",
                    "set_parent_next_symbol_vmaddr=0xffffff80021c417b",
                    "get_role_symbol_bytes=0x0b",
                    "get_parent_symbol_bytes=0x0b",
                    "set_role_symbol_bytes=0x0b",
                    "set_parent_symbol_bytes=0x0b",
                    "body_sha256=9e4580f0175946d7624b2451e6ffda84a93e91e3e2d852d7a2c7998ee2d78576",
                    "0xe082280e",
                    "reads neither public argument",
                )
            ),
            "note_records_scope_and_nonclaims": all(
                token in note
                for token in (
                    "IOC 96",
                    "IOC 97",
                    "0xffffff80021bf29b",
                    "0xffffff80021c417b",
                    "0xe082280e",
                    "compile-time Tahoe-only guard",
                    "outer-request public Tahoe GET/SET",
                    "without inspecting `req_data`",
                    "card-specific SET remains excluded",
                    "no independent card-specific contract claim",
                    "does not claim null, carrier/ABI, private virtual-route, card-specific contract, V1, pre-26, association, firmware, runtime-execution, or broader Tahoe behavior parity",
                    "No private carrier or selector is constructed or invoked",
                )
            ),
            "outer_request_tahoe_role_and_parent_get_set_return_exact_unread_status_without_inner_carrier_read": (
                "req->req_data" not in role_tahoe
                and "cmd == SIOCGA80211 || cmd == SIOCSA80211" in role_tahoe
                and "return static_cast<IOReturn>(0xe082280e);" in role_tahoe
                and "getInterfaceRole" not in role_tahoe
                and "req->req_len" not in role_tahoe
                and "return kIOReturnSuccess;" not in role_tahoe
                and "req->req_data" not in parent_tahoe
                and "cmd == SIOCGA80211 || cmd == SIOCSA80211" in parent_tahoe
                and "return static_cast<IOReturn>(0xe082280e);" in parent_tahoe
                and "getPrimarySkywalkInterface" not in parent_tahoe
                and "getBSDName" not in parent_tahoe
                and "memmove" not in parent_tahoe
                and "req->req_len" not in parent_tahoe
                and "return kIOReturnSuccess;" not in parent_tahoe
            ),
            "null_unknown_and_pre26_role_parent_logic_remain_after_guard": (
                "if (cmd != SIOCGA80211)\n                return kIOReturnUnsupported;" in role_after_guard
                and "if (req->req_data == NULL)\n                return kApple80211NotVirtualInterface;" in role_after_guard
                and "getInterfaceRole()" in role_after_guard
                and "return kIOReturnSuccess;" in role_after_guard
                and "if (cmd != SIOCGA80211)\n                return kIOReturnUnsupported;" in parent_after_guard
                and "if (req->req_data == NULL)\n                return kApple80211NotVirtualInterface;" in parent_after_guard
                and "getPrimarySkywalkInterface" in parent_after_guard
                and "getBSDName" in parent_after_guard
                and "memmove" in parent_after_guard
                and "return kIOReturnSuccess;" in parent_after_guard
            ),
            "bsd_ingress_terminalizes_nonunsupported_status": (
                "IOReturn ret = processApple80211Ioctl(normalizedCmd, req);" in bsd_bridge
                and "if (ret != kIOReturnUnsupported)\n            return ret;" in bsd_bridge
            ),
            "card_specific_get_shares_dispatch_but_set_stays_excluded": (
                "if (req->req_data == nullptr)\n        return false;" in card_route_helper
                and "case APPLE80211_IOC_VIRTUAL_IF_ROLE:" in card_specific
                and "req.req_len = sizeof(uint32_t);" in card_specific
                and "case APPLE80211_IOC_VIRTUAL_IF_PARENT:" in card_specific
                and "req.req_len = IFNAMSIZ;" in card_specific
                and "routeTahoeSkywalkIoctl(interface, &req," in card_specific
                and "isSet ? SIOCSA80211 : SIOCGA80211" in card_specific
                and "case kIocVirtualIfRole:" in role_parent_policy
                and "case kIocVirtualIfParent:" in role_parent_policy
                and "return !isSet;" in role_parent_policy
            ),
            "selector_numbers_and_carrier_abi_remain_declared": (
                "#define APPLE80211_IOC_VIRTUAL_IF_ROLE           96" in ioctl
                and "#define APPLE80211_IOC_VIRTUAL_IF_PARENT         97" in ioctl
                and "Pv" in raw
            ),
            "all_active_skywalk_source_phases_and_target_guards_are_explicit": (
                all(marker in project for marker in active_source_markers)
                and "#if __IO80211_TARGET >= __MAC_26_0" in role_route
                and "#endif // __IO80211_TARGET >= __MAC_26_0" in role_route
                and "#if __IO80211_TARGET >= __MAC_26_0" in parent_route
                and "#endif // __IO80211_TARGET >= __MAC_26_0" in parent_route
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
            "Skywalk public VIRTUAL_IF_ROLE/PARENT alignment checks failed: "
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
            f"Skywalk public VIRTUAL_IF_ROLE/PARENT alignment validation failed: {exc}",
            file=sys.stderr,
        )
        sys.exit(1)
