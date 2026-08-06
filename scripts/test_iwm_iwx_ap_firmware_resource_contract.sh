#!/bin/sh
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

python3 - "$repo_root" <<'PY'
from pathlib import Path
import re
import sys

root = Path(sys.argv[1])
runtime = (root / "include/HAL/ItlApFirmwareRuntime.hpp").read_text()
iwm_hal = (root / "itlwm/hal_iwm/ItlIwm.cpp").read_text()
iwm_mac = (root / "itlwm/hal_iwm/mac80211.cpp").read_text()
iwx_hal = (root / "itlwm/hal_iwx/ItlIwx.cpp").read_text()
iwx_capability = (root / "itlwm/hal_iwx/IwxApGoCapability.hpp").read_text()
iwx_reg = (root / "itlwm/hal_iwx/if_iwxreg.h").read_text()


def require(haystack: str, needle: str, label: str) -> None:
    if needle not in haystack:
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


def require_order(source: str, needles: list[str], label: str) -> None:
    positions = [source.find(needle) for needle in needles]
    if any(position < 0 for position in positions) or positions != sorted(positions):
        raise SystemExit(f"FAIL: {label} order: {list(zip(needles, positions))}")


for field in ("ssid", "credential", "rsnIE", "beacon"):
    require(runtime, f"runtime->{field}", f"owned AP {field}")
require(runtime, "runtime->config.beaconTemplate = runtime->beacon;",
        "owned beacon pointer")
require(runtime, "explicit_bzero(runtime, sizeof(*runtime));",
        "credential-bearing reset")
require(runtime, "runtime->broadcastQueueId = UINT16_MAX;",
        "invalid broadcast queue reset")
require(runtime, "runtime->multicastQueueId = UINT16_MAX;",
        "invalid multicast queue reset")
require(runtime, "runtime->clients[index].queueId = UINT16_MAX;",
        "invalid per-client queue reset")
require(runtime, "ItlApFirmwareClientRuntime clients[kItlApFirmwareMaxClients]",
        "bounded client resource table")

iwm_hal_start = body(iwm_hal, "startAPMode(const struct ItlHalApConfig *config)")
require(iwm_hal_start, "itl_ap_client_config_supported(config)",
        "IWM open/WPA2 admission")
require(iwm_hal_start, "iwm_start_ap_resources(&com, &apRuntime)",
        "IWM HAL resource start")
require(body(iwm_hal, "stopAPMode()"),
        "iwm_stop_ap_resources(&com, &apRuntime)", "IWM HAL resource stop")
iwm_capability = body(iwm_hal, "supportsAPMode() const")
for needle, label in (
    ("#if !defined(IEEE80211_OPT_OUT_STA_ONLY)", "IWM opt-out gate"),
    ("IWM_DEVICE_FAMILY_8000", "IWM 8000 family"),
    ("IWM_DEVICE_FAMILY_9000", "IWM 9000 family"),
    ("IWM_UCODE_TLV_CAPA_DQA_SUPPORT", "IWM DQA gate"),
    ("IWM_UCODE_TLV_API_STA_TYPE", "IWM typed-station gate"),
    ("com.sc_mqrx_supported", "IWM MQ-RX gate"),
):
    require(iwm_capability, needle, label)

iwm_start = body(iwm_mac, "iwm_start_ap_resources(struct iwm_softc *sc,")
require_order(iwm_start, [
    "iwm_ap_send_beacon_template(sc, runtime)",
    "iwm_ap_mac_ctxt_cmd(sc, runtime, IWM_FW_CTXT_ACTION_ADD)",
    "iwm_ap_binding_cmd(sc, runtime, true)",
    "runtime->multicastStaId,",
    "runtime->broadcastStaId,",
    "iwm_ap_update_quotas(sc, runtime, true)",
], "IWM Linux-donor AP bring-up")
require(iwm_start, "IWM_DQA_GCAST_QUEUE", "IWM multicast queue")
require(iwm_start, "IWM_DQA_AP_PROBE_RESP_QUEUE", "IWM probe queue")
require(iwm_mac, "IWM_FW_CTXT_ACTION_MODIFY", "IWM same-PHY binding modify")
require_order(body(iwm_mac, "iwm_ap_mac_ctxt_cmd(struct iwm_softc *sc,"), [
    "iwm_nic_lock(sc)",
    "iwm_read_prph(sc, IWM_DEVICE_SYSTEM_TIME_REG)",
    "iwm_nic_unlock(sc)",
], "IWM locked firmware system-time read")
iwm_ap_mac = body(iwm_mac,
    "iwm_ap_mac_ctxt_cmd(struct iwm_softc *sc,")
for needle, label in (
    ("runtime->config.channel <= 14 ? 0x01 : 0x15", "IWM AP basic OFDM rates"),
    ("IWM_MAC_FILTER_IN_PROBE_REQUEST", "IWM AP probe-request filter"),
    ("IWM_MAC_QOS_FLG_TGN", "IWM AP HT/TGN flag"),
    ("primary->in_ni.ni_rstamp", "IWM associated-STA TBTT anchor"),
    ("36 + arc4random_uniform(64 - 36)", "IWM reference TBTT separation"),
):
    require(iwm_ap_mac, needle, label)
if "IWM_MAC_FILTER_ACCEPT_GRP" in iwm_ap_mac:
    raise SystemExit("FAIL: IWM AP MAC must leave multicast RX to typed station")
for state in ("IEEE80211_S_INIT", "IEEE80211_S_RUN"):
    require(iwm_start, state, f"IWM stable AP start state {state}")

for field in ("byte_cnt", "flags", "template_id", "tim_idx", "tim_size",
              "ecsa_offset", "csa_offset", "frame[0]"):
    require(iwx_reg, field, f"IWX modern beacon {field}")
ap_wire = body(iwx_reg, "struct iwx_mac_data_ap")
for field in ("uint32_t reserved1;", "uint32_t reserved2;"):
    require(ap_wire, field, f"IWX API-68 AP reserved field {field}")
if "reciprocal" in ap_wire:
    raise SystemExit("FAIL: IWX API-68 AP reserved words must not be reciprocal fields")
iwx_ap_fill = body(iwx_hal, "iwx_mac_ctxt_cmd_fill_ap(struct iwx_softc *sc,")
for field in ("reserved1", "reserved2"):
    if field in iwx_ap_fill:
        raise SystemExit(f"FAIL: IWX API-68 AP {field} must remain zero")
require(iwx_hal, "commandVersion != 11 && commandVersion != 12",
        "IWX API-68 beacon version gate")
require(iwx_hal, "IWX_MAC_BEACON_CCK", "IWX v11 beacon rate flag")
require(iwx_hal, "rateIndex - IWX_FIRST_OFDM_RATE",
        "IWX v11 beacon firmware rate index")
iwx_beacon = body(iwx_hal,
    "iwx_ap_send_beacon_template(struct iwx_softc *sc,")
require_order(iwx_beacon, [
    "IWX_BEACON_TEMPLATE_CMD,",
    "IWX_CMD_ASYNC,",
    "commandLength, command",
], "IWX API-68 asynchronous beacon resource submission")
require(iwx_hal, "case IWX_BEACON_TEMPLATE_CMD:",
        "IWX asynchronous beacon q0 completion retirement")
require(iwx_hal, "iwx_read_prph(sc, IWX_DEVICE_SYSTEM_TIME_REG)",
        "IWX firmware system-time TBTT")
require(iwx_hal, "IWX_FW_CTXT_ACTION_MODIFY", "IWX same-PHY binding modify")
iwx_ap_mac = body(iwx_hal,
    "iwx_ap_mac_ctxt_cmd(struct iwx_softc *sc,")
for needle, label in (
    ("runtime->config.channel <= 14 ? 0x01 : 0x15", "IWX AP basic OFDM rates"),
    ("IWX_MAC_FILTER_IN_PROBE_REQUEST", "IWX AP probe-request filter"),
    ("IWX_MAC_QOS_FLG_TGN", "IWX AP HT/TGN flag"),
    ("primary->in_ni.ni_rstamp", "IWX associated-STA TBTT anchor"),
    ("36 + arc4random_uniform(64 - 36)", "IWX reference TBTT separation"),
):
    require(iwx_ap_mac, needle, label)
if "IWX_MAC_FILTER_ACCEPT_GRP" in iwx_ap_mac:
    raise SystemExit("FAIL: IWX AP MAC must leave multicast RX to typed station")
for needle, label in (
    ("#if !defined(IEEE80211_OPT_OUT_STA_ONLY)", "IWX opt-out gate"),
    ("IWX_DEVICE_FAMILY_22000", "IWX 22000 family"),
    ("IWX_DEVICE_FAMILY_AX210", "IWX AX210 family"),
    ("IWX_UCODE_TLV_API_STA_TYPE", "IWX typed-station gate"),
    ("addStationVersion < 12", "IWX ADD_STA version gate"),
    ("txCommandVersion <= 8", "IWX modern TX rate-index gate"),
    ("beaconVersion == 11 || beaconVersion == 12", "IWX beacon gate"),
):
    require(iwx_capability, needle, label)
if "IWX_UCODE_TLV_CAPA_DQA_SUPPORT" in body(
        iwx_capability, "iwx_softc_supports_ap_go("):
    raise SystemExit(
        "FAIL: IWX gen2/TVQM admission must not require removed DQA TLV")
if "IWX_UCODE_TLV_CAPA_BEACON_STORING" in body(
        iwx_capability, "iwx_softc_supports_ap_go("):
    raise SystemExit("FAIL: optional BEACON_STORING must not gate base AP")
if "IWX_UCODE_TLV_CAPA_GO_UAPSD" in body(
        iwx_capability, "iwx_softc_supports_ap_go("):
    raise SystemExit("FAIL: optional GO_UAPSD must not gate base AP")

iwx_station = body(iwx_hal, "iwx_ap_add_internal_sta(struct iwx_softc *sc,")
require_order(iwx_station, [
    "iwx_send_cmd_pdu_status(sc, IWX_ADD_STA",
    "iwx_tvqm_enable_txq_for_sta(",
], "IWX API-68 add-station-before-queue")
require(iwx_station, "*queueId = (uint16_t)assignedQueue;",
        "IWX firmware-assigned internal queue ownership")

iwx_start = body(iwx_hal, "iwx_start_ap_mode(struct iwx_softc *sc,")
require(iwx_start, "const uint8_t beaconCommandVersion = iwx_lookup_cmd_ver(",
        "IWX beacon-ABI epoch ordering gate")
for state in ("IEEE80211_S_INIT", "IEEE80211_S_RUN"):
    require(iwx_start, state, f"IWX stable AP start state {state}")
v13_branch = body(iwx_start,
    "if (beaconCommandVersion >= 13 &&")
require_order(v13_branch, [
    "iwx_ap_mac_ctxt_cmd(sc, runtime,",
    "iwx_ap_send_beacon_template(sc, runtime)",
], "IWX v13 link-owned MAC-before-beacon bring-up")
api68_branch = iwx_start[iwx_start.find("} else {",
    iwx_start.find("if (beaconCommandVersion >= 13 &&")):]
require_order(api68_branch, [
    "iwx_ap_send_beacon_template(sc, runtime)",
    "iwx_ap_mac_ctxt_cmd(sc, runtime,",
    "iwx_ap_binding_cmd(sc, runtime, true)",
    "runtime->multicastStaId,",
    "runtime->broadcastStaId,",
    "iwx_ap_update_quotas(sc, runtime, true)",
], "IWX API-68/v11-v12 beacon-before-MAC bring-up")
require(iwx_start, "&runtime->multicastQueueId", "IWX multicast TVQM queue")
require(iwx_start, "&runtime->broadcastQueueId", "IWX broadcast TVQM queue")

iwx_remove = body(iwx_hal, "iwx_ap_remove_internal_sta(struct iwx_softc *sc,")
require_order(iwx_remove, [
    "iwx_ap_exchange_tx_ring_carrier(",
    "iwx_reset_tx_ring(sc, detached)",
    "iwx_free_tx_ring(sc, detached)",
    "iwx_send_cmd_pdu(sc, IWX_REMOVE_STA",
], "IWX detach-and-reclaim-before-station teardown")
if "iwx_alloc_tx_ring" in iwx_remove:
    raise SystemExit("FAIL: dynamic IWX AP queue must not become a static ring")

iwx_stop = body(iwx_hal, "iwx_stop_ap_mode(struct iwx_softc *sc,")
require(iwx_stop, "for (size_t index = 0; index < kItlApFirmwareMaxClients; index++)",
        "IWX all-client teardown")
require_order(iwx_stop, [
    "iwx_ap_update_quotas(sc, runtime, false)",
    "runtime->broadcastStaId,",
    "runtime->multicastStaId,",
    "iwx_ap_binding_cmd(sc, runtime, false)",
    "IWX_FW_CTXT_ACTION_REMOVE",
    "itl_ap_firmware_runtime_reset(runtime)",
], "IWX reverse AP unwind")

print("PASS: paired IWM/IWX AP firmware resource contract")
PY
