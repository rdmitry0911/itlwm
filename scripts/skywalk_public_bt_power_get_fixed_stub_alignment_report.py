#!/usr/bin/env python3
"""Generate and verify Skywalk public BT_POWER GET fixed-stub evidence."""

import argparse
import hashlib
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/skywalk_public_bt_power_get_fixed_stub_alignment_report.json"
NOTE = ROOT / "docs/reference/CR-526-skywalk-public-bt-power-get-fixed-stub-alignment-20260715.md"
OLD_NOTE = ROOT / "docs/reference/CR-479-legacy-btcoex-setter-fail-contracts-20260707.md"
RAW = ROOT / "docs/reference/artifacts/skywalk-bt-power-get-public-fixed-stub-bootkc-current/raw.txt"
RAW_MANIFEST = RAW.with_name("SHA256SUMS.txt")
CPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.cpp"
V2 = ROOT / "AirportItlwm/AirportItlwmV2.cpp"
ROUTES = ROOT / "AirportItlwm/TahoeSkywalkIoctlRoutes.hpp"
PROJECT = ROOT / "itlwm.xcodeproj/project.pbxproj"


def section(source, begin, end):
    start = source.index(begin)
    return source[start:source.index(end, start)]


def report():
    cpp = CPP.read_text(encoding="utf-8")
    v2 = V2.read_text(encoding="utf-8")
    routes = ROUTES.read_text(encoding="utf-8")
    project = PROJECT.read_text(encoding="utf-8")
    note = " ".join(NOTE.read_text(encoding="utf-8").split())
    old_note = " ".join(OLD_NOTE.read_text(encoding="utf-8").split())
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
    bt_power_route = section(
        dispatcher,
        "case APPLE80211_IOC_BT_POWER:",
        "case APPLE80211_IOC_BTCOEX_PROFILES:",
    )
    tahoe_get_branch = section(
        bt_power_route,
        "#if __IO80211_TARGET >= __MAC_26_0",
        "#endif // __IO80211_TARGET >= __MAC_26_0",
    )
    after_tahoe_get_branch = bt_power_route[
        bt_power_route.index("#endif // __IO80211_TARGET >= __MAC_26_0")
        + len("#endif // __IO80211_TARGET >= __MAC_26_0"):
    ]
    card_specific = section(
        v2,
        "SInt32 AirportItlwm::handleCardSpecific(",
        "IOReturn AirportItlwm::enableAdapter",
    )
    bt_power_policy = section(
        routes,
        "case kIocBtCoexFlags:",
        "case kIocRoamProfile:",
    )

    null_guard = "if (req->req_data == NULL)\n        return kIOReturnUnsupported;"
    active_source_markers = (
        "F8A028722A4A7FE100C6DE90 /* AirportItlwmSkywalkInterface.cpp in Sources */",
        "F8D94CD22B9ABFE20081A3C4 /* AirportItlwmSkywalkInterface.cpp in Sources */",
        "F8E94CD22B9ABFE20081A3C4 /* AirportItlwmSkywalkInterface.cpp in Sources */",
    )
    return {
        "schema": "itlwm-skywalk-public-bt-power-get-fixed-stub-alignment-v1",
        "source_base_revision": "dd3bbbe3612d5bb22700d92d645d5157af4a56ab",
        "reference": {
            "bootkc_sha256": "eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d",
            "bootkc_uuid": "F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5",
            "embedded_kext_uuid": "8FB4B7F0-D656-3539-B8D6-C1327A50377C",
            "nlist_index": 7138,
            "public_wrapper": "0xffffff80021bf391",
            "next_symbol": "0xffffff80021bf39c",
            "fixed_status": "0xe082280e",
            "body_sha256": "9e4580f0175946d7624b2451e6ffda84a93e91e3e2d852d7a2c7998ee2d78576",
        },
        "scope": {
            "normal_nonnull_public_bsd_get_only": True,
            "set_route_modified": False,
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
                    "nlist_index=7138",
                    "n_type=0x0f",
                    "n_sect=1",
                    "n_desc=0x0000",
                    "__Z21apple80211getBT_POWERP23IO80211SkywalkInterfacePv",
                    "symbol_vmaddr=0xffffff80021bf391",
                    "symbol_vmaddr_end=0xffffff80021bf39c",
                    "symbol_fileoff=0x20bf391",
                    "symbol_fileoff_end=0x20bf39c",
                    "symbol_bytes=0x0b",
                    "next_nlist_index=7366",
                    "__Z25apple80211getAVAILABILITYP23IO80211SkywalkInterfacePv",
                    "body_sha256=9e4580f0175946d7624b2451e6ffda84a93e91e3e2d852d7a2c7998ee2d78576",
                    "0xe082280e",
                    "reads neither public argument",
                )
            ),
            "note_records_scope_and_nonclaims": all(
                token in note
                for token in (
                    "IOC 104",
                    "0xffffff80021bf391",
                    "0xe082280e",
                    "compile-time Tahoe-only guard",
                    "normal non-null public Tahoe BSD GET",
                    "card-specific GET remains excluded",
                    "current-reference supersession",
                    "does not claim outer-null, carrier/ABI, SET, card-specific GET, association, firmware, runtime-execution, or broader Tahoe behavior parity",
                    "No private carrier or selector is constructed or invoked",
                )
            ),
            "normal_nonnull_tahoe_public_get_returns_exact_fixed_status_unread": (
                "case APPLE80211_IOC_BT_POWER:" in bt_power_route
                and "if (cmd == SIOCGA80211)" in tahoe_get_branch
                and "return static_cast<IOReturn>(0xe082280e);" in tahoe_get_branch
                and "req->req_data->" not in tahoe_get_branch
                and "getBT_POWER" not in tahoe_get_branch
                and "return kIOReturnSuccess;" not in tahoe_get_branch
            ),
            "set_and_pre26_get_fallback_remain_separate": (
                "return (cmd == SIOCSA80211) ? kApple80211ClassOwnerAbsent" in after_tahoe_get_branch
                and ": kIOReturnUnsupported;" in after_tahoe_get_branch
                and "SIOCSA80211" not in tahoe_get_branch
            ),
            "null_fallback_remains_outside_this_layer": (
                null_guard in dispatcher
                and dispatcher.index(null_guard)
                < dispatcher.index("case APPLE80211_IOC_BT_POWER:")
            ),
            "bsd_ingress_terminalizes_exact_nonunsupported_status": (
                "IOReturn ret = processApple80211Ioctl(normalizedCmd, req);" in bsd_bridge
                and "if (ret != kIOReturnUnsupported)\n            return ret;" in bsd_bridge
            ),
            "card_specific_get_remains_excluded_before_dispatch": (
                "case kIocBtPower:" in bt_power_policy
                and "return isSet;" in bt_power_policy
                and "routeTahoeSkywalkIoctl(interface, &req," in card_specific
                and "isSet ? SIOCSA80211 : SIOCGA80211" in card_specific
            ),
            "old_list_backed_observation_is_not_broadened": (
                "getBT_POWER" in old_note
                and "CR-479" in note
                and "current-reference supersession" in note
            ),
            "all_active_skywalk_source_phases_and_target_guard_are_explicit": (
                all(marker in project for marker in active_source_markers)
                and "#if __IO80211_TARGET >= __MAC_26_0" in bt_power_route
                and "#endif // __IO80211_TARGET >= __MAC_26_0" in bt_power_route
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
            "Skywalk public BT_POWER GET alignment checks failed: "
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
            f"Skywalk public BT_POWER GET alignment validation failed: {exc}",
            file=sys.stderr,
        )
        sys.exit(1)
