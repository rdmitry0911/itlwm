#!/usr/bin/env python3
"""Generate and verify Skywalk public VIRTUAL_IF_DELETE fixed-stub evidence."""

import argparse
import hashlib
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/skywalk_public_virtual_if_delete_fixed_stub_alignment_report.json"
NOTE = ROOT / "docs/reference/CR-524-skywalk-public-virtual-if-delete-fixed-stub-alignment-20260715.md"
RAW = ROOT / "docs/reference/artifacts/skywalk-virtual-if-delete-public-fixed-stub-bootkc-current/raw.txt"
RAW_MANIFEST = RAW.with_name("SHA256SUMS.txt")
CPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.cpp"
V2 = ROOT / "AirportItlwm/AirportItlwmV2.cpp"
V2_HPP = ROOT / "AirportItlwm/AirportItlwmV2.hpp"
V1 = ROOT / "AirportItlwm/AirportSTAIOCTL.cpp"
HPP = ROOT / "AirportItlwm/AirportItlwm.hpp"
IOCTL = ROOT / "include/Airport/apple80211_ioctl.h"
ROUTES = ROOT / "AirportItlwm/TahoeSkywalkIoctlRoutes.hpp"
PROJECT = ROOT / "itlwm.xcodeproj/project.pbxproj"


def section(source, begin, end):
    start = source.index(begin)
    return source[start:source.index(end, start)]


def report():
    cpp = CPP.read_text(encoding="utf-8")
    v2 = V2.read_text(encoding="utf-8")
    v2_hpp = V2_HPP.read_text(encoding="utf-8")
    v1 = V1.read_text(encoding="utf-8")
    hpp = HPP.read_text(encoding="utf-8")
    ioctl = IOCTL.read_text(encoding="utf-8")
    routes = ROUTES.read_text(encoding="utf-8")
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
    virtual_delete_route = section(
        dispatcher,
        "case APPLE80211_IOC_VIRTUAL_IF_DELETE:",
        "case APPLE80211_IOC_SCAN_REQ:",
    )
    tahoe_set_branch = section(
        virtual_delete_route,
        "#if __IO80211_TARGET >= __MAC_26_0",
        "#else",
    )
    pre26_set_branch = section(
        virtual_delete_route,
        "#else",
        "#endif // __IO80211_TARGET >= __MAC_26_0",
    )
    skywalk_helper = section(
        cpp,
        "setVIRTUAL_IF_DELETE(apple80211_virt_if_delete_data *data)\n{",
        "setROAM_PROFILE(apple80211_roam_profile_all_bands *)",
    )
    v1_dispatch = section(
        v1,
        "case APPLE80211_IOC_VIRTUAL_IF_DELETE:",
        "case APPLE80211_IOC_VIRTUAL_IF_ROLE:",
    )
    v1_helper = section(
        v1,
        "setVIRTUAL_IF_DELETE(OSObject *object, struct apple80211_virt_if_delete_data *data)",
        "getLINK_CHANGED_EVENT_DATA(",
    )
    owner_cleanup = section(
        v2,
        "void AirportItlwm::deleteAPSTAOwner()",
        "IOReturn AirportItlwm::deleteAPSTAOwnerForBSDName",
    )
    owner_delete = section(
        v2,
        "IOReturn AirportItlwm::deleteAPSTAOwnerForBSDName",
        "IOReturn AirportItlwm::\nsetSOFTAP_EXTENDED_CAPABILITIES_IE",
    )
    card_specific = section(
        v2,
        "SInt32 AirportItlwm::handleCardSpecific(",
        "IOReturn AirportItlwm::enableAdapter",
    )

    null_guard = "if (req->req_data == NULL)\n        return kIOReturnUnsupported;"
    active_source_markers = (
        "F8A028722A4A7FE100C6DE90 /* AirportItlwmSkywalkInterface.cpp in Sources */",
        "F8D94CD22B9ABFE20081A3C4 /* AirportItlwmSkywalkInterface.cpp in Sources */",
        "F8E94CD22B9ABFE20081A3C4 /* AirportItlwmSkywalkInterface.cpp in Sources */",
    )
    return {
        "schema": "itlwm-skywalk-public-virtual-if-delete-fixed-stub-alignment-v1",
        "source_base_revision": "d874de83b7db330289a89c9a360577e48b1cb5e4",
        "reference": {
            "bootkc_sha256": "eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d",
            "bootkc_uuid": "F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5",
            "embedded_kext_uuid": "8FB4B7F0-D656-3539-B8D6-C1327A50377C",
            "nlist_index": 7756,
            "public_wrapper": "0xffffff80021c415a",
            "next_symbol": "0xffffff80021c4165",
            "fixed_status": "0xe082280e",
            "body_sha256": "9e4580f0175946d7624b2451e6ffda84a93e91e3e2d852d7a2c7998ee2d78576",
        },
        "scope": {
            "normal_nonnull_public_set_only": True,
            "outer_or_inner_null_fallback_modified": False,
            "pre26_helper_route_modified": False,
            "apsta_owner_cleanup_modified": False,
            "card_specific_route_modified": False,
            "deployment": False,
            "radio_or_association": False,
            "runtime_selector_invocation": False,
            "traffic": False,
        },
        "checks": {
            "reference_raw_manifest_matches": manifest == f"{raw_digest}  raw.txt\n",
            "reference_raw_has_exact_unread_public_fixed_stub": all(
                token in raw
                for token in (
                    "eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d",
                    "F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5",
                    "8FB4B7F0-D656-3539-B8D6-C1327A50377C",
                    "nlist_index=7756",
                    "n_type=0x0f",
                    "n_sect=1",
                    "n_desc=0x0000",
                    "__Z30apple80211setVIRTUAL_IF_DELETEP23IO80211SkywalkInterfacePv",
                    "symbol_vmaddr=0xffffff80021c415a",
                    "symbol_vmaddr_end=0xffffff80021c4165",
                    "symbol_fileoff=0x20c415a",
                    "symbol_fileoff_end=0x20c4165",
                    "symbol_bytes=0x0b",
                    "next_nlist_index=7635",
                    "__Z28apple80211setVIRTUAL_IF_ROLEP23IO80211SkywalkInterfacePv",
                    "body_sha256=9e4580f0175946d7624b2451e6ffda84a93e91e3e2d852d7a2c7998ee2d78576",
                    "0xe082280e",
                    "reads neither public argument",
                )
            ),
            "note_records_scope_and_nonclaims": all(
                token in note
                for token in (
                    "IOC 95",
                    "0xffffff80021c415a",
                    "0xe082280e",
                    "compile-time Tahoe-only guard",
                    "normal non-null public Tahoe SET",
                    "current-reference supersession",
                    "does not claim outer-null, GET, carrier/ABI, APSTA, owner-lifetime, firmware, runtime-execution, or broader Tahoe behavior parity",
                    "No private carrier or selector is constructed or invoked",
                )
            ),
            "normal_nonnull_tahoe_public_route_returns_exact_fixed_status_unread": (
                "case APPLE80211_IOC_VIRTUAL_IF_DELETE:" in virtual_delete_route
                and "if (cmd == SIOCSA80211)" in virtual_delete_route
                and "return static_cast<IOReturn>(0xe082280e);" in tahoe_set_branch
                and "setVIRTUAL_IF_DELETE" not in tahoe_set_branch
                and "deleteAPSTAOwner" not in tahoe_set_branch
                and "req->req_data->" not in tahoe_set_branch
                and "return kIOReturnSuccess;" not in tahoe_set_branch
            ),
            "pre26_public_route_retains_existing_helper": (
                "return setVIRTUAL_IF_DELETE(" in pre26_set_branch
                and "(apple80211_virt_if_delete_data *)req->req_data" in pre26_set_branch
                and "static_cast<IOReturn>(0xe082280e)" not in pre26_set_branch
            ),
            "null_and_get_fallbacks_remain_outside_this_layer": (
                null_guard in dispatcher
                and dispatcher.index(null_guard)
                < dispatcher.index("case APPLE80211_IOC_VIRTUAL_IF_DELETE:")
                and "return kIOReturnUnsupported;" in virtual_delete_route
            ),
            "bsd_ingress_terminalizes_nonunsupported": (
                "IOReturn ret = processApple80211Ioctl(normalizedCmd, req);" in bsd_bridge
                and "if (ret != kIOReturnUnsupported)\n            return ret;" in bsd_bridge
            ),
            "card_specific_tahoe_route_excludes_ioc95": (
                "routeTahoeSkywalkIoctl(interface, &req," in card_specific
                and "kIocVirtualIfDelete" not in routes
                and "APPLE80211_IOC_VIRTUAL_IF_DELETE" not in routes
                and "95" not in routes
            ),
            "helper_controller_and_v1_cleanup_topology_remain": (
                cpp.count("setVIRTUAL_IF_DELETE(") == 2
                and "if (data == nullptr)" in skywalk_helper
                and "if (instance == nullptr)" in skywalk_helper
                and "return instance->deleteAPSTAOwnerForBSDName(data->bsd_name);" in skywalk_helper
                and "#define APPLE80211_IOC_VIRTUAL_IF_DELETE         95" in ioctl
                and "FUNC_IOCTL_SET(VIRTUAL_IF_DELETE, apple80211_virt_if_delete_data)" in hpp
                and "IOCTL_SET(request_type, VIRTUAL_IF_DELETE, apple80211_virt_if_delete_data);" in v1_dispatch
                and "#if __IO80211_TARGET >= __MAC_13_0" in v1_helper
                and "return deleteAPSTAOwnerForBSDName(data->bsd_name);" in v1_helper
                and "void AirportItlwm::deleteAPSTAOwner()" in owner_cleanup
                and "fAPSTAOwner->release();" in owner_cleanup
                and "return kIOReturnUnsupported;" in owner_delete
                and "deleteAPSTAOwner();" in owner_delete
                and "return kIOReturnSuccess;" in owner_delete
                and "IOReturn deleteAPSTAOwnerForBSDName(const uint8_t *bsdName);" in v2_hpp
            ),
            "all_active_skywalk_source_phases_and_target_guard_are_explicit": (
                all(marker in project for marker in active_source_markers)
                and "#if __IO80211_TARGET >= __MAC_26_0" in virtual_delete_route
                and "#else" in virtual_delete_route
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
            "Skywalk public VIRTUAL_IF_DELETE alignment checks failed: "
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
            f"Skywalk public VIRTUAL_IF_DELETE alignment validation failed: {exc}",
            file=sys.stderr,
        )
        sys.exit(1)
