#!/usr/bin/env python3
"""Generate and verify legacy AWDL OOB-auto-request blind-success evidence."""

import argparse
import hashlib
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/legacy_awdl_oob_auto_request_blind_success_quarantine_report.json"
NOTE = ROOT / "docs/reference/CR-510-legacy-awdl-oob-auto-request-blind-success-quarantine-20260715.md"
RAW = ROOT / "docs/reference/artifacts/awdl-oob-auto-request-public-wrapper-bootkc-current/raw.txt"
RAW_MANIFEST = RAW.with_name("SHA256SUMS.txt")
CPP = ROOT / "AirportItlwm/AirportVirtualIOCTL.cpp"
HPP = ROOT / "AirportItlwm/AirportItlwm.hpp"
IOCTL = ROOT / "include/Airport/apple80211_ioctl.h"
SKYWALK = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.cpp"
PROJECT = ROOT / "itlwm.xcodeproj/project.pbxproj"


def section(source, begin, end):
    start = source.index(begin)
    return source[start:source.index(end, start)]


def report():
    cpp = CPP.read_text(encoding="utf-8")
    hpp = HPP.read_text(encoding="utf-8")
    ioctl = IOCTL.read_text(encoding="utf-8")
    skywalk = SKYWALK.read_text(encoding="utf-8")
    project = PROJECT.read_text(encoding="utf-8")
    note = " ".join(NOTE.read_text(encoding="utf-8").split())
    raw = RAW.read_text(encoding="utf-8")
    manifest = RAW_MANIFEST.read_text(encoding="utf-8")
    raw_digest = hashlib.sha256(RAW.read_bytes()).hexdigest()

    dispatch = section(
        cpp,
        "case APPLE80211_IOC_AWDL_OOB_AUTO_REQUEST:",
        "case APPLE80211_IOC_IE:",
    )
    setter_start = cpp.index(
        "setAWDL_OOB_AUTO_REQUEST(OSObject *object, struct apple80211_awdl_oob_request *data)"
    )
    setter = cpp[setter_start:]
    virtual_request = section(
        cpp,
        "apple80211VirtualRequest(UInt request_type, int request_number,",
        "setAWDL_PEER_TRAFFIC_REGISTRATION(OSObject *object,",
    )
    ioct_macro = section(hpp, "#define IOCTL(REQ_TYPE, REQ, DATA_TYPE)", "#define IOCTL_GET")
    ioct_set_macro = section(hpp, "#define IOCTL_SET(REQ_TYPE, REQ, DATA_TYPE)", "#define FUNC_IOCTL")
    carrier = section(
        ioctl,
        "struct apple80211_awdl_oob_request",
        "//Roam:",
    )
    tahoe_marker = "F8E94CA52B9ABFE20081A3C4 /* Sources */ = {"
    tahoe_start = project.index(tahoe_marker)
    tahoe_phase = project[tahoe_start:project.index("\t\t};", tahoe_start)]

    return {
        "schema": "itlwm-legacy-awdl-oob-auto-request-blind-success-quarantine-v1",
        "source_base_revision": "198db9d107718e171298dfb3bc5978aca1483402",
        "reference": {
            "bootkc_sha256": "eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d",
            "bootkc_uuid": "F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5",
            "embedded_kext_uuid": "8FB4B7F0-D656-3539-B8D6-C1327A50377C",
            "public_wrapper": "0xffffff80021c50a4",
            "next_symbol": "0xffffff80021c50f9",
            "selector": 225,
            "gate_vtable_offset": "0xcc8",
            "tail_vtable_offset": "0x1148",
            "owner_mismatch_status": "0xe082280e",
            "terminal_linkage_is_resolved": False,
            "separate_unlinked_peer_manager_stub": "0xffffff800217a4f0",
        },
        "scope": {
            "deployment": False,
            "radio_or_association": False,
            "runtime_selector_invocation": False,
            "tahoe_source_modified_by_this_layer": False,
            "traffic": False,
        },
        "checks": {
            "reference_raw_manifest_matches": manifest == f"{raw_digest}  raw.txt\n",
            "reference_raw_records_conditional_wrapper_and_terminal_caveat": all(
                token in raw
                for token in (
                    "eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d",
                    "F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5",
                    "8FB4B7F0-D656-3539-B8D6-C1327A50377C",
                    "symbol_nlist=7980",
                    "symbol_vmaddr=0xffffff80021c50a4",
                    "next_symbol_vmaddr=0xffffff80021c50f9",
                    "symbol_fileoff=0x20c50a4",
                    "selector=225",
                    "+0xcc8",
                    "+0x1148",
                    "0xe082280e",
                    "IO80211AWDLProtocol",
                    "0xffffff80023e0f68",
                    "does not dereference or validate the carrier",
                    "peer_manager_named_symbol_vmaddr=0xffffff800217a4f0",
                    "peer_manager_named_symbol_raw=55 48 89 e5 31 c0 5d c3",
                    "Terminal linkage is intentionally unresolved",
                    "not proven to be its target",
                    "must not be generalized into a claim that the reference selector",
                    "never validates the carrier",
                )
            ),
            "note_records_scope_reference_and_terminal_caveat": all(
                token in note
                for token in (
                    "IOC 225",
                    "SET-only",
                    "0xffffff80021c50a4",
                    "0xe1",
                    "+0xcc8",
                    "+0x1148",
                    "0xe082280e",
                    "0xffffff800217a4f0",
                    "not proven",
                    "does not establish the terminal method",
                    "does not claim Apple valid-input return-code parity",
                    "no deployment, radio, association, AWDL, P2P, scan, firmware, event, traffic, CCA, or runtime-execution claim",
                )
            ),
            "selector_carrier_set_only_route_and_get_error_remain": (
                "#define APPLE80211_IOC_AWDL_OOB_AUTO_REQUEST 225" in ioctl
                and "uint16_t    data_len;" in carrier
                and "uint8_t     data[1782];" in carrier
                and "__attribute__((packed))" in carrier
                and "FUNC_IOCTL_SET(AWDL_OOB_AUTO_REQUEST, apple80211_awdl_oob_request)" in hpp
                and "if (request_type != SIOCGA80211 && request_type != SIOCSA80211)" in virtual_request
                and "IOReturn ret = kIOReturnError;" in virtual_request
                and "IOCTL_SET(request_type, AWDL_OOB_AUTO_REQUEST, apple80211_awdl_oob_request);" in dispatch
                and "if (REQ_TYPE == SIOCSA80211)" in ioct_set_macro
                and "ret = set##REQ" in ioct_set_macro
                and "getAWDL_OOB_AUTO_REQUEST" not in hpp
                and "getAWDL_OOB_AUTO_REQUEST" not in cpp
            ),
            "setter_is_unread_and_fails_closed": (
                "(void)object;" in setter
                and "(void)data;" in setter
                and "return kIOReturnUnsupported;" in setter
                and "return kIOReturnSuccess;" not in setter
                and "data->" not in setter
                and "object->" not in setter
                and "memcpy" not in setter
                and "memset" not in setter
            ),
            "tahoe_absence_remains": (
                "AWDL_OOB_AUTO_REQUEST" not in skywalk
                and "AirportVirtualIOCTL.cpp" not in tahoe_phase
                and "AirportItlwmSkywalkInterface.cpp in Sources" in tahoe_phase
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
        raise ValueError("AWDL OOB-auto-request checks failed: " + ", ".join(failed))
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
        print(f"AWDL OOB-auto-request validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
