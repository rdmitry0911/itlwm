#!/usr/bin/env python3
"""Generate and verify Skywalk public COUNTERMEASURES GET fixed-stub evidence."""

import argparse
import hashlib
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/skywalk_public_countermeasures_get_fixed_stub_alignment_report.json"
NOTE = ROOT / "docs/reference/CR-543-skywalk-public-countermeasures-get-fixed-stub-alignment-20260715.md"
RAW = ROOT / "docs/reference/artifacts/skywalk-countermeasures-get-public-fixed-stub-bootkc-current/raw.txt"
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
    cpp = CPP.read_text(encoding="utf-8")
    v2 = V2.read_text(encoding="utf-8")
    v1 = V1.read_text(encoding="utf-8")
    virtual = VIRTUAL.read_text(encoding="utf-8")
    routes = ROUTES.read_text(encoding="utf-8")
    ioctl = IOCTL.read_text(encoding="utf-8")
    project = PROJECT.read_text(encoding="utf-8")
    note = " ".join(NOTE.read_text(encoding="utf-8").split())
    raw = RAW.read_text(encoding="utf-8")
    manifest = RAW_MANIFEST.read_text(encoding="utf-8")
    raw_digest = hashlib.sha256(RAW.read_bytes()).hexdigest()
    bsd = section(cpp, "processBSDCommand(ifnet_t interface, UInt cmd, void *data)", "processApple80211Ioctl(UInt cmd, apple80211req *req)")
    dispatcher = section(cpp, "processApple80211Ioctl(UInt cmd, apple80211req *req)", "IOReturn AirportItlwmSkywalkInterface::\ngetAUTH_TYPE")
    macro_case = "#if __IO80211_TARGET >= __MAC_26_0\n        case APPLE80211_IOC_COUNTERMEASURES:"
    counter = section(dispatcher, macro_case, "case APPLE80211_IOC_POWER:")
    pre26_dispatcher = dispatcher.replace(counter, "")
    card = section(v2, "SInt32 AirportItlwm::handleCardSpecific(", "IOReturn AirportItlwm::enableAdapter")
    markers = (
        "F8A028722A4A7FE100C6DE90 /* AirportItlwmSkywalkInterface.cpp in Sources */",
        "F8D94CD22B9ABFE20081A3C4 /* AirportItlwmSkywalkInterface.cpp in Sources */",
        "F8E94CD22B9ABFE20081A3C4 /* AirportItlwmSkywalkInterface.cpp in Sources */",
    )
    return {
        "schema": "itlwm-skywalk-public-countermeasures-get-fixed-stub-alignment-v2",
        "source_base_revision": "d195a271ea325f91a71d69193a6a9fd10d8c3ecc",
        "reference": {
            "bootkc_sha256": "eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d",
            "bootkc_uuid": "F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5",
            "embedded_kext_uuid": "8FB4B7F0-D656-3539-B8D6-C1327A50377C",
            "nlist_index": 7574,
            "public_wrapper": "0xffffff80021bea50",
            "next_symbol": "0xffffff80021bea5b",
            "fixed_status": "0xe082280e",
            "body_sha256": "9e4580f0175946d7624b2451e6ffda84a93e91e3e2d852d7a2c7998ee2d78576",
        },
        "scope": {
            "public_nonnull_request_object_tahoe_bsd_get_only": True,
            "carrier_is_not_observed": True,
            "countermeasures_set_behavior_is_outside_this_get_evidence": True,
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
                "symbol=__Z28apple80211getCOUNTERMEASURESP23IO80211SkywalkInterfacePv",
                "nlist_index=7574", "n_type=0x0f", "n_sect=1", "n_desc=0x0000",
                "symbol_vmaddr=0xffffff80021bea50", "symbol_vmaddr_end=0xffffff80021bea5b",
                "symbol_fileoff=0x20bea50", "symbol_fileoff_end=0x20bea5b", "symbol_bytes=0x0b",
                "next_nlist_index=7499", "__Z27apple80211getFRAG_THRESHOLDP23IO80211SkywalkInterfacePv",
                "next_symbol_vmaddr=0xffffff80021bea5b", "body_sha256=9e4580f0175946d7624b2451e6ffda84a93e91e3e2d852d7a2c7998ee2d78576",
                "0xe082280e", "reads neither public argument",
            )),
            "note_records_scope_and_nonclaims": all(token in note for token in (
                "IOC 30", "0xffffff80021bea50", "0xe082280e", "compile-time Tahoe-only case",
                "selector remains absent from the pre-26 switch", "card-specific route has no COUNTERMEASURES entry",
                "SET behavior is separately aligned and documented by CR-588",
                "does not independently prove SET behavior",
                "does not claim outer-null dispatch behavior, a COUNTERMEASURES carrier ABI, security-countermeasure behavior, V1, Virtual IOCTL, card-specific behavior, firmware, runtime-execution, radio, association, traffic, or broader Tahoe behavior parity",
                "No private carrier or selector is constructed or invoked",
            )),
            "public_tahoe_get_returns_exact_unread_status": (
                dispatcher.count("case APPLE80211_IOC_COUNTERMEASURES:") == 1
                and counter.startswith(macro_case)
                and "SIOCGA80211" in counter
                and "return static_cast<IOReturn>(0xe082280e);" in counter
                and "req->req_data" not in counter
                and "return kIOReturnSuccess;" not in counter
            ),
            "tahoe_target_guard_and_pre26_boundary_remain_explicit": (
                "return kIOReturnUnsupported;" in counter
                and "#endif // __IO80211_TARGET >= __MAC_26_0" in counter
                and "case APPLE80211_IOC_COUNTERMEASURES:" not in pre26_dispatcher
            ),
            "outer_null_and_bsd_boundaries_remain_explicit": (
                "if (req == NULL)\n        return kIOReturnUnsupported;" in dispatcher
                and dispatcher.index("if (req == NULL)\n        return kIOReturnUnsupported;") < dispatcher.index(macro_case)
                and "data != NULL" in bsd
                and "IOReturn ret = processApple80211Ioctl(normalizedCmd, req);" in bsd
                and "if (ret != kIOReturnUnsupported)\n            return ret;" in bsd
            ),
            "legacy_virtual_and_card_boundaries_remain_separate": (
                "APPLE80211_IOC_COUNTERMEASURES" not in v1
                and "APPLE80211_IOC_COUNTERMEASURES" not in virtual
                and "kIocCountermeasures" not in routes
                and "APPLE80211_IOC_COUNTERMEASURES" not in routes
                and "default:\n            return false;" in routes
                and "routeTahoeSkywalkIoctl(interface, &req," in card
            ),
            "selector_is_declared_without_local_carrier_access": (
                "#define APPLE80211_IOC_COUNTERMEASURES           30" in ioctl
                and "req->req_data" not in counter
            ),
            "all_active_skywalk_source_phases_and_target_guard_are_explicit": (
                all(marker in project for marker in markers)
                and counter.startswith(macro_case)
                and "#endif // __IO80211_TARGET >= __MAC_26_0" in counter
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
        raise ValueError("Skywalk public COUNTERMEASURES GET alignment validation failed: " + ", ".join(failed))
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
        print(f"Skywalk public COUNTERMEASURES GET alignment validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
