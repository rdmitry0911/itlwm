#!/usr/bin/env python3
"""Generate and verify modern WCL Roam Profile functional evidence."""

import argparse
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/wcl_roam_profile_quarantine_report.json"
NOTE = ROOT / "docs/reference/CR-479-wcl-roam-profile-quarantine-20260714.md"
SIGNAL_AUDIT = ROOT / "docs/tahoe_signal_chain_audit.md"
INVENTORY = ROOT / "docs/tahoe_discrepancy_inventory.md"
CPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.cpp"
NET80211 = ROOT / "itl80211/openbsd/net80211/ieee80211.c"
INPUT = ROOT / "itl80211/openbsd/net80211/ieee80211_input.c"
NODE = ROOT / "itl80211/openbsd/net80211/ieee80211_node.c"
VAR = ROOT / "itl80211/openbsd/net80211/ieee80211_var.h"
IWN_REG = ROOT / "itlwm/hal_iwn/if_iwnreg.h"
IWM_REG = ROOT / "itlwm/hal_iwm/if_iwmreg.h"
IWX_REG = ROOT / "itlwm/hal_iwx/if_iwxreg.h"
SOURCE_ROOTS = (
    ROOT / "AirportItlwm",
    ROOT / "include",
    ROOT / "itl80211",
    ROOT / "itlwm",
)


def section(source, begin, end):
    start = source.index(begin)
    return source[start:source.index(end, start)]


def source_contains(token):
    for root in SOURCE_ROOTS:
        for path in root.rglob("*"):
            if path.suffix not in {".c", ".cc", ".cpp", ".h", ".hpp"}:
                continue
            if token in path.read_text(encoding="utf-8", errors="ignore"):
                return True
    return False


def has_all(source, tokens):
    return all(token in source for token in tokens)


def report():
    cpp = CPP.read_text(encoding="utf-8")
    net80211 = NET80211.read_text(encoding="utf-8")
    input_source = INPUT.read_text(encoding="utf-8")
    node = NODE.read_text(encoding="utf-8")
    var = VAR.read_text(encoding="utf-8")
    note = NOTE.read_text(encoding="utf-8")
    signal_audit = SIGNAL_AUDIT.read_text(encoding="utf-8")
    inventory = INVENTORY.read_text(encoding="utf-8")
    normalized_signal_audit = " ".join(signal_audit.split())
    normalized_inventory = " ".join(inventory.split())
    setter = section(
        cpp,
        "setWCL_ROAM_PROFILE_CONFIG(apple80211_roam_profile_config *data)",
        "setWCL_ARP_MODE",
    )
    parser = section(cpp, "tahoeBuildIntelRoamProfile", "IOReturn AirportItlwmSkywalkInterface::\nsetWCL_ROAM_PROFILE_CONFIG")
    reference_tokens = (
        "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
        "0x100018b74", "`+0x6d8`", "0x100141e10", "0x1001a01a0",
        "`0xe00002bc`", "`+0x15c0`", "0x10001c3f8", "`+0x0`",
        "`+0xb8`", "`+0x170`", "0x10001bfca", "0x10001bd9a",
        "0x10001c5b0", "`+0x238`", "0x10001de02", "0x10001c6ba",
        "0x10001c322", "0x10017b900", "0x10017b6e6",
    )
    parser_tokens = (
        "0x000, 0x0b8, 0x170", "bandOffset + 4 + source * 0x3c",
        "input + 4", "input + 6", "input + 8", "input + 0x0a",
        "input + 0x0c", "input + 0x0e", "input + 0x10",
        "input[0x24]", "input + 0x26", "input + 0x28",
        "input + 0x2e", "input + 0x30", "input + 0x36",
        "input + 0x38", "raw + 0x230", "raw[0x238]",
    )
    checks = {
        "reference_consumer_map": has_all(note, reference_tokens)
        and "Bytes without a recovered consumer remain reserved" in note,
        "exact_public_carrier_size": has_all(
            cpp,
            (
                "struct apple80211_roam_profile_config",
                "uint8_t raw[0x23c]",
                "static_assert(sizeof(apple80211_roam_profile_config) == 0x23c",
            ),
        ),
        "consumed_fields_parsed": has_all(parser, parser_tokens)
        and "tahoeRoamProfileReadSignedByte" in parser,
        "setter_publishes_or_errors": has_all(
            setter,
            (
                "if (data == nullptr)", "return kIOReturnBadArgumentTahoe;",
                "if (fHalService == nullptr)", "if (ic == nullptr)",
                "tahoeBuildIntelRoamProfile(data, &policy)",
                "ieee80211_set_roam_profile_policy(ic, &policy)",
                "return kIOReturnSuccess;",
            ),
        ) and "return kIOReturnUnsupported;" not in setter,
        "generation_published_policy_owner": has_all(
            var + net80211,
            (
                "struct ieee80211_roam_profile_bracket",
                "struct ieee80211_roam_profile_policy",
                "ic_roam_profile_generation", "ic_roam_profile",
                "ieee80211_set_roam_profile_policy",
                "ieee80211_roam_profile_snapshot",
                "IEEE80211_ROAM_PROFILE_BAND_2GHZ",
                "IEEE80211_ROAM_PROFILE_BAND_5GHZ",
                "IEEE80211_ROAM_PROFILE_BAND_6GHZ",
            ),
        ),
        "scan_schedule_consumes_profile": has_all(
            input_source + net80211,
            (
                "ieee80211_roam_profile_scan_delay",
                "initial_scan_period_s", "full_scan_period_s",
                "backoff_multiplier", "max_scan_period_s",
                "roam_profile > 0 ? roam_delay_ms",
            ),
        ),
        "candidate_selection_consumes_delta_and_boost": has_all(
            node + net80211,
            (
                "ieee80211_roam_profile_candidate_allowed",
                "roam_delta_db", "boost_threshold_dbm", "boost_delta_db",
                "candidate_score >= current_dbm",
            ),
        ),
        "all_intel_families_share_rssi_bias": has_all(
            IWN_REG.read_text(encoding="utf-8"), ("IWN_MIN_DBM    -100", "IWN_MAX_DBM    -33")
        ) and has_all(
            IWM_REG.read_text(encoding="utf-8"), ("IWM_MIN_DBM    -100", "IWM_MAX_DBM    -33")
        ) and has_all(
            IWX_REG.read_text(encoding="utf-8"), ("IWX_MIN_DBM    -100", "IWX_MAX_DBM    -33")
        ),
        "no_fake_broadcom_transport": all(
            not source_contains(token)
            for token in (
                "setRoamingProfileV6(", "handleRoamProfileAsyncCallBack(",
                "disable6GForRoamScans(", "disable6GForRoamScansCallback(",
                "applyRoamingCandidateBoost(", "configureMultiAPBit(",
                "sendIOVarSet(", "runIOVarSet(", '"roam_prof"',
                '"join_pref"', '"roam_multi_ap_env"',
            )
        ),
        "runtime_evidence_recorded": has_all(
            note,
            (
                "8CA541DE-EE9F-351A-B6C5-BC509304E9A5",
                "082e92f707f8618d407e264eb932fdcc073c1ca98cae9753e15afa0ee084a915",
                "20000 ms", "1288 times", "35/35", "25/25", "WPA3",
            ),
        ),
        "historical_claims_updated": "quarantine is now functionally closed" in normalized_inventory
        and "2026-08-01 functional closure" in normalized_signal_audit
        and "valid carrier now succeeds only after policy publication" in normalized_signal_audit,
    }
    return {
        "schema": "itlwm-wcl-roam-profile-functional-v2",
        "source_base_revision": "20ca3b14d4dc3fc3532ad07d7fb9b8a03140e25f",
        "reference": {
            "image_sha256": "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
            "infra_wrapper": "0x100018b74",
            "infra_virtual_offset": "0x6d8",
            "core_setter": "0x100141e10",
            "adapter_null_cold_path": "0x1001a01a0",
            "roam_adapter_offset": "0x15c0",
            "adapter_setter": "0x10001c3f8",
            "per_band_setter": "0x10001bfca",
            "profile_callback": "0x10001bd9a",
            "disable_6g": "0x10001c5b0",
            "disable_6g_callback": "0x10001de02",
            "candidate_boost": "0x10001c6ba",
            "multi_ap": "0x10001c322",
            "commander_send_iovar_set": "0x10017b900",
            "commander_run_iovar_set": "0x10017b6e6",
            "null_status": "0xe00002bc",
            "public_carrier_bytes": "0x23c",
        },
        "local": {
            "matching_user_visible_roam_policy_owner_implemented": True,
            "request_false_success": False,
            "complete_consumed_field_slice_proven": True,
            "complete_reserved_carrier_semantics_proven": False,
            "broadcom_commander_transport_emulated": False,
            "iwn_iwm_iwx_shared_policy": True,
        },
        "runtime": {
            "candidate_uuid": "8CA541DE-EE9F-351A-B6C5-BC509304E9A5",
            "candidate_binary_sha256": "082e92f707f8618d407e264eb932fdcc073c1ca98cae9753e15afa0ee084a915",
            "auxkc_sha256": "6fe8d1d331561d7402f7c9bd99bc820dd5c279b74d089798441b944dea184c9a",
            "profile_valid_mask": 7,
            "profile_band_bracket_counts": [2, 2, 1],
            "active_scan_delay_ms": 20000,
            "scan_delay_observations": 1288,
            "security": "WPA3 Personal",
            "source_bound_ping": "35/35 pre-final; 20/20 post-boot; 25/25 radio-cycle",
            "wifi_faults": 0,
            "wifi_recoveries": 0,
        },
        "checks": checks,
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--write", action="store_true")
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    if args.write == args.check:
        parser.error("select exactly one of --write or --check")
    value = report()
    failed = [key for key, passed in value["checks"].items() if not passed]
    if failed:
        raise ValueError("WCL Roam Profile functional checks failed: " + ", ".join(failed))
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
        print(f"WCL Roam Profile functional validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
