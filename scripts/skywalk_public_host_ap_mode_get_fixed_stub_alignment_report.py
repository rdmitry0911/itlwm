#!/usr/bin/env python3
"""Generate and verify Skywalk public HOST_AP_MODE GET fixed-stub evidence."""

import argparse
import hashlib
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/skywalk_public_host_ap_mode_get_fixed_stub_alignment_report.json"
NOTE = ROOT / "docs/reference/CR-541-skywalk-public-host-ap-mode-get-fixed-stub-alignment-20260715.md"
RAW = ROOT / "docs/reference/artifacts/skywalk-host-ap-mode-get-public-fixed-stub-bootkc-current/raw.txt"
RAW_MANIFEST = RAW.with_name("SHA256SUMS.txt")
CPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.cpp"
V2 = ROOT / "AirportItlwm/AirportItlwmV2.cpp"
V1 = ROOT / "AirportItlwm/AirportSTAIOCTL.cpp"
VIRTUAL = ROOT / "AirportItlwm/AirportVirtualIOCTL.cpp"
ROUTES = ROOT / "AirportItlwm/TahoeSkywalkIoctlRoutes.hpp"
IOCTL = ROOT / "include/Airport/apple80211_ioctl.h"
VAR = ROOT / "include/Airport/apple80211_var.h"
PROJECT = ROOT / "itlwm.xcodeproj/project.pbxproj"


def section(source, begin, end):
    start = source.index(begin)
    return source[start:source.index(end, start)]


def report():
    cpp = CPP.read_text(encoding="utf-8")
    v2 = V2.read_text(encoding="utf-8")
    v1 = V1.read_text(encoding="utf-8")
    virtual = VIRTUAL.read_text(encoding="utf-8")
    routes = ROUTES.read_text(encoding="utf-8")
    ioctl = IOCTL.read_text(encoding="utf-8")
    var = VAR.read_text(encoding="utf-8")
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
    host_ap_case = section(
        dispatcher,
        "case APPLE80211_IOC_HOST_AP_MODE:",
        "case APPLE80211_IOC_AP_MODE:",
    )
    tahoe_branch = section(
        host_ap_case,
        "#if __IO80211_TARGET >= __MAC_26_0",
        "#endif // __IO80211_TARGET >= __MAC_26_0",
    )
    after_tahoe_branch = host_ap_case[
        host_ap_case.index("#endif // __IO80211_TARGET >= __MAC_26_0")
        + len("#endif // __IO80211_TARGET >= __MAC_26_0"):
    ]
    route_switch = section(
        routes,
        "inline bool shouldRoute(uint32_t reqType, bool isSet)",
        "} // namespace TahoeSkywalkIoctlRoutes",
    )
    set_only_routes = section(route_switch, "case kIocAssociate:", "default:")
    active_source_markers = (
        "F8A028722A4A7FE100C6DE90 /* AirportItlwmSkywalkInterface.cpp in Sources */",
        "F8D94CD22B9ABFE20081A3C4 /* AirportItlwmSkywalkInterface.cpp in Sources */",
        "F8E94CD22B9ABFE20081A3C4 /* AirportItlwmSkywalkInterface.cpp in Sources */",
    )

    return {
        "schema": "itlwm-skywalk-public-host-ap-mode-get-fixed-stub-alignment-v1",
        "source_base_revision": "f2a7816d0d5f141baff4bc1f140a9f8cbd9475ad",
        "reference": {
            "bootkc_sha256": "eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d",
            "bootkc_uuid": "F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5",
            "embedded_kext_uuid": "8FB4B7F0-D656-3539-B8D6-C1327A50377C",
            "nlist_index": 7374,
            "public_wrapper": "0xffffff80021be942",
            "next_symbol": "0xffffff80021be94d",
            "fixed_status": "0xe082280e",
            "body_sha256": "9e4580f0175946d7624b2451e6ffda84a93e91e3e2d852d7a2c7998ee2d78576",
        },
        "scope": {
            "public_nonnull_request_object_tahoe_bsd_get_only": True,
            "carrier_and_instance_are_not_observed": True,
            "host_ap_set_modified": False,
            "outer_null_dispatch_modified": False,
            "pre26_route_modified": False,
            "card_specific_route_modified": False,
            "legacy_v1_modified": False,
            "virtual_ioctl_modified": False,
            "carrier_abi_modified": False,
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
                    "symbol=__Z25apple80211getHOST_AP_MODEP23IO80211SkywalkInterfaceP23apple80211_network_data",
                    "nlist_index=7374",
                    "n_type=0x0f",
                    "n_sect=1",
                    "n_desc=0x0000",
                    "symbol_vmaddr=0xffffff80021be942",
                    "symbol_vmaddr_end=0xffffff80021be94d",
                    "symbol_fileoff=0x20be942",
                    "symbol_fileoff_end=0x20be94d",
                    "symbol_bytes=0x0b",
                    "next_nlist_index=7088",
                    "__Z20apple80211getAP_MODEP23IO80211SkywalkInterfaceP22apple80211_apmode_data",
                    "next_symbol_vmaddr=0xffffff80021be94d",
                    "body_sha256=9e4580f0175946d7624b2451e6ffda84a93e91e3e2d852d7a2c7998ee2d78576",
                    "0xe082280e",
                    "reads neither public argument",
                )
            ),
            "note_records_scope_and_nonclaims": all(
                token in note
                for token in (
                    "IOC 25",
                    "0xffffff80021be942",
                    "0xe082280e",
                    "compile-time Tahoe-only GET branch",
                    "APSTA instance guard and setHOST_AP_MODE producer remain below the GET branch",
                    "admit IOC 25 only when isSet",
                    "does not claim outer-null dispatch behavior, a HOST_AP_MODE carrier ABI, SET HostAP behavior, APSTA-owner lifecycle, V1, Virtual IOCTL, card-specific behavior, AP startup, firmware, runtime-execution, radio, association, traffic, or broader Tahoe behavior parity",
                    "No private carrier or selector is constructed or invoked",
                )
            ),
            "public_tahoe_get_returns_exact_unread_status": (
                dispatcher.count("case APPLE80211_IOC_HOST_AP_MODE:") == 1
                and "if (cmd == SIOCGA80211)" in tahoe_branch
                and "return static_cast<IOReturn>(0xe082280e);" in tahoe_branch
                and "req->req_data" not in tahoe_branch
                and "instance->" not in tahoe_branch
                and "if (instance == NULL)" not in tahoe_branch
                and "setHOST_AP_MODE" not in tahoe_branch
                and "return kIOReturnSuccess;" not in tahoe_branch
            ),
            "instance_set_and_pre26_routes_remain_separate": (
                "if (instance == NULL)\n                return kIOReturnNotReady;" in after_tahoe_branch
                and "? instance->setHOST_AP_MODE(" in after_tahoe_branch
                and "SIOCSA80211" in after_tahoe_branch
                and "SIOCSA80211" not in tahoe_branch
            ),
            "outer_null_dispatch_remains_outside_this_layer": (
                "if (req == NULL)\n        return kIOReturnUnsupported;" in dispatcher
                and dispatcher.index("if (req == NULL)\n        return kIOReturnUnsupported;")
                < dispatcher.index("case APPLE80211_IOC_HOST_AP_MODE:")
                and "data != NULL" in bsd_bridge
            ),
            "bsd_ingress_terminalizes_exact_nonunsupported_status": (
                "IOReturn ret = processApple80211Ioctl(normalizedCmd, req);" in bsd_bridge
                and "if (ret != kIOReturnUnsupported)\n            return ret;" in bsd_bridge
            ),
            "legacy_virtual_and_card_get_boundaries_remain_separate": (
                "APPLE80211_IOC_HOST_AP_MODE" not in v1
                and "APPLE80211_IOC_HOST_AP_MODE" not in virtual
                and "kIocHostApMode = 25" in routes
                and "case kIocHostApMode:" in set_only_routes
                and "return isSet;" in set_only_routes
                and "if (req->req_data == nullptr)\n        return false;" in v2
                and "return TahoeSkywalkIoctlRoutes::shouldRoute(req->req_type, isSet);" in v2
            ),
            "selector_and_existing_network_carrier_are_not_consumed_by_get": (
                "#define APPLE80211_IOC_HOST_AP_MODE             25" in ioctl
                and "#define APPLE80211_IOC_HOST_AP_MODE_START        1" in ioctl
                and "#define APPLE80211_IOC_HOST_AP_MODE_STOP         2" in ioctl
                and "struct apple80211_network_data" in var
                and "AirportItlwmAPSTAHostApModeNetworkDataLayout" in after_tahoe_branch
                and "req->req_data" not in tahoe_branch
            ),
            "all_active_skywalk_source_phases_and_target_guard_are_explicit": (
                all(marker in project for marker in active_source_markers)
                and "#if __IO80211_TARGET >= __MAC_26_0" in host_ap_case
                and "#endif // __IO80211_TARGET >= __MAC_26_0" in host_ap_case
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
            "Skywalk public HOST_AP_MODE GET alignment validation failed: "
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
            f"Skywalk public HOST_AP_MODE GET alignment validation failed: {exc}",
            file=sys.stderr,
        )
        sys.exit(1)
