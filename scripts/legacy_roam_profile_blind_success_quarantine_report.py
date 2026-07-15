#!/usr/bin/env python3
"""Generate and verify legacy V1 ROAM_PROFILE blind-success quarantine evidence."""

import argparse
import hashlib
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/legacy_roam_profile_blind_success_quarantine_report.json"
NOTE = ROOT / "docs/reference/CR-522-legacy-roam-profile-blind-success-quarantine-20260715.md"
RAW = ROOT / "docs/reference/artifacts/legacy-roam-profile-public-dispatch-bootkc-current/raw.txt"
RAW_MANIFEST = RAW.with_name("SHA256SUMS.txt")
CPP = ROOT / "AirportItlwm/AirportSTAIOCTL.cpp"
HPP = ROOT / "AirportItlwm/AirportItlwm.hpp"
MAIN = ROOT / "AirportItlwm/AirportItlwm.cpp"
IOCTL = ROOT / "include/Airport/apple80211_ioctl.h"
SKYWALK = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.cpp"
PROJECT = ROOT / "itlwm.xcodeproj/project.pbxproj"


def section(source, begin, end):
    start = source.index(begin)
    return source[start:source.index(end, start)]


def report():
    cpp = CPP.read_text(encoding="utf-8")
    hpp = HPP.read_text(encoding="utf-8")
    main = MAIN.read_text(encoding="utf-8")
    ioctl = IOCTL.read_text(encoding="utf-8")
    skywalk = SKYWALK.read_text(encoding="utf-8")
    project = PROJECT.read_text(encoding="utf-8")
    note = " ".join(NOTE.read_text(encoding="utf-8").split())
    raw = RAW.read_text(encoding="utf-8")
    manifest = RAW_MANIFEST.read_text(encoding="utf-8")
    raw_digest = hashlib.sha256(RAW.read_bytes()).hexdigest()

    legacy_route = section(
        cpp,
        "case APPLE80211_IOC_ROAM_PROFILE:",
        "case APPLE80211_IOC_WOW_PARAMETERS:",
    )
    legacy_getter = section(
        cpp,
        "getROAM_PROFILE(OSObject *object, struct apple80211_roam_profile_band_data *data)",
        "setROAM_PROFILE(OSObject *object, struct apple80211_roam_profile_band_data *data)",
    )
    legacy_setter = section(
        cpp,
        "setROAM_PROFILE(OSObject *object, struct apple80211_roam_profile_band_data *data)",
        "getBTCOEX_CONFIG(OSObject *object, struct apple80211_btc_config_data *data)",
    )
    ioct_macro = section(hpp, "#define IOCTL(REQ_TYPE, REQ, DATA_TYPE)", "#define IOCTL_GET")
    carrier = section(
        ioctl,
        "struct apple80211_roam_profile_band_data",
        "struct apple80211_ie_data",
    )
    teardown = section(main, "if (roamProfile != NULL)", "if (btcProfile != NULL)")
    tahoe_marker = "F8E94CA52B9ABFE20081A3C4 /* Sources */ = {"
    tahoe_start = project.index(tahoe_marker)
    tahoe_phase = project[tahoe_start:project.index("\t\t};", tahoe_start)]

    return {
        "schema": "itlwm-legacy-roam-profile-blind-success-quarantine-v1",
        "source_base_revision": "c4a7b2f7a4a6b0e3ea3b5b6eff3b153a603b5c9b",
        "reference": {
            "bootkc_sha256": "eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d",
            "bootkc_uuid": "F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5",
            "embedded_kext_uuid": "8FB4B7F0-D656-3539-B8D6-C1327A50377C",
            "selector": "0xd8",
            "public_wrapper": "0xffffff80021c4f19",
            "next_symbol": "0xffffff80021c4f6e",
            "gate_vtable_offset": "0xcc8",
            "terminal_vtable_offset": "0x1178",
            "owner_absent_status": "0xe082280e",
            "body_sha256": "17b3fd4216f0fd8371173ee1e29a4b66fade12d030c406229b0eaadd5739d0bc",
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
            "reference_raw_has_current_gate_metacast_dynamic_slot_topology": all(
                token in raw
                for token in (
                    "eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d",
                    "F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5",
                    "8FB4B7F0-D656-3539-B8D6-C1327A50377C",
                    "nlist_index=7410",
                    "n_type=0x0f",
                    "n_sect=1",
                    "n_desc=0x0000",
                    "__Z25apple80211setROAM_PROFILEP23IO80211SkywalkInterfaceP33apple80211_roam_profile_all_bands",
                    "symbol_vmaddr=0xffffff80021c4f19",
                    "symbol_vmaddr_end=0xffffff80021c4f6e",
                    "symbol_fileoff=0x20c4f19",
                    "symbol_fileoff_end=0x20c4f6e",
                    "symbol_bytes=0x55",
                    "next_nlist_index=7530",
                    "__Z27apple80211setAWDL_OPER_MODEP23IO80211SkywalkInterfacePv",
                    "next_symbol_vmaddr=0xffffff80021c4f6e",
                    "body_sha256=17b3fd4216f0fd8371173ee1e29a4b66fade12d030c406229b0eaadd5739d0bc",
                    "selector 0xd8",
                    "virtual +0xcc8",
                    "OSMetaClassBase::safeMetaCast",
                    "virtual +0x1178",
                    "0xe082280e",
                    "does not establish a null-input contract",
                    "or a successful-terminal status",
                )
            ),
            "note_records_scope_and_nonclaims": all(
                token in note
                for token in (
                    "IOC 216 / 0xd8",
                    "0xffffff80021c4f19",
                    "0xe082280e",
                    "does not claim null-input, valid-input return, carrier layout, ABI, user-client, GET, roaming-policy, or Tahoe behavior parity",
                    "not Apple valid-input return-code parity",
                    "no deployment, radio, association, APSTA, AWDL, P2P, scan, firmware, event, traffic, CCA, or runtime-execution claim",
                )
            ),
            "legacy_selector_216_carrier_route_and_getter_remain": (
                "#define APPLE80211_IOC_ROAM_PROFILE 216" in ioctl
                and "uint32_t    version;" in carrier
                and "uint32_t    profile_cnt;" in carrier
                and "profiles[4];" in carrier
                and "__attribute__((packed))" in carrier
                and "static_assert(sizeof(struct apple80211_roam_profile_band_data) == 76" in ioctl
                and "FUNC_IOCTL(ROAM_PROFILE, apple80211_roam_profile_band_data)" in hpp
                and "IOCTL(request_type, ROAM_PROFILE, apple80211_roam_profile_band_data);" in legacy_route
                and "if (REQ_TYPE == SIOCGA80211)" in ioct_macro
                and "ret = get##REQ" in ioct_macro
                and "ret = set##REQ" in ioct_macro
                and "if (roamProfile == NULL)" in legacy_getter
                and "memcpy(data, roamProfile, sizeof(struct apple80211_roam_profile_band_data));" in legacy_getter
                and "return kIOReturnSuccess;" in legacy_getter
            ),
            "setter_is_unread_fail_closed_without_cache_mutation": (
                "(void)object;" in legacy_setter
                and "(void)data;" in legacy_setter
                and "return kIOReturnUnsupported;" in legacy_setter
                and "return kIOReturnSuccess;" not in legacy_setter
                and "roamProfile" not in legacy_setter
                and "IOMalloc" not in legacy_setter
                and "IOFree" not in legacy_setter
                and "memcpy" not in legacy_setter
                and "data->" not in legacy_setter
                and "object->" not in legacy_setter
            ),
            "v1_readback_pointer_and_teardown_remain_separate": (
                "uint8_t *roamProfile;" in hpp
                and cpp.count("roamProfile") == 2
                and main.count("roamProfile") == 3
                and "IOFree(roamProfile, sizeof(struct apple80211_roam_profile_band_data));" in teardown
                and "roamProfile = NULL;" in teardown
            ),
            "no_legacy_v1_roam_owner_or_transport_is_introduced": all(
                token not in (cpp + hpp + main)
                for token in (
                    '"roam_prof"',
                    "AppleBCMWLANRoamAdapter",
                    "sendIOVarSet",
                    "runIOVarSet",
                )
            ),
            "tahoe_source_phase_remains_separate": (
                "AirportSTAIOCTL.cpp in Sources" not in tahoe_phase
                and "AirportItlwmSkywalkInterface.cpp in Sources" in tahoe_phase
                and "setROAM_PROFILE(apple80211_roam_profile_all_bands *)" in skywalk
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
        raise ValueError("legacy ROAM_PROFILE quarantine checks failed: " + ", ".join(failed))
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
        print(f"legacy ROAM_PROFILE quarantine validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
