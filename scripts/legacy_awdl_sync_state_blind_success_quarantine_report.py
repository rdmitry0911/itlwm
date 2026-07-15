#!/usr/bin/env python3
"""Generate and verify legacy AWDL sync-state blind-success evidence."""

import argparse
import hashlib
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/legacy_awdl_sync_state_blind_success_quarantine_report.json"
NOTE = ROOT / "docs/reference/CR-513-legacy-awdl-sync-state-blind-success-quarantine-20260715.md"
RAW = ROOT / "docs/reference/artifacts/awdl-sync-state-public-wrapper-bootkc-current/raw.txt"
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
        "case APPLE80211_IOC_AWDL_SYNC_STATE:",
        "case APPLE80211_IOC_AWDL_SYNC_PARAMS:",
    )
    getter = section(
        cpp,
        "getAWDL_SYNC_STATE(OSObject *object, struct apple80211_awdl_sync_state *data)",
        "setAWDL_SYNC_STATE(OSObject *object,",
    )
    setter = section(
        cpp,
        "setAWDL_SYNC_STATE(OSObject *object, struct apple80211_awdl_sync_state *data)",
        "getAWDL_SYNC_PARAMS(OSObject *object,",
    )
    virtual_request = section(
        cpp,
        "apple80211VirtualRequest(UInt request_type, int request_number,",
        "setAWDL_PEER_TRAFFIC_REGISTRATION(OSObject *object,",
    )
    ioct_macro = section(hpp, "#define IOCTL(REQ_TYPE, REQ, DATA_TYPE)", "#define IOCTL_GET")
    carrier = section(
        ioctl,
        "struct apple80211_awdl_sync_state",
        "struct apple80211_awdl_sync_params",
    )
    tahoe_marker = "F8E94CA52B9ABFE20081A3C4 /* Sources */ = {"
    tahoe_start = project.index(tahoe_marker)
    tahoe_phase = project[tahoe_start:project.index("\t\t};", tahoe_start)]

    return {
        "schema": "itlwm-legacy-awdl-sync-state-blind-success-quarantine-v1",
        "source_base_revision": "faa4ec8a3888ac4de6cb37717a5ea42fb757479d",
        "reference": {
            "bootkc_sha256": "eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d",
            "bootkc_uuid": "F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5",
            "embedded_kext_uuid": "8FB4B7F0-D656-3539-B8D6-C1327A50377C",
            "public_wrapper": "0xffffff80021c45eb",
            "internal_selector": "0xffffff80021e4922",
            "terminal": "0xffffff800217b47e",
            "selector": 134,
            "internal_carrier_bytes": 24,
        },
        "local": {
            "packed_carrier_bytes": 8,
            "carrier_size_is_reference_parity": False,
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
            "reference_raw_records_public_internal_terminal_and_size_caveat": all(
                token in raw
                for token in (
                    "eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d",
                    "F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5",
                    "8FB4B7F0-D656-3539-B8D6-C1327A50377C",
                    "symbol_nlist=7607",
                    "symbol_vmaddr=0xffffff80021c45eb",
                    "next_symbol_vmaddr=0xffffff80021c4640",
                    "symbol_fileoff=0x20c45eb",
                    "selector=134",
                    "+0xcc8",
                    "+0x10e0",
                    "0xe082280e",
                    "internal_selector_vmaddr=0xffffff80021e4922",
                    "internal_selector_admission=req+0x18 must equal 0x18",
                    "IO80211AWDLPeerManager::gMetaClass",
                    "terminal_vmaddr=0xffffff800217b47e",
                    "terminal_bounded_range=0x3ce",
                    "carrier first dword=1",
                    "carrier+0x4",
                    "0x08 bytes",
                    "requires 0x18",
                    "does not prove that the public dynamic +0x10e0 tail resolves to that terminal",
                    "must not be generalized into a claim that the reference selector never validates the",
                    "carrier",
                )
            ),
            "note_records_scope_reference_and_size_divergence": all(
                token in note
                for token in (
                    "IOC 134",
                    "0xffffff80021c45eb",
                    "0x86",
                    "+0xcc8",
                    "+0x10e0",
                    "0xe082280e",
                    "0xffffff80021e4922",
                    "0x18",
                    "0x08",
                    "0xffffff800217b47e",
                    "does not establish full terminal policy",
                    "does not claim Apple valid-input return-code parity",
                    "does not claim a global local AWDL or backend absence",
                    "no deployment, radio, association, AWDL, P2P, scan, firmware, event, traffic, CCA, or runtime-execution claim",
                )
            ),
            "selector_carrier_header_route_and_getter_remain": (
                "#define APPLE80211_IOC_AWDL_SYNC_STATE 134" in ioctl
                and "uint32_t    version;" in carrier
                and "uint32_t    state;" in carrier
                and "__attribute__((packed))" in carrier
                and "FUNC_IOCTL(AWDL_SYNC_STATE, apple80211_awdl_sync_state)" in hpp
                and "if (request_type != SIOCGA80211 && request_type != SIOCSA80211)" in virtual_request
                and "IOCTL(request_type, AWDL_SYNC_STATE, apple80211_awdl_sync_state);" in dispatch
                and "if (REQ_TYPE == SIOCGA80211)" in ioct_macro
                and "ret = get##REQ" in ioct_macro
                and "ret = set##REQ" in ioct_macro
                and "data->version = APPLE80211_VERSION;" in getter
                and "data->state = awdlSyncState;" in getter
                and "return kIOReturnSuccess;" in getter
            ),
            "setter_is_unread_and_fails_closed": (
                "(void)object;" in setter
                and "(void)data;" in setter
                and "return kIOReturnUnsupported;" in setter
                and "return kIOReturnSuccess;" not in setter
                and "data->" not in setter
                and "object->" not in setter
                and "awdlSyncState" not in setter
                and "memcpy" not in setter
                and "memset" not in setter
            ),
            "local_carrier_size_divergence_is_preserved": (
                "uint32_t    state;" in carrier
                and "req+0x18 must equal 0x18" in raw
                and "0x08 bytes" in raw
                and "return kIOReturnUnsupported;" in setter
            ),
            "paired_getter_and_tahoe_absence_remain": (
                "data->state = awdlSyncState;" in getter
                and "AWDL_SYNC_STATE" not in skywalk
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
        raise ValueError("AWDL sync-state checks failed: " + ", ".join(failed))
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
        print(f"AWDL sync-state validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
