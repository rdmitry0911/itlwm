#!/bin/sh
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

python3 - "$repo_root" <<'PY'
from pathlib import Path
import sys

root = Path(sys.argv[1])
hal = (root / "include/HAL/ItlHalService.hpp").read_text()
runtime = (root / "include/HAL/ItlApFirmwareRuntime.hpp").read_text()
framing = (root / "include/HAL/ItlApOpenRuntime.hpp").read_text()
iwn_hpp = (root / "itlwm/hal_iwn/ItlIwn.hpp").read_text()
iwn = (root / "itlwm/hal_iwn/ItlIwn.cpp").read_text()
iwm = (root / "itlwm/hal_iwm/mac80211.cpp").read_text()
iwx = (root / "itlwm/hal_iwx/ItlIwx.cpp").read_text()


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


for needle, label in (
    ("uint16_t clientLegacyRateMask", "per-client legacy rate set"),
    ("bool rateControlConfigured", "firmware rate-control fence"),
):
    require(runtime, needle, label)

parse = body(framing, "itl_ap_open_parse_assoc(struct ItlApFirmwareRuntime *runtime,")
for needle, label in (
    ("IEEE80211_ELEMID_RATES", "supported rates IE"),
    ("IEEE80211_ELEMID_XRATES", "extended rates IE"),
    ("itl_hal_ap_legacy_rate_mask", "AP/client rate intersection"),
    ("legacyRateMask &= 0x0ff0", "5 GHz CCK exclusion"),
    ("client->clientLegacyRateMask = legacyRateMask",
     "association-owned rate mask"),
):
    require(parse, needle, label)

require(hal, "itl_hal_ap_legacy_rate_mask",
        "shared IWN/IWM/IWX legacy-rate mapper")
require(iwn_hpp, "uint16_t apClientLegacyRateMask",
        "IWN per-client legacy rate set")
iwn_parse = body(iwn, "bool ItlIwn::iwn_handle_ap_assoc_req(")
for needle, label in (
    ("IEEE80211_ELEMID_XRATES", "IWN extended rates IE"),
    ("itl_hal_ap_legacy_rate_mask", "IWN shared rate intersection"),
    ("legacyRateMask &= 0x0ff0", "IWN 5 GHz CCK exclusion"),
    ("apClientLegacyRateMask = legacyRateMask", "IWN rate commit"),
):
    require(iwn_parse, needle, label)
iwn_config = body(iwn, "int ItlIwn::iwn_send_ap_client_link_quality()")
for needle, label in (
    ("struct iwn_cmd_link_quality", "IWN Link Quality command"),
    ("IWN_MAX_TX_RETRIES", "IWN bounded retry table"),
    ("apClientLegacyRateMask", "IWN negotiated legacy fallback"),
    ("IWN_CMD_LINK_QUALITY", "IWN Link Quality submission"),
):
    require(iwn_config, needle, label)
iwn_tx = body(iwn, "int ItlIwn::iwn_send_ap_data_frame(")
require(iwn_tx, "IWN_TX_LINKQ", "IWN firmware retry-table TX")

iwm_config = body(iwm,
    "iwm_ap_configure_client_rates(")
for needle, label in (
    ("struct iwm_lq_cmd", "IWM Link Quality command"),
    ("IWM_LQ_MAX_RETRY_NUM", "IWM bounded retry table"),
    ("iwl_mvm_mac80211_idx_to_hwrate", "IWM firmware rate encoding"),
    ("IWM_LQ_CMD", "IWM Link Quality submission"),
    ("client->rateControlConfigured = error == 0", "IWM commit fence"),
):
    require(iwm_config, needle, label)
iwm_tx = body(iwm,
    "iwm_ap_send_raw_frame(struct iwm_softc *sc, mbuf_t m,")
require(iwm_tx, "IWM_TX_CMD_FLG_STA_RATE", "IWM firmware retry-table TX")
require(iwm_tx, "firmwareRate ? IWM_DEFAULT_TX_RETRY",
        "IWM data retry budget")

iwx_config = body(iwx,
    "iwx_ap_configure_client_rates(")
for needle, label in (
    ("IWX_TLC_MNG_CONFIG_CMD", "IWX TLC command"),
    ("IWX_TLC_MNG_MODE_NON_HT", "IWX negotiated legacy mode"),
    ("client->clientLegacyRateMask", "IWX per-client rate bitmap"),
    ("struct iwl_tlc_config_cmd_v3", "IWX TLC v3 compatibility"),
    ("struct iwx_tlc_config_cmd_v4", "IWX TLC v4 compatibility"),
    ("client->rateControlConfigured = error == 0", "IWX commit fence"),
):
    require(iwx_config, needle, label)
iwx_tx = body(iwx,
    "iwx_ap_send_raw_frame(struct iwx_softc *sc, mbuf_t m,")
require(iwx_tx, "firmwareRate ? 0 : IWX_TX_FLAGS_CMD_RATE",
        "IWX TLC-selected unicast data rate")

for family, source, task_signature, configure in (
    ("IWM", iwm, "iwm_ap_client_task(void *arg)",
     "iwm_ap_configure_client_rates"),
    ("IWX", iwx, "iwx_ap_client_task(void *arg)",
     "iwx_ap_configure_client_rates"),
):
    task = body(source, task_signature)
    assert task.index(configure) < task.index("itl_ap_open_build_assoc_success"), \
        f"FAIL: {family} must configure rate control before association success"

print("PASS: IWN/IWM/IWX AP per-client legacy firmware rate control")
PY
