#!/usr/bin/env python3
"""Generate and verify Skywalk public CIPHER_KEY GET fixed-stub evidence."""

import argparse
import hashlib
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/skywalk_public_cipher_key_get_fixed_stub_alignment_report.json"
NOTE = ROOT / "docs/reference/CR-528-skywalk-public-cipher-key-get-fixed-stub-alignment-20260715.md"
RAW = ROOT / "docs/reference/artifacts/skywalk-cipher-key-get-public-fixed-stub-bootkc-current/raw.txt"
RAW_MANIFEST = RAW.with_name("SHA256SUMS.txt")
CPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.cpp"
V2 = ROOT / "AirportItlwm/AirportItlwmV2.cpp"
V1 = ROOT / "AirportItlwm/AirportSTAIOCTL.cpp"
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
    cipher_route = section(
        dispatcher,
        "case APPLE80211_IOC_CIPHER_KEY:",
        "case APPLE80211_IOC_STATION_LIST:",
    )
    tahoe_get_branch = section(
        cipher_route,
        "#if __IO80211_TARGET >= __MAC_26_0",
        "#endif // __IO80211_TARGET >= __MAC_26_0",
    )
    after_tahoe_get_branch = cipher_route[
        cipher_route.index("#endif // __IO80211_TARGET >= __MAC_26_0")
        + len("#endif // __IO80211_TARGET >= __MAC_26_0"):
    ]
    cipher_helper = section(
        cpp,
        "setCIPHER_KEY(struct apple80211_key *key)",
        "getPHY_MODE(struct apple80211_phymode_data *pd)",
    )
    card_specific = section(
        v2,
        "SInt32 AirportItlwm::handleCardSpecific(",
        "IOReturn AirportItlwm::enableAdapter",
    )
    cipher_policy = section(
        routes,
        "case kIocAssociate:",
        "default:",
    )
    active_source_markers = (
        "F8A028722A4A7FE100C6DE90 /* AirportItlwmSkywalkInterface.cpp in Sources */",
        "F8D94CD22B9ABFE20081A3C4 /* AirportItlwmSkywalkInterface.cpp in Sources */",
        "F8E94CD22B9ABFE20081A3C4 /* AirportItlwmSkywalkInterface.cpp in Sources */",
    )
    null_guard = "if (req->req_data == NULL)\n        return kIOReturnUnsupported;"

    return {
        "schema": "itlwm-skywalk-public-cipher-key-get-fixed-stub-alignment-v1",
        "source_base_revision": "3d53c383ec950bc1f17ec2df8bccc363e8176649",
        "reference": {
            "bootkc_sha256": "eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d",
            "bootkc_uuid": "F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5",
            "embedded_kext_uuid": "8FB4B7F0-D656-3539-B8D6-C1327A50377C",
            "nlist_index": 7234,
            "public_wrapper": "0xffffff80021be3c6",
            "next_symbol": "0xffffff80021be3d1",
            "fixed_status": "0xe082280e",
            "body_sha256": "9e4580f0175946d7624b2451e6ffda84a93e91e3e2d852d7a2c7998ee2d78576",
        },
        "scope": {
            "normal_nonnull_public_bsd_get_only": True,
            "set_or_pmk_path_modified": False,
            "outer_or_inner_null_fallback_modified": False,
            "pre26_route_modified": False,
            "card_specific_get_route_modified": False,
            "legacy_v1_modified": False,
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
                    "nlist_index=7234",
                    "n_type=0x0f",
                    "n_sect=1",
                    "n_desc=0x0000",
                    "__Z23apple80211getCIPHER_KEYP23IO80211SkywalkInterfaceP14apple80211_key",
                    "symbol_vmaddr=0xffffff80021be3c6",
                    "symbol_vmaddr_end=0xffffff80021be3d1",
                    "symbol_fileoff=0x20be3c6",
                    "symbol_fileoff_end=0x20be3d1",
                    "symbol_bytes=0x0b",
                    "next_nlist_index=7090",
                    "__Z20apple80211getCHANNELP23IO80211SkywalkInterfaceP23apple80211_channel_data",
                    "next_symbol_vmaddr=0xffffff80021be3d1",
                    "body_sha256=9e4580f0175946d7624b2451e6ffda84a93e91e3e2d852d7a2c7998ee2d78576",
                    "0xe082280e",
                    "reads neither public argument",
                )
            ),
            "note_records_scope_and_nonclaims": all(
                token in note
                for token in (
                    "IOC 3",
                    "0xffffff80021be3c6",
                    "0xe082280e",
                    "compile-time Tahoe-only guard",
                    "normal non-null public Tahoe BSD GET",
                    "card-specific GET remains excluded",
                    "does not claim outer-null, carrier/ABI, SET, card-specific GET, private key route, PMK, association, firmware, runtime-execution, or broader Tahoe behavior parity",
                    "No private carrier or selector is constructed or invoked",
                )
            ),
            "normal_nonnull_tahoe_public_get_returns_exact_fixed_status_unread": (
                "case APPLE80211_IOC_CIPHER_KEY:" in cipher_route
                and "if (cmd == SIOCGA80211)" in tahoe_get_branch
                and "return static_cast<IOReturn>(0xe082280e);" in tahoe_get_branch
                and "req->req_data->" not in tahoe_get_branch
                and "setCIPHER_KEY" not in tahoe_get_branch
                and "setAPSTA_CIPHER_KEY" not in tahoe_get_branch
                and "return kIOReturnSuccess;" not in tahoe_get_branch
            ),
            "set_apsta_and_pmk_paths_remain_separate": (
                "if (cmd != SIOCSA80211)\n                return kIOReturnUnsupported;" in after_tahoe_get_branch
                and "instance->setAPSTA_CIPHER_KEY(" in after_tahoe_get_branch
                and "return setCIPHER_KEY((apple80211_key *)req->req_data);" in after_tahoe_get_branch
                and "APPLE80211_CIPHER_PMK" in cipher_helper
                and "return installExternalPmkLocked(key->key," in cipher_helper
                and "\"CIPHER_KEY\"" in cipher_helper
                and "case APPLE80211_IOC_CIPHER_KEY:\n            IOCTL_SET(request_type, CIPHER_KEY, apple80211_key);" in v1
            ),
            "null_fallback_remains_outside_this_layer": (
                null_guard in dispatcher
                and dispatcher.index(null_guard)
                < dispatcher.index("case APPLE80211_IOC_CIPHER_KEY:")
            ),
            "bsd_ingress_terminalizes_exact_nonunsupported_status": (
                "IOReturn ret = processApple80211Ioctl(normalizedCmd, req);" in bsd_bridge
                and "if (ret != kIOReturnUnsupported)\n            return ret;" in bsd_bridge
            ),
            "card_specific_get_remains_excluded_before_dispatch": (
                "case kIocCipherKey:" in cipher_policy
                and "return isSet;" in cipher_policy
                and "routeTahoeSkywalkIoctl(interface, &req," in card_specific
                and "isSet ? SIOCSA80211 : SIOCGA80211" in card_specific
            ),
            "selector_and_unread_pointer_context_remain_explicit": (
                "#define APPLE80211_IOC_CIPHER_KEY                3" in ioctl
                and "struct apple80211_key" in var
                and "P14apple80211_key" in raw
            ),
            "all_active_skywalk_source_phases_and_target_guard_are_explicit": (
                all(marker in project for marker in active_source_markers)
                and "#if __IO80211_TARGET >= __MAC_26_0" in cipher_route
                and "#endif // __IO80211_TARGET >= __MAC_26_0" in cipher_route
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
            "Skywalk public CIPHER_KEY GET alignment checks failed: "
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
            f"Skywalk public CIPHER_KEY GET alignment validation failed: {exc}",
            file=sys.stderr,
        )
        sys.exit(1)
