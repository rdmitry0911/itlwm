#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

python3 - "$ROOT" <<'PY'
from pathlib import Path
import sys

root = Path(sys.argv[1])
sky = (root / "AirportItlwm/AirportItlwmSkywalkInterface.cpp").read_text()
core = (root / "itl80211/openbsd/net80211/ieee80211.c").read_text()
node = (root / "itl80211/openbsd/net80211/ieee80211_node.c").read_text()
proto = (root / "itl80211/openbsd/net80211/ieee80211_proto.c").read_text()
var = (root / "itl80211/openbsd/net80211/ieee80211_var.h").read_text()
iwn = (root / "itlwm/hal_iwn/ItlIwn.cpp").read_text()
iwx = (root / "itlwm/hal_iwx/IwxSaeEngine.inc").read_text()


def fail(message):
    raise SystemExit(f"Tahoe WCL reassoc roam-scan contract: {message}")


def require(text, needle, label):
    if needle not in text:
        fail(f"missing {label}: {needle}")


def forbid(text, needle, label):
    if needle in text:
        fail(f"unexpected {label}: {needle}")


def body(text, marker, label):
    start = text.find(marker)
    if start < 0:
        fail(f"missing {label}")
    opening = text.find("{", start)
    if opening < 0:
        fail(f"missing body for {label}")
    depth = 0
    for pos in range(opening, len(text)):
        if text[pos] == "{":
            depth += 1
        elif text[pos] == "}":
            depth -= 1
            if depth == 0:
                return text[opening + 1:pos]
    fail(f"unterminated {label}")


carrier = body(sky, "struct apple80211_reassoc\n", "Apple reassoc carrier")
for token in (
    "uint16_t channel_specs[50]",
    "apple80211_reassoc_candidate candidates[7]",
    "uint32_t candidate_count",
    "uint32_t channel_spec_count",
    "uint8_t feature_flags",
    "int8_t prune_rssi_dbm",
):
    require(carrier, token, "25C56 carrier layout")
for token in (
    "offsetof(apple80211_reassoc, candidates) == 0x64",
    "offsetof(apple80211_reassoc, candidate_count) == 0x90",
    "offsetof(apple80211_reassoc, channel_spec_count) == 0x94",
    "sizeof(apple80211_reassoc) == 0x9c",
):
    require(sky, token, "carrier ABI assertion")

producer = body(sky, "setWCL_REASSOC(apple80211_reassoc *data)",
                "WCL reassoc producer")
require(producer, "ieee80211_begin_wcl_reassoc_bgscan",
        "real lower roam-scan delegation")
require(producer, "data->channel_spec_count",
        "50-entry chanspec ingestion")
require(producer, "data->candidate_count", "candidate ingestion")
forbid(producer, "SAME_BSS_TRANSPARENT", "fabricated same-BSS success")
forbid(producer, "IEEE80211_FC0_SUBTYPE_REASSOC_REQ",
       "OTA request to the still-current BSS")
forbid(producer, "clearExternalPmkEligibilityLocked",
       "credential destruction before target selection")

scan = body(core, "ieee80211_begin_wcl_reassoc_bgscan(",
            "common WCL roam scan")
for token in (
    "(*ic->ic_bgscan_start)(ic)",
    "ieee80211_free_allnodes(ic, 0)",
    "IEEE80211_F_BGSCAN",
    "IEEE80211_WCL_REASSOC_OWNER_LEAF_SCAN_STARTED",
):
    require(scan, token, "real HAL scan ownership")
forbid(scan, "airportItlwmIsRoamLocked",
       "explicit request blocked by autonomous-roam preference")

selector = body(core, "ieee80211_wcl_reassoc_candidate_disposition(",
                "WCL candidate filter")
for token in (
    "ic_wcl_reassoc_source_bssid",
    "request->channel_count",
    "request->candidate_count",
    "request->prune_rssi_dbm",
):
    require(selector, token, "bounded Apple candidate policy")

matcher = body(node, "ieee80211_match_bss(", "BSS admission")
for token in (
    "wcl_target = bgscan && ic->ic_wcl_reassoc_owner_active",
    "ieee80211_wcl_reassoc_candidate_disposition(ic, ni, NULL) >= 0",
    "wnm_target != 1 && !wcl_target",
):
    require(matcher, token, "firmware-roam DESBSSID bypass")

completion = body(node, "ieee80211_end_scan_controlled(",
                  "scan completion")
for token in (
    "wcl_reassoc_scan",
    "IEEE80211_WCL_REASSOC_OWNER_LEAF_SCAN_FAILED",
    "IEEE80211_WCL_REASSOC_OWNER_LEAF_ROAM_STARTED",
    "ic_sae_wcl_roam_start",
    "source->ni_unref_cb = ieee80211_node_switch_bss",
):
    require(completion, token, "real target switch path")

success = body(core, "ieee80211_wcl_reassoc_target_port_valid(",
               "target completion gate")
for token in (
    "IEEE80211_WCL_REASSOC_OWNER_LEAF_ROAM_STARTED",
    "ic->ic_state != IEEE80211_S_RUN",
    "ni->ni_port_valid",
    "ic_wcl_reassoc_target_bssid",
    "ieee80211_wcl_reassoc_post_success(ic)",
):
    require(success, token, "post-roam RUN/port-valid completion")

failure = body(core, "void\nieee80211_wcl_reassoc_post_failure(",
               "WCL async failure")
require(failure, "LEAF_SCAN_FAILED", "source-preserving no-target failure")
require(failure, "ieee80211_pae_assoc_epoch_begin(ic)",
        "post-switch failure epoch fence")

admit = body(proto,
    "ieee80211_sae_wcl_request_admit_cached_roam_candidate(",
    "cached SAE roam admission")
for token in (
    "ic_wcl_reassoc_owner_active",
    "IEEE80211_WCL_REASSOC_OWNER_LEAF_ROAM_STARTED",
    "ic_wcl_reassoc_target_bssid",
    "IEEE80211_SAE_WCL_REQUEST_SCAN_ISSUED",
):
    require(admit, token, "fresh WCL-selected SAE target proof")

targeted = body(iwn, "iwn_sae_targeted_roam_start(",
                "IWN targeted SAE roam")
for token in (
    "sc_sae_wcl_credential_active",
    "ieee80211_sae_wcl_request_begin",
    "stageSaeWclCredential",
    "ieee80211_sae_wcl_request_admit_cached_roam_candidate",
    "ieee80211_node_join_bss",
    'consume_wnm ? "BTM" : "WCL"',
):
    require(targeted, token, "driver-resident SAE retarget")
require(iwn, "ic->ic_sae_wcl_roam_start = ItlIwn::iwn_sae_wcl_roam_start",
        "IWN hook publication")
require(iwn, "ic->ic_sae_wcl_roam_start = NULL",
        "IWN hook teardown")
targeted_iwx = body(iwx, "iwx_sae_targeted_roam_start(",
                    "IWX targeted SAE roam")
for token in (
    "sc_sae_wcl_credential_active",
    "ieee80211_sae_wcl_request_begin",
    "stageSaeWclCredential",
    "ieee80211_sae_wcl_request_admit_cached_roam_candidate",
    "ieee80211_node_join_bss",
    'consume_wnm ? "BTM" : "WCL"',
):
    require(targeted_iwx, token, "IWX driver-resident SAE retarget")
require(iwx, "ic->ic_sae_wcl_roam_start = ItlIwx::iwx_sae_wcl_roam_start",
        "IWX hook publication")
require(iwx, "ic->ic_sae_wcl_roam_start = NULL",
        "IWX hook teardown")
require(var, "ic_sae_wcl_roam_start", "common optional SAE roam hook")

print("PASS: Tahoe WCL reassoc uses a real bounded roam scan and IWN/IWX SAE retarget")
PY
