#!/usr/bin/env python3
"""Generate and verify legacy AWDL extension-state-machine evidence."""

import argparse
import hashlib
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/legacy_awdl_extension_state_machine_blind_success_quarantine_report.json"
NOTE = ROOT / "docs/reference/CR-507-legacy-awdl-extension-state-machine-blind-success-quarantine-20260715.md"
RAW = ROOT / "docs/reference/artifacts/awdl-extension-state-machine-parameters-public-wrapper-bootkc-current/raw.txt"
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
        "case APPLE80211_IOC_AWDL_EXTENSION_STATE_MACHINE_PARAMETERS:",
        "case APPLE80211_IOC_AWDL_SYNC_STATE:",
    )
    setter = section(
        cpp,
        "setAWDL_EXTENSION_STATE_MACHINE_PARAMETERS(OSObject *object, struct apple80211_awdl_extension_state_machine_parameter *data)",
        "getAWDL_SYNC_STATE(OSObject *object,",
    )
    getter = section(
        cpp,
        "getAWDL_EXTENSION_STATE_MACHINE_PARAMETERS(OSObject *object, struct apple80211_awdl_extension_state_machine_parameter *data)",
        "setAWDL_EXTENSION_STATE_MACHINE_PARAMETERS(OSObject *object,",
    )
    virtual_request = section(
        cpp,
        "apple80211VirtualRequest(UInt request_type, int request_number,",
        "setAWDL_PEER_TRAFFIC_REGISTRATION(OSObject *object,",
    )
    ioct_macro = section(hpp, "#define IOCTL(REQ_TYPE, REQ, DATA_TYPE)", "#define IOCTL_GET")
    carrier = section(
        ioctl,
        "struct apple80211_awdl_extension_state_machine_parameter",
        "struct apple80211_awdl_sync_state",
    )
    tahoe_marker = "F8E94CA52B9ABFE20081A3C4 /* Sources */ = {"
    tahoe_start = project.index(tahoe_marker)
    tahoe_phase = project[tahoe_start:project.index("\t\t};", tahoe_start)]

    return {
        "schema": "itlwm-legacy-awdl-extension-state-machine-blind-success-quarantine-v1",
        "source_base_revision": "83eac167d45bf9ae95747967eeb9196af9646ee7",
        "reference": {
            "bootkc_sha256": "eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d",
            "bootkc_uuid": "F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5",
            "embedded_kext_uuid": "8FB4B7F0-D656-3539-B8D6-C1327A50377C",
            "public_wrapper": "0xffffff80021c437f",
            "next_symbol": "0xffffff80021c43d4",
            "selector": 118,
            "gate_vtable_offset": "0xcc8",
            "tail_vtable_offset": "0x10b0",
            "owner_mismatch_status": "0xe082280e",
            "internal_admission": "0xffffff80021e4400:IO80211AWDLPeerManager::gMetaClass=0xffffff80023e1460:req+0x18=0x14:req+0x20_nonnull",
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
            "reference_raw_records_conditional_wrapper_and_admission_caveat": all(
                token in raw
                for token in (
                    "eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d",
                    "F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5",
                    "8FB4B7F0-D656-3539-B8D6-C1327A50377C",
                    "symbol_nlist=8248",
                    "symbol_vmaddr=0xffffff80021c437f",
                    "next_symbol_vmaddr=0xffffff80021c43d4",
                    "symbol_fileoff=0x20c437f",
                    "selector=118",
                    "+0xcc8",
                    "+0x10b0",
                    "0xe082280e",
                    "internal_selector_vmaddr=0xffffff80021e4400",
                    "internal_selector_metaclass=0xffffff80023e1460=IO80211AWDLPeerManager::gMetaClass",
                    "req+0x18 must equal 0x14",
                    "req+0x20 must be non-null",
                    "does not dereference or validate the carrier",
                    "must not be generalized into a claim that the",
                    "reference selector never validates the carrier",
                )
            ),
            "note_records_scope_reference_and_caveat": all(
                token in note
                for token in (
                    "IOC 118",
                    "0xffffff80021c437f",
                    "0x76",
                    "+0xcc8",
                    "+0x10b0",
                    "0xe082280e",
                    "0xffffff80021e4400",
                    "0x14",
                    "IO80211AWDLPeerManager",
                    "0xffffff80023e1460",
                    "does not claim Apple valid-input return-code parity",
                    "no deployment, radio, association, AWDL, P2P, scan, firmware, event, traffic, CCA, or runtime-execution claim",
                )
            ),
            "selector_carrier_header_and_legacy_route_remain": (
                "#define APPLE80211_IOC_AWDL_EXTENSION_STATE_MACHINE_PARAMETERS 118" in ioctl
                and "uint32_t    version;" in carrier
                and "uint32_t    unk1;" in carrier
                and "uint32_t    unk2;" in carrier
                and "uint32_t    unk3;" in carrier
                and "uint32_t    unk4;" in carrier
                and "FUNC_IOCTL(AWDL_EXTENSION_STATE_MACHINE_PARAMETERS, apple80211_awdl_extension_state_machine_parameter)" in hpp
                and "if (request_type != SIOCGA80211 && request_type != SIOCSA80211)" in virtual_request
                and "IOCTL(request_type, AWDL_EXTENSION_STATE_MACHINE_PARAMETERS, apple80211_awdl_extension_state_machine_parameter);" in dispatch
                and "if (REQ_TYPE == SIOCGA80211)" in ioct_macro
                and "ret = get##REQ" in ioct_macro
                and "ret = set##REQ" in ioct_macro
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
            "paired_getter_and_tahoe_absence_remain": (
                "data->version = APPLE80211_VERSION;" in getter
                and "return kIOReturnSuccess;" in getter
                and "AWDL_EXTENSION_STATE_MACHINE_PARAMETERS" not in skywalk
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
        raise ValueError("AWDL extension-state-machine checks failed: " + ", ".join(failed))
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
        print(f"AWDL extension-state-machine validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
