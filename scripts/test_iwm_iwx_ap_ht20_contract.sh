#!/bin/sh
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

python3 - "$repo_root" <<'PY'
from pathlib import Path
import sys

root = Path(sys.argv[1])
hal = (root / "include/HAL/ItlHalService.hpp").read_text()
owner = (root / "AirportItlwm/AirportItlwmAPSTAOwner.cpp").read_text()
runtime = (root / "include/HAL/ItlApFirmwareRuntime.hpp").read_text()
framing = (root / "include/HAL/ItlApOpenRuntime.hpp").read_text()
iwm = (root / "itlwm/hal_iwm/mac80211.cpp").read_text()
iwx = (root / "itlwm/hal_iwx/ItlIwx.cpp").read_text()
donor_output = (root / "itl80211/openbsd/net80211/ieee80211_output.c").read_text()
donor_input = (root / "itl80211/openbsd/net80211/ieee80211_input.c").read_text()


def require(source: str, needle: str, label: str) -> None:
    if needle not in source:
        raise SystemExit(f"FAIL: missing {label}: {needle}")


def body(source: str, signature: str) -> str:
    start = source.find(signature)
    if start < 0:
        raise SystemExit(f"FAIL: missing function: {signature}")
    opening = source.find("{", start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening:index + 1]
    raise SystemExit(f"FAIL: unterminated function: {signature}")


for source, needle, label in (
    (donor_output, "ieee80211_add_htcaps", "OpenBSD HT capability donor"),
    (donor_output, "ieee80211_add_htop", "OpenBSD HT operation donor"),
    (donor_input, "ieee80211_setup_htcaps(ni, htcaps + 2, htcaps[1])",
     "OpenBSD per-peer HT parser donor"),
    (hal, "uint16_t htCapabilities", "AP HT capability snapshot"),
    (hal, "uint8_t htMcsSet[16]", "AP HT MCS snapshot"),
    (hal, "itl_hal_ap_build_ht_capability_ie", "shared HT capability builder"),
    (hal, "itl_hal_ap_build_ht_operation_ie", "shared HT operation builder"),
    (runtime, "bool clientHt", "per-client negotiated HT state"),
    (runtime, "uint8_t clientHtMcs[2]", "per-client one/two-stream MCS sets"),
):
    require(source, needle, label)

start = body(owner, "IOReturn AirportItlwmAPSTAOwner::startLowerIfReady()")
for needle, label in (
    ("ic->ic_sup_mcs", "physical-device MCS source"),
    ("IEEE80211_HTCAP_SGI20", "physical-device SGI20 carrier"),
    ("cfg.htMcsSet", "owned HT profile snapshot"),
):
    require(start, needle, label)
beacon = body(owner, "static size_t apsta_build_beacon(")
require(beacon, "itl_hal_ap_build_ht_capability_ie",
        "beacon/probe HT capability")
require(beacon, "itl_hal_ap_build_ht_operation_ie",
        "beacon/probe HT20 operation")

parse = body(framing, "itl_ap_open_parse_assoc(struct ItlApFirmwareRuntime *runtime,")
for needle, label in (
    ("IEEE80211_ELEMID_HTCAPS", "HT Capability IE parser"),
    ("elementLength == 26", "bounded HT Capability shape"),
    ("htCapabilities[5] & runtime->config.htMcsSet[0]",
     "one-stream MCS intersection"),
    ("htCapabilities[6] & runtime->config.htMcsSet[1]",
     "two-stream MCS intersection"),
    ("client->clientHt = ht", "association-owned HT commit"),
):
    require(parse, needle, label)
response = body(framing, "itl_ap_open_build_assoc_success(")
require(response, "kItlHalApHtCapabilityIELength",
        "HT association-response sizing")
require(response, "itl_hal_ap_build_ht_operation_ie",
        "HT association-response operation")

iwm_add = body(iwm, "iwm_ap_add_internal_sta(struct iwm_softc *sc,")
for needle, label in (
    ("IWM_STA_FLG_FAT_EN_20MHZ", "IWM HT20 station width"),
    ("IWM_STA_FLG_MIMO_EN_MIMO2", "IWM two-stream station flag"),
    ("IWM_STA_FLG_MAX_AGG_SIZE_MSK", "IWM peer A-MPDU limit"),
):
    require(iwm_add, needle, label)
iwm_rates = body(iwm, "iwm_ap_configure_client_rates(")
for needle, label in (
    ("RATE_MCS_HT_MSK", "IWM HT firmware rate encoding"),
    ("RATE_MCS_SGI_MSK", "IWM negotiated SGI20 rate encoding"),
    ("client->clientLegacyRateMask", "IWM mandatory legacy fallback"),
    ("command.agg_frame_cnt_limit = 1", "bounded non-aggregate HT base"),
):
    require(iwm_rates, needle, label)

iwx_add = body(iwx, "iwx_ap_add_internal_sta(struct iwx_softc *sc,")
for needle, label in (
    ("IWX_STA_FLG_FAT_EN_20MHZ", "IWX HT20 station width"),
    ("IWX_STA_FLG_MIMO_EN_MIMO2", "IWX two-stream station flag"),
    ("IWX_STA_FLG_MAX_AGG_SIZE_MSK", "IWX peer A-MPDU limit"),
):
    require(iwx_add, needle, label)
iwx_rates = body(iwx, "iwx_ap_configure_client_rates(")
for needle, label in (
    ("IWX_TLC_MNG_MODE_HT", "IWX HT TLC mode"),
    ("command.ht_rates[IWX_TLC_NSS_1]", "IWX one-stream HT bitmap"),
    ("command.ht_rates[IWX_TLC_NSS_2]", "IWX two-stream HT bitmap"),
    ("IWX_TLC_MNG_CH_WIDTH_20MHZ", "IWX negotiated SGI20 bitmap"),
    ("commandV3.ht_rates", "IWX TLC v3 HT compatibility"),
):
    require(iwx_rates, needle, label)

for family, source, task_signature in (
    ("IWM", iwm, "iwm_ap_client_task(void *arg)"),
    ("IWX", iwx, "iwx_ap_client_task(void *arg)"),
):
    task = body(source, task_signature)
    require(task, "client->clientStationHt != client->clientHt",
            f"{family} HT reassociation replacement fence")
    require(task, "client->clientStationHtNss != client->clientHtNss",
            f"{family} NSS reassociation replacement fence")

print("PASS: paired IWM/IWX AP HT20 association and firmware rate control")
PY
