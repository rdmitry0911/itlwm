#!/usr/bin/env python3
"""Generate and verify Skywalk public CUR_PMK fixed-stub evidence."""

import argparse
import hashlib
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/skywalk_public_cur_pmk_fixed_stub_alignment_report.json"
NOTE = ROOT / "docs/reference/CR-525-skywalk-public-cur-pmk-fixed-stub-alignment-20260715.md"
RAW = ROOT / "docs/reference/artifacts/skywalk-cur-pmk-public-fixed-stub-bootkc-current/raw.txt"
RAW_MANIFEST = RAW.with_name("SHA256SUMS.txt")
CPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.cpp"
HPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.hpp"
V2 = ROOT / "AirportItlwm/AirportItlwmV2.cpp"
ROUTES = ROOT / "AirportItlwm/TahoeSkywalkIoctlRoutes.hpp"
IOCTL = ROOT / "include/Airport/apple80211_ioctl.h"
VAR = ROOT / "include/Airport/apple80211_var.h"
INFRA = ROOT / "include/Airport/IO80211InfraProtocol.h"
AGENT = ROOT / "AirportItlwmAgent/src/userclient.c"
PROJECT = ROOT / "itlwm.xcodeproj/project.pbxproj"


def section(source, begin, end):
    start = source.index(begin)
    return source[start:source.index(end, start)]


def report():
    cpp = CPP.read_text(encoding="utf-8")
    hpp = HPP.read_text(encoding="utf-8")
    v2 = V2.read_text(encoding="utf-8")
    routes = ROUTES.read_text(encoding="utf-8")
    ioctl = IOCTL.read_text(encoding="utf-8")
    var = VAR.read_text(encoding="utf-8")
    infra = INFRA.read_text(encoding="utf-8")
    agent = AGENT.read_text(encoding="utf-8")
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
    cur_pmk_route = section(
        dispatcher,
        "case APPLE80211_IOC_CUR_PMK:",
        "case APPLE80211_IOC_ASSOCIATION_STATUS:",
    )
    tahoe_set_branch = section(
        cur_pmk_route,
        "#if __IO80211_TARGET >= __MAC_26_0",
        "#else",
    )
    pre26_set_branch = section(
        cur_pmk_route,
        "#else",
        "#endif // __IO80211_TARGET >= __MAC_26_0",
    )
    get_helper = section(
        cpp,
        "getCUR_PMK(apple80211_pmk *)",
        "setCUR_PMK(struct apple80211_pmk *pmk)",
    )
    set_helper = section(
        cpp,
        "setCUR_PMK(struct apple80211_pmk *pmk)",
        "installExternalPmkLocked(const uint8_t *pmk_bytes,",
    )
    cipher_pmk = section(
        cpp,
        "case APPLE80211_CIPHER_PMK: {",
        "case APPLE80211_CIPHER_MSK: {",
    )
    card_specific = section(
        v2,
        "SInt32 AirportItlwm::handleCardSpecific(",
        "IOReturn AirportItlwm::enableAdapter",
    )
    plti_entry = section(
        v2,
        "IOReturn AirportItlwmUserClient::\nsExtDeliverPMK(",
        "IOReturn AirportItlwmUserClient::\nsExtWaitAssociationTarget(",
    )
    plti_sink = section(
        v2,
        "IOReturn AirportItlwm::\ndeliverExternalPMK(",
        "#endif // __IO80211_TARGET >= __MAC_26_0",
    )
    cur_pmk_policy = section(
        routes,
        "case kIocRoamProfile:",
        "case kIocAssociate:",
    )

    null_guard = "if (req->req_data == NULL)\n        return kIOReturnUnsupported;"
    active_source_markers = (
        "F8A028722A4A7FE100C6DE90 /* AirportItlwmSkywalkInterface.cpp in Sources */",
        "F8D94CD22B9ABFE20081A3C4 /* AirportItlwmSkywalkInterface.cpp in Sources */",
        "F8E94CD22B9ABFE20081A3C4 /* AirportItlwmSkywalkInterface.cpp in Sources */",
    )
    return {
        "schema": "itlwm-skywalk-public-cur-pmk-fixed-stub-alignment-v1",
        "source_base_revision": "bff05eb4fcb0ec799c539158db8c49e4da27affa",
        "reference": {
            "bootkc_sha256": "eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d",
            "bootkc_uuid": "F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5",
            "embedded_kext_uuid": "8FB4B7F0-D656-3539-B8D6-C1327A50377C",
            "nlist_index": 7112,
            "public_wrapper": "0xffffff80021c700b",
            "next_symbol": "0xffffff80021c7016",
            "fixed_status": "0xe082280e",
            "body_sha256": "9e4580f0175946d7624b2451e6ffda84a93e91e3e2d852d7a2c7998ee2d78576",
        },
        "scope": {
            "normal_nonnull_public_set_only": True,
            "outer_or_inner_null_fallback_modified": False,
            "pre26_helper_route_modified": False,
            "get_route_modified": False,
            "cipher_key_pmk_path_modified": False,
            "plti_deliver_pmk_path_modified": False,
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
                    "nlist_index=7112",
                    "n_type=0x0f",
                    "n_sect=1",
                    "n_desc=0x0000",
                    "__Z20apple80211setCUR_PMKP23IO80211SkywalkInterfaceP14apple80211_pmk",
                    "symbol_vmaddr=0xffffff80021c700b",
                    "symbol_vmaddr_end=0xffffff80021c7016",
                    "symbol_fileoff=0x20c700b",
                    "symbol_fileoff_end=0x20c7016",
                    "symbol_bytes=0x0b",
                    "next_nlist_index=7467",
                    "__Z26apple80211setDYNSAR_DETAILP23IO80211SkywalkInterfaceP24apple80211_dynsar_detail",
                    "body_sha256=9e4580f0175946d7624b2451e6ffda84a93e91e3e2d852d7a2c7998ee2d78576",
                    "0xe082280e",
                    "reads neither public argument",
                )
            ),
            "note_records_scope_and_nonclaims": all(
                token in note
                for token in (
                    "IOC 360",
                    "0xffffff80021c700b",
                    "0xe082280e",
                    "compile-time Tahoe-only guard",
                    "normal non-null public Tahoe SET",
                    "current-reference supersession",
                    "does not claim outer-null, GET, carrier/ABI, private virtual-route, PMK-owner, association, firmware, runtime-execution, or broader Tahoe behavior parity",
                    "No private carrier or selector is constructed or invoked",
                )
            ),
            "normal_nonnull_tahoe_public_route_returns_exact_fixed_status_unread": (
                "case APPLE80211_IOC_CUR_PMK:" in cur_pmk_route
                and "if (cmd == SIOCSA80211)" in cur_pmk_route
                and "return static_cast<IOReturn>(0xe082280e);" in tahoe_set_branch
                and "setCUR_PMK" not in tahoe_set_branch
                and "installExternalPmkLocked" not in tahoe_set_branch
                and "req->req_data->" not in tahoe_set_branch
                and "return kIOReturnSuccess;" not in tahoe_set_branch
            ),
            "pre26_set_and_get_route_remain_separate": (
                "return setCUR_PMK((struct apple80211_pmk *)req->req_data);" in pre26_set_branch
                and "static_cast<IOReturn>(0xe082280e)" not in pre26_set_branch
                and "return getCUR_PMK((struct apple80211_pmk *)req->req_data);" in cur_pmk_route
                and "return static_cast<IOReturn>(0xe00002c7);" in get_helper
            ),
            "null_fallback_remains_outside_this_layer": (
                null_guard in dispatcher
                and dispatcher.index(null_guard)
                < dispatcher.index("case APPLE80211_IOC_CUR_PMK:")
            ),
            "both_public_tahoe_ingress_paths_terminalize_cur_pmk": (
                "IOReturn ret = processApple80211Ioctl(normalizedCmd, req);" in bsd_bridge
                and "if (ret != kIOReturnUnsupported)\n            return ret;" in bsd_bridge
                and "routeTahoeSkywalkIoctl(interface, &req," in card_specific
                and "isSet ? SIOCSA80211 : SIOCGA80211" in card_specific
                and "if (ret != kIOReturnUnsupported)\n            return ret;" in card_specific
                and "case kIocCurPmk:" in routes
                and "case kIocCurPmk:" in cur_pmk_policy
                and "return true;" in cur_pmk_policy
            ),
            "cipher_and_plti_pmk_paths_remain_separate": (
                "return installExternalPmkLocked(key->key," in cipher_pmk
                and "\"CIPHER_KEY\"" in cipher_pmk
                and "return setCUR_PMK(" not in cipher_pmk
                and "return target->fProvider->deliverExternalPMK(" in plti_entry
                and "apple80211_key" in agent
                and "IOConnectCallMethod(" in agent
                and "kAirportItlwmUserClientMethod_DeliverPMK" in agent
                and "CUR_PMK" not in agent
                and "setCUR_PMK" not in plti_entry
                and "setCUR_PMK" not in plti_sink
                and "return a.rc;" in plti_sink
            ),
            "retained_virtual_carrier_and_abi_context_remain_explicit": (
                "virtual IOReturn setCUR_PMK(apple80211_pmk *) override;" in hpp
                and "setCUR_PMK(struct apple80211_pmk *pmk)" in set_helper
                and "return installExternalPmkLocked(pmk->apple_pmk_setter_source," in set_helper
                and cpp.count("return setCUR_PMK(") == 1
                and "#define APPLE80211_IOC_CUR_PMK                 360" in ioctl
                and "sizeof(struct apple80211_pmk) == 0x5c" in var
                and "0xffffff80021c700b" in infra
                and "does not invoke this virtual receiver" in infra
            ),
            "all_active_skywalk_source_phases_and_target_guard_are_explicit": (
                all(marker in project for marker in active_source_markers)
                and "#if __IO80211_TARGET >= __MAC_26_0" in cur_pmk_route
                and "#else" in cur_pmk_route
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
            "Skywalk public CUR_PMK alignment checks failed: " + ", ".join(failed)
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
            f"Skywalk public CUR_PMK alignment validation failed: {exc}",
            file=sys.stderr,
        )
        sys.exit(1)
