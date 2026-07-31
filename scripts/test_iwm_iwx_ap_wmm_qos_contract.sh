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
    (donor_output, "ieee80211_add_wme_param", "OpenBSD WMM parameter donor"),
    (donor_input, "WME info IE: len=7 type=2 subtype=0",
     "OpenBSD WMM association parser donor"),
    (runtime, "bool clientQos", "per-client negotiated QoS state"),
    (runtime, "bool clientStationQos", "installed firmware QoS identity"),
):
    require(source, needle, label)

for needle, label in (
    ("static constexpr uint8_t kItlHalApWmmParameterIE[]",
     "shared WMM Parameter carrier"),
    ("0x00, 0x50, 0xf2, 0x02, 0x01, 0x01", "WMM OUI/type/subtype/version"),
    ("0x03, 0xa4, 0x00, 0x00", "Best Effort EDCA record"),
    ("0x27, 0xa4, 0x00, 0x00", "Background EDCA record"),
    ("0x42, 0x43, 0x5e, 0x00", "Video EDCA record"),
    ("0x62, 0x32, 0x2f, 0x00", "Voice EDCA record"),
):
    require(hal, needle, label)

beacon = body(owner, "static size_t apsta_build_beacon(")
require(beacon, "sizeof(kItlHalApWmmParameterIE)", "beacon capacity accounting")
require(beacon, "memcpy(cursor, kItlHalApWmmParameterIE",
        "beacon/probe WMM advertisement")

parse = body(framing, "itl_ap_open_parse_assoc(struct ItlApFirmwareRuntime *runtime,")
for needle, label in (
    ("IEEE80211_ELEMID_QOS_CAP", "standard QoS Capability parser"),
    ("elementLength == 7", "bounded WMM Information parser"),
    ("memcmp(cursor + 2, MICROSOFT_OUI, 3)", "WMM OUI validation"),
    ("WME_INFO_OUI_SUBTYPE", "WMM Information subtype validation"),
    ("client->clientQos = qos", "association QoS commit"),
):
    require(parse, needle, label)

response = body(framing, "itl_ap_open_build_assoc_success(")
require(response, "client->clientQos ? sizeof(kItlHalApWmmParameterIE) : 0",
        "negotiated association-response sizing")
require(response, "memcpy(out, kItlHalApWmmParameterIE",
        "negotiated association-response WMM parameters")

encap = body(framing, "itl_ap_open_encap_data(")
for needle, label in (
    ("sizeof(struct ieee80211_qosframe)", "26-byte QoS header"),
    ("IEEE80211_FC0_SUBTYPE_QOS", "QoS Data subtype"),
    ("LE_WRITE_2(qos->i_qos, 0)", "Best Effort TID 0 carrier"),
    ("bytes + headerLength", "dynamic LLC placement"),
):
    require(encap, needle, label)

iwm_tx = body(iwm, "iwm_ap_send_raw_frame(struct iwm_softc *sc, mbuf_t m,")
require(iwm_tx, "ieee80211_has_qos(wh)", "IWM QoS descriptor selection")
require(iwm_tx, "ieee80211_get_qos(wh) & IEEE80211_QOS_TID",
        "IWM negotiated TID descriptor")

for family, source, add_signature, task_signature, tid_name in (
    ("IWM", iwm, "iwm_ap_add_client_sta(", "iwm_ap_client_task(void *arg)",
     "IWM_TID_NON_QOS"),
    ("IWX", iwx, "iwx_ap_add_client_sta(", "iwx_ap_client_task(void *arg)",
     "IWX_TID_NON_QOS"),
):
    add = body(source, add_signature)
    require(add, f"client->clientQos ? 0 : {tid_name}",
            f"{family} per-client BE queue binding")
    require(add, "client->clientStationQos = client->clientQos",
            f"{family} installed QoS identity")
    task = body(source, task_signature)
    require(task, "client->clientStationQos != client->clientQos",
            f"{family} reassociation QoS replacement fence")

print("PASS: paired IWM/IWX AP WMM negotiation and QoS Best Effort data path")
PY
