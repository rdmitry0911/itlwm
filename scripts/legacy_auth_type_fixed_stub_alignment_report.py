#!/usr/bin/env python3
"""Generate and verify legacy V1 AUTH_TYPE fixed-stub alignment evidence."""

import argparse
import hashlib
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/legacy_auth_type_fixed_stub_alignment_report.json"
NOTE = ROOT / "docs/reference/CR-517-legacy-auth-type-fixed-stub-alignment-20260715.md"
RAW = ROOT / "docs/reference/artifacts/legacy-auth-type-public-fixed-stub-bootkc-current/raw.txt"
RAW_MANIFEST = RAW.with_name("SHA256SUMS.txt")
CPP = ROOT / "AirportItlwm/AirportSTAIOCTL.cpp"
HPP = ROOT / "AirportItlwm/AirportItlwm.hpp"
IOCTL = ROOT / "include/Airport/apple80211_ioctl.h"
SKYWALK = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.cpp"
SKYWALK_HPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.hpp"
PROJECT = ROOT / "itlwm.xcodeproj/project.pbxproj"


def section(source, begin, end):
    start = source.index(begin)
    return source[start:source.index(end, start)]


def report():
    cpp = CPP.read_text(encoding="utf-8")
    hpp = HPP.read_text(encoding="utf-8")
    ioctl = IOCTL.read_text(encoding="utf-8")
    skywalk = SKYWALK.read_text(encoding="utf-8")
    skywalk_hpp = SKYWALK_HPP.read_text(encoding="utf-8")
    project = PROJECT.read_text(encoding="utf-8")
    note = " ".join(NOTE.read_text(encoding="utf-8").split())
    raw = RAW.read_text(encoding="utf-8")
    manifest = RAW_MANIFEST.read_text(encoding="utf-8")
    raw_digest = hashlib.sha256(RAW.read_bytes()).hexdigest()

    legacy_dispatch = section(
        cpp,
        "case APPLE80211_IOC_AUTH_TYPE:  // 2",
        "case APPLE80211_IOC_CHANNEL:  // 4",
    )
    legacy_getter = section(
        cpp,
        "getAUTH_TYPE(OSObject *object, struct apple80211_authtype_data *ad)",
        "setAUTH_TYPE(OSObject *object, struct apple80211_authtype_data *ad)",
    )
    legacy_setter = section(
        cpp,
        "setAUTH_TYPE(OSObject *object, struct apple80211_authtype_data *ad)",
        "setCIPHER_KEY(OSObject *object, struct apple80211_key *key)",
    )
    ioct_macro = section(hpp, "#define IOCTL(REQ_TYPE, REQ, DATA_TYPE)", "#define IOCTL_GET")
    carrier = section(
        ioctl,
        "struct apple80211_authtype_data",
        "struct apple80211_sup_channel_data",
    )
    tahoe_route = section(
        skywalk,
        "case APPLE80211_IOC_AUTH_TYPE:",
        "case APPLE80211_IOC_HOST_AP_MODE:",
    )
    tahoe_setter = section(
        skywalk,
        "setAUTH_TYPE(struct apple80211_authtype_data *ad)",
        "setCIPHER_KEY(struct apple80211_key *key)",
    )
    tahoe_marker = "F8E94CA52B9ABFE20081A3C4 /* Sources */ = {"
    tahoe_start = project.index(tahoe_marker)
    tahoe_phase = project[tahoe_start:project.index("\t\t};", tahoe_start)]

    return {
        "schema": "itlwm-legacy-auth-type-fixed-stub-alignment-v1",
        "source_base_revision": "8cb60985d5434a127196f55099e78e7ecdc33647",
        "reference": {
            "bootkc_sha256": "eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d",
            "bootkc_uuid": "F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5",
            "embedded_kext_uuid": "8FB4B7F0-D656-3539-B8D6-C1327A50377C",
            "fixed_status": "0xe082280e",
            "public_wrapper": "0xffffff80021c3520",
            "next_symbol": "0xffffff80021c352b",
            "body_sha256": "9e4580f0175946d7624b2451e6ffda84a93e91e3e2d852d7a2c7998ee2d78576",
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
            "reference_raw_has_exact_unread_fixed_stub": all(
                token in raw
                for token in (
                    "eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d",
                    "F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5",
                    "8FB4B7F0-D656-3539-B8D6-C1327A50377C",
                    "nlist_index=7204",
                    "n_type=0x0f",
                    "n_sect=1",
                    "n_desc=0x0000",
                    "__Z22apple80211setAUTH_TYPEP23IO80211SkywalkInterfaceP24apple80211_authtype_data",
                    "symbol_vmaddr=0xffffff80021c3520",
                    "symbol_vmaddr_end=0xffffff80021c352b",
                    "symbol_fileoff=0x20c3520",
                    "symbol_fileoff_end=0x20c352b",
                    "symbol_bytes=0x0b",
                    "next_nlist_index=7261",
                    "__Z23apple80211setCIPHER_KEYP23IO80211SkywalkInterfaceP14apple80211_key",
                    "next_symbol_vmaddr=0xffffff80021c352b",
                    "raw=55 48 89 e5 b8 0e 28 82 e0 5d c3",
                    "body_sha256=9e4580f0175946d7624b2451e6ffda84a93e91e3e2d852d7a2c7998ee2d78576",
                    "mov eax,0xe082280e",
                    "unread fixed public stub",
                    "selector load, gate, metacast, dynamic tail, static-handler or terminal",
                    "not be relabelled as kIOReturnUnsupported",
                )
            ),
            "note_records_scope_and_nonclaims": all(
                token in note
                for token in (
                    "IOC 2",
                    "0xffffff80021c3520",
                    "0xe082280e",
                    "does not claim carrier, null-input, ABI, user-client, GET, association, or Tahoe behavior parity",
                    "does not claim Apple historical behavior",
                    "no deployment, radio, association, APSTA, AWDL, P2P, scan, firmware, event, traffic, CCA, or runtime-execution claim",
                )
            ),
            "legacy_selector_carrier_and_route_remain": (
                "#define APPLE80211_IOC_AUTH_TYPE                2" in ioctl
                and "u_int32_t    version;" in carrier
                and "u_int32_t    authtype_lower;" in carrier
                and "u_int32_t    authtype_upper;" in carrier
                and "FUNC_IOCTL(AUTH_TYPE, apple80211_authtype_data)" in hpp
                and "if (request_type != SIOCGA80211 && request_type != SIOCSA80211)" in cpp
                and "IOCTL(request_type, AUTH_TYPE, apple80211_authtype_data);" in legacy_dispatch
                and "if (REQ_TYPE == SIOCGA80211)" in ioct_macro
                and "ret = get##REQ" in ioct_macro
                and "ret = set##REQ" in ioct_macro
            ),
            "legacy_setter_matches_exact_fixed_status_without_reads_or_cache_write": (
                "(void)object;" in legacy_setter
                and "(void)ad;" in legacy_setter
                and "static_cast<IOReturn>(0xe082280e)" in legacy_setter
                and "return kIOReturnSuccess;" not in legacy_setter
                and "return kIOReturnUnsupported;" not in legacy_setter
                and "ad->" not in legacy_setter
                and "object->" not in legacy_setter
                and "current_authtype_lower" not in legacy_setter
                and "current_authtype_upper" not in legacy_setter
                and "memcpy" not in legacy_setter
                and "memset" not in legacy_setter
            ),
            "legacy_getter_and_v1_readback_fields_remain_separate": (
                "ad->version = APPLE80211_VERSION;" in legacy_getter
                and "ad->authtype_lower = current_authtype_lower;" in legacy_getter
                and "ad->authtype_upper = current_authtype_upper;" in legacy_getter
                and "return kIOReturnSuccess;" in legacy_getter
                and "u_int32_t current_authtype_lower;" in hpp
                and "u_int32_t current_authtype_upper;" in hpp
            ),
            "tahoe_auth_route_state_and_owner_context_remain_separate": (
                "if (cmd == SIOCGA80211)" in tahoe_route
                and "return getAUTH_TYPE((apple80211_authtype_data *)req->req_data);" in tahoe_route
                and "if (cmd == SIOCSA80211)" in tahoe_route
                and "return setAUTH_TYPE((apple80211_authtype_data *)req->req_data);" in tahoe_route
                and "return kIOReturnUnsupported;" in tahoe_route
                and "current_authtype_lower = ad->authtype_lower;" in tahoe_setter
                and "current_authtype_upper = ad->authtype_upper;" in tahoe_setter
                and "u_int32_t current_authtype_lower;" in skywalk_hpp
                and "u_int32_t current_authtype_upper;" in skywalk_hpp
                and "tahoeSeedBssManagerAssociatedAuthType(" in skywalk
            ),
            "legacy_source_is_absent_from_tahoe_phase": (
                "AirportSTAIOCTL.cpp in Sources" not in tahoe_phase
                and "AirportItlwmV2.cpp in Sources" in tahoe_phase
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
        raise ValueError("legacy AUTH_TYPE fixed-stub checks failed: " + ", ".join(failed))
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
        print(f"legacy AUTH_TYPE fixed-stub validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
