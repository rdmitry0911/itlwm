#!/usr/bin/env python3
"""Generate and verify the live WCL Roam Lock Intel backend evidence."""

import argparse
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "evidence/state/wcl_roam_lock_quarantine_report.json"
NOTE = ROOT / "docs/reference/CR-479-wcl-roam-lock-quarantine-20260714.md"
SIGNAL_AUDIT = ROOT / "docs/tahoe_signal_chain_audit.md"
CPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.cpp"
HPP = ROOT / "AirportItlwm/AirportItlwmSkywalkInterface.hpp"
BRIDGE = ROOT / "include/ClientKit/AirportItlwmRoamLockBridge.h"
NET80211 = ROOT / "itl80211/openbsd/net80211/ieee80211.c"
INPUT = ROOT / "itl80211/openbsd/net80211/ieee80211_input.c"


def section(source, begin, end):
    start = source.index(begin)
    return source[start:source.index(end, start)]


def report():
    cpp = CPP.read_text(encoding="utf-8")
    hpp = HPP.read_text(encoding="utf-8")
    note = NOTE.read_text(encoding="utf-8")
    signal_audit = SIGNAL_AUDIT.read_text(encoding="utf-8")
    bridge = BRIDGE.read_text(encoding="utf-8")
    net80211 = NET80211.read_text(encoding="utf-8")
    input_source = INPUT.read_text(encoding="utf-8")
    normalized_signal_audit = " ".join(signal_audit.split())
    setter = section(
        cpp,
        "setWCL_SET_ROAM_LOCK(apple80211_set_roam_lock *data)",
        "setVOICE_IND_STATE",
    )
    correction_heading = "## Q13 correction: WCL Roam Lock is RoamAdapter-backed"
    return {
        "schema": "itlwm-wcl-roam-lock-intel-backend-v2",
        "source_base_revision": "bb3dd0fd0f2bc2a1a8d31406fafec7cbde247681",
        "reference": {
            "image_sha256": "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
            "infra_wrapper": "0x100018adc",
            "infra_virtual_offset": "0x4b0",
            "core_setter": "0x10011ed1e",
            "core_null_cold_path": "0x1002082a6",
            "roam_adapter_offset": "0x15c0",
            "adapter_setter": "0x10001e4e0",
            "adapter_callback": "0x10001e59e",
            "commander_send_iovar_set": "0x10017b900",
            "null_status": "0x16",
            "effective_input_byte": "0x0",
            "transport_payload_bytes": "0x4",
        },
        "local": {
            "intel_host_roam_lock_backend_implemented": True,
            "request_false_success": False,
            "complete_public_carrier_layout_proven": False,
            "runtime_diagnostic_payload_sequence": [1, 0],
            "runtime_final_wcl_status": "GOOD:0:0x0",
            "runtime_final_observed_input_phase": "locked=1 before ASSOCIATE",
            "runtime_final_loaded_uuid": "0B6DFCAD-40A1-31AC-A992-EF7CA1AD636B",
            "runtime_final_binary_sha256": "cef09452035a15f939fb114f6a46779fe0f2eed4e6a825d498ce08ac58b5566d",
            "runtime_pure_sae_mfp_traffic": True,
            "runtime_locked_beacon_loss_recovery": True,
            "runtime_radio_recovery": True,
            "runtime_final_unlock_call_observed": False,
            "families": ["IWN", "IWM", "IWX"],
        },
        "checks": {
            "reference_note": all(
                token in note
                for token in (
                    "4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab",
                    "0x100018adc",
                    "`+0x4b0`",
                    "0x10011ed1e",
                    "0x1002082a6",
                    "raw `0x16`",
                    "`+0x15c0`",
                    "0x10001e4e0",
                    "4-byte boolean",
                    "`\"roam_off\"`",
                    "0x10017b900",
                    "0x10001e59e",
                    "complete public carrier allocation",
                    "Runtime closure",
                    "locked=1",
                    "locked=0",
                    "0B6DFCAD-40A1-31AC-A992-EF7CA1AD636B",
                    "cef09452035a15f939fb114f6a46779fe0f2eed4e6a825d498ce08ac58b5566d",
                    "`GOOD:0:0x0`",
                    "MFP=yes",
                    "final-build\n`locked=0` producer call was not observed",
                )
            ),
            "setter_owns_supported_policy": all(
                token in setter
                for token in (
                    "if (data == nullptr)",
                    "return kApple80211ErrInvalidArgumentRaw;",
                    "AIRPORT_ITLWM_REQUIRE_LIVE_OPERATION();",
                    "airportItlwmSetRoamLocked(locked);",
                    "timeout_del(&ic->ic_bgscan_timeout);",
                    "IEEE80211_F_DISABLE_BG_AUTO_CONNECT",
                    "return kIOReturnSuccess;",
                )
            )
            and all(
                token not in setter
                for token in (
                    "cachedWclRoamLocked",
                    "hasCachedWclRoamLock",
                    "return kIOReturnUnsupported;",
                )
            ),
            "layout_neutral_shared_owner": (
                all(
                    token in bridge
                    for token in (
                        "airportItlwmSetRoamLocked",
                        "airportItlwmIsRoamLocked",
                        "outside ieee80211com",
                    )
                )
                and all(
                    token in net80211
                    for token in (
                        "static volatile u_int32_t airport_itlwm_roam_locked",
                        "__atomic_store_n(&airport_itlwm_roam_locked",
                        "__atomic_load_n(&airport_itlwm_roam_locked",
                    )
                )
            ),
            "autonomous_roam_is_suppressed": (
                "ieee80211_begin_bgscan(struct _ifnet *ifp)" in net80211
                and "if (airportItlwmIsRoamLocked())" in net80211
                and "else if (!airportItlwmIsRoamLocked() &&" in input_source
            ),
            "pseudo_state_and_layout_remain_absent": all(
                token not in cpp and token not in hpp and token not in net80211
                for token in (
                    "cachedWclRoamLocked",
                    "hasCachedWclRoamLock",
                    "struct apple80211_set_roam_lock",
                )
            ),
            "broadcom_transport_not_invented": all(
                token not in (cpp + net80211 + input_source)
                for token in (
                    "handleRoamOffAsyncCallBack(",
                    "sendIOVarSet(",
                    "runIOVarSet(",
                    "\"roam_off\"",
                )
            ),
            "stale_q13_claim_corrected": correction_heading in signal_audit
            and "roam-lock recovery demonstrates a RoamAdapter transport lifecycle and is reclassified"
            in normalized_signal_audit
            and "- `setWCL_SET_ROAM_LOCK`\n- `setHEARTBEAT`" not in signal_audit,
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
    failed = [key for key, passed in value["checks"].items() if not passed]
    if failed:
        raise ValueError("WCL Roam Lock backend checks failed: " + ", ".join(failed))
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
        print(f"WCL Roam Lock backend validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
