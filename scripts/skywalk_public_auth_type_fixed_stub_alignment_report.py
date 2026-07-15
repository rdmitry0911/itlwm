#!/usr/bin/env python3
"""Generate and verify Skywalk public AUTH_TYPE fixed-stub evidence."""

import argparse
import hashlib
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/skywalk_public_auth_type_fixed_stub_alignment_report.json"
NOTE = ROOT / "docs/reference/CR-523-skywalk-public-auth-type-fixed-stub-alignment-20260715.md"
RAW = ROOT / "docs/reference/artifacts/legacy-auth-type-public-fixed-stub-bootkc-current/raw.txt"
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
    auth_route = section(
        dispatcher,
        "case APPLE80211_IOC_AUTH_TYPE:",
        "case APPLE80211_IOC_HOST_AP_MODE:",
    )
    tahoe_set_branch = section(
        auth_route,
        "#if __IO80211_TARGET >= __MAC_26_0",
        "#else",
    )
    pre26_set_branch = section(
        auth_route,
        "#else",
        "#endif // __IO80211_TARGET >= __MAC_26_0",
    )
    auth_helper = section(
        cpp,
        "setAUTH_TYPE(struct apple80211_authtype_data *ad)",
        "setCIPHER_KEY(struct apple80211_key *key)",
    )
    public_assoc = section(
        cpp,
        "setASSOCIATE(struct apple80211_assoc_data *ad)",
        "setDISASSOCIATE(void *ad)",
    )
    hidden_assoc = section(
        cpp,
        "setWCL_ASSOCIATE(apple80211AssocCandidates *candidates)",
        "setWCL_LEAVE_NETWORK(apple80211_leave_network *data)",
    )
    card_specific = section(
        v2,
        "SInt32 AirportItlwm::handleCardSpecific(",
        "IOReturn AirportItlwm::enableAdapter",
    )
    auth_route_policy = section(
        routes,
        "case kIocAssociate:",
        "default:",
    )
    tahoe_marker = "F8E94CA52B9ABFE20081A3C4 /* Sources */ = {"
    tahoe_start = project.index(tahoe_marker)
    tahoe_phase = project[tahoe_start:project.index("\t\t};", tahoe_start)]

    null_guard = "if (req->req_data == NULL)\n        return kIOReturnUnsupported;"
    return {
        "schema": "itlwm-skywalk-public-auth-type-fixed-stub-alignment-v1",
        "source_base_revision": "5d76d0f504941c990e28905dde45a084ae79e472",
        "reference": {
            "bootkc_sha256": "eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d",
            "bootkc_uuid": "F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5",
            "embedded_kext_uuid": "8FB4B7F0-D656-3539-B8D6-C1327A50377C",
            "nlist_index": 7204,
            "public_wrapper": "0xffffff80021c3520",
            "next_symbol": "0xffffff80021c352b",
            "fixed_status": "0xe082280e",
            "body_sha256": "9e4580f0175946d7624b2451e6ffda84a93e91e3e2d852d7a2c7998ee2d78576",
        },
        "scope": {
            "normal_nonnull_public_set_only": True,
            "outer_or_inner_null_fallback_modified": False,
            "internal_association_helper_modified": False,
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
                    "nlist_index=7204",
                    "__Z22apple80211setAUTH_TYPEP23IO80211SkywalkInterfaceP24apple80211_authtype_data",
                    "symbol_vmaddr=0xffffff80021c3520",
                    "symbol_vmaddr_end=0xffffff80021c352b",
                    "symbol_fileoff=0x20c3520",
                    "symbol_fileoff_end=0x20c352b",
                    "symbol_bytes=0x0b",
                    "next_nlist_index=7261",
                    "__Z23apple80211setCIPHER_KEYP23IO80211SkywalkInterfaceP14apple80211_key",
                    "body_sha256=9e4580f0175946d7624b2451e6ffda84a93e91e3e2d852d7a2c7998ee2d78576",
                    "0xe082280e",
                    "reads neither public argument",
                )
            ),
            "note_records_scope_and_nonclaims": all(
                token in note
                for token in (
                    "IOC 2",
                    "0xffffff80021c3520",
                    "0xe082280e",
                    "compile-time Tahoe-only guard",
                    "normal non-null public SET",
                    "does not claim outer-null, carrier/ABI, GET, association, firmware, runtime-execution, or broader Tahoe behavior parity",
                    "No private carrier or selector is constructed or invoked",
                )
            ),
            "normal_nonnull_public_route_returns_exact_fixed_status_unread": (
                "case APPLE80211_IOC_AUTH_TYPE:" in auth_route
                and "if (cmd == SIOCGA80211)" in auth_route
                and "return getAUTH_TYPE((apple80211_authtype_data *)req->req_data);" in auth_route
                and "if (cmd == SIOCSA80211)" in auth_route
                and "return static_cast<IOReturn>(0xe082280e);" in tahoe_set_branch
                and "setAUTH_TYPE" not in tahoe_set_branch
                and "current_authtype" not in tahoe_set_branch
                and "req->req_data->" not in tahoe_set_branch
                and "return kIOReturnSuccess;" not in tahoe_set_branch
            ),
            "pre26_public_route_remains_existing_auth_context_helper": (
                "return setAUTH_TYPE((apple80211_authtype_data *)req->req_data);" in pre26_set_branch
                and "static_cast<IOReturn>(0xe082280e)" not in pre26_set_branch
            ),
            "null_fallback_remains_outside_this_layer": (
                null_guard in dispatcher
                and dispatcher.index(null_guard) < dispatcher.index("case APPLE80211_IOC_AUTH_TYPE:")
            ),
            "both_public_tahoe_ingress_paths_terminalize_nonunsupported": (
                "IOReturn ret = processApple80211Ioctl(normalizedCmd, req);" in bsd_bridge
                and "if (ret != kIOReturnUnsupported)\n            return ret;" in bsd_bridge
                and "routeTahoeSkywalkIoctl(interface, &req," in card_specific
                and "isSet ? SIOCSA80211 : SIOCGA80211" in card_specific
                and "if (ret != kIOReturnUnsupported)\n            return ret;" in card_specific
                and "case kIocAuthType:" in auth_route_policy
                and "return isSet;" in auth_route_policy
            ),
            "internal_auth_context_helper_and_association_calls_remain": (
                "current_authtype_lower = ad->authtype_lower;" in auth_helper
                and "current_authtype_upper = ad->authtype_upper;" in auth_helper
                and "return kIOReturnSuccess;" in auth_helper
                and cpp.count("setAUTH_TYPE(&auth_type_data);") == 2
                and "setAUTH_TYPE(&auth_type_data);" in public_assoc
                and "setAUTH_TYPE(&auth_type_data);" in hidden_assoc
            ),
            "tahoe_source_phase_and_v1_boundary_remain_explicit": (
                "AirportItlwmSkywalkInterface.cpp in Sources" in tahoe_phase
                and "AirportItlwmV2.cpp in Sources" in tahoe_phase
                and "AirportSTAIOCTL.cpp in Sources" not in tahoe_phase
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
        raise ValueError("Skywalk public AUTH_TYPE alignment checks failed: " + ", ".join(failed))
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
        print(f"Skywalk public AUTH_TYPE alignment validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
