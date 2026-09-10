#!/bin/sh
# Static cross-family guard for AP-side receive A-MPDU negotiation.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
python3 - "$ROOT" <<'PY'
from pathlib import Path
import sys

root = Path(sys.argv[1])

def read(path):
    value = (root / path).read_text()
    if not value:
        raise SystemExit(f"FAIL: empty source: {path}")
    return value

def require(text, needle, label):
    if needle not in text:
        raise SystemExit(f"FAIL: AP RX A-MPDU contract lacks {label}")

def ordered(text, first, second, label):
    a = text.find(first)
    b = text.find(second, a + 1)
    if a < 0 or b < 0:
        raise SystemExit(f"FAIL: AP RX A-MPDU order lacks {label}")

wire = read("include/HAL/ItlApBlockAckRuntime.hpp")
runtime = read("include/HAL/ItlApOpenRuntime.hpp")
firmware = read("include/HAL/ItlApFirmwareRuntime.hpp")
iwn_h = read("itlwm/hal_iwn/ItlIwn.hpp")
iwn = read("itlwm/hal_iwn/ItlIwn.cpp")
iwm_h = read("itlwm/hal_iwm/ItlIwm.hpp")
iwm = read("itlwm/hal_iwm/mac80211.cpp")
iwm_rx = read("itlwm/hal_iwm/rx.cpp")
iwx_h = read("itlwm/hal_iwx/ItlIwx.hpp")
iwx = read("itlwm/hal_iwx/ItlIwx.cpp")

for needle, label in (
    ("IEEE80211_ACTION_ADDBA_REQ", "ADDBA request parser"),
    ("IEEE80211_ACTION_ADDBA_RESP", "ADDBA response builder"),
    ("IEEE80211_ACTION_DELBA", "DELBA parser"),
    ("IEEE80211_ADDBA_BA_POLICY", "immediate BA policy"),
    ("IEEE80211_BA_MAX_WINSZ", "bounded receive window"),
    ("requireProtected", "PMF receive policy"),
    ("hardwareDecrypted", "protected-action decrypt witness"),
    ("micCrcLength", "per-frame firmware trailer length"),
    ("IEEE80211_CCMP_HDRLEN", "protected action CCMP offset"),
    ("struct ItlApRxBaRuntime", "bounded software reorder owner"),
    ("itl_ap_rx_ba_reorder", "sequence reorder engine"),
    ("distance >= runtime->window", "receive-window advance"),
    ("old/duplicate sequence", "old sequence rejection"),
    ("IEEE80211_FC0_SUBTYPE_BAR", "BAR window advance"),
    ("IEEE80211_BA_GAP_TIMEOUT", "bounded gap timeout"),
    ("packetTail", "same-SN A-MSDU subframe chain"),
    ("subframeIndex > slot->lastSubframeIndex",
     "ordered A-MSDU subframe admission"),
):
    require(wire, needle, label)

for needle, label in (
    ("kItlApOpenRxAddBaRequest", "shared ADDBA disposition"),
    ("kItlApOpenRxDelBa", "shared DELBA disposition"),
    ("itl_ap_open_build_addba_response", "shared response handoff"),
    ("itl_ap_block_ack_parse", "shared wire parser use"),
):
    require(runtime, needle, label)
require(firmware, "uint16_t clientRxBaMask;", "per-client BA ownership")
require(firmware, "clientRxBa[kItlApRxBaTidCount]",
        "per-client reorder ownership")
require(firmware, "itl_ap_rx_ba_stop(&client->clientRxBa[tid])",
        "buffer purge before client reset")

for header, source, prefix, label in (
    (iwm_h, iwm, "iwm", "IWM"),
    (iwx_h, iwx, "iwx", "IWX"),
):
    require(header, f"{prefix}_ap_set_client_rx_ba", f"{label} BA API")
    require(source, "command.sta_id = client->staId;",
            f"{label} AP station identity")
    require(source, "runtime->macId, runtime->macColor",
            f"{label} AP MAC context identity")
    require(source, "rxba->sta_id = client->staId;",
            f"{label} reorder owner")
    require(source, "client->clientRxBaMask", f"{label} BA lifetime mask")
    require(source, "itl_ap_rx_ba_start(&client->clientRxBa[tid]",
            f"{label} reorder start")
    require(source, "itl_ap_rx_ba_stop(&client->clientRxBa[tid]",
            f"{label} reorder stop")
    require(source, "itl_ap_open_reorder_rx(",
            f"{label} pre-decap reorder dispatch")
    require(source, "apFrames, true", f"{label} ordered redispatch")
    require(source, "mbuf_nextpkt", f"{label} A-MSDU chain redispatch")
    require(source, "result.disposition == kItlApOpenRxAddBaRequest",
            f"{label} runtime ADDBA dispatch")
    require(source, "IEEE80211_STATUS_REFUSED",
            f"{label} fail-closed response")
    require(source, "result.baPeerInitiator",
            f"{label} DELBA direction")
    ordered(source, "client->clientRxBaMask", f"{prefix}_ap_remove_internal_sta",
            f"{label} BA teardown before station removal")

for needle, label in (
    ("IWM_ADD_STA_STATUS_MASK", "IWM ADD_STA status validation"),
    ("IWM_ADD_STA_BAID_VALID_MASK", "IWM firmware BAID validation"),
    ("rollbackStatus", "IWM invalid-BAID rollback"),
    ("command.modify_mask = IWM_STA_MODIFY_REMOVE_BA_TID;",
     "IWM firmware REMOVE_BA rollback"),
):
    require(iwm, needle, label)

iwx_reg = read("itlwm/hal_iwx/if_iwxreg.h")
for needle, label in (
    ("IWX_UCODE_TLV_CAPA_BAID_ML_SUPPORT", "IWX BAID-ML capability"),
    ("IWX_RX_BAID_ALLOCATION_CONFIG_CMD", "IWX BAID allocation opcode"),
    ("struct iwx_rx_baid_cfg_cmd", "IWX BAID command ABI"),
):
    require(iwx_reg, needle, label)
for needle, label in (
    ("iwx_rx_baid_cfg_cmd", "IWX BAID command helper"),
    ("const bool baidMl = isset(", "IWX capability-selected BAID API"),
    ("hcmd.id = IWX_WIDE_ID(IWX_DATA_PATH_GROUP, IWX_RX_BAID_ALLOCATION_CONFIG_CMD);",
     "IWX wide DATA_PATH command"),
    ("hcmd.context_command = context;", "IWX optional primary command ownership"),
    ("hcmd.data[0] = &command;", "IWX BAID command payload"),
    ("hcmd.len[0] = sizeof(command);", "IWX BAID command length"),
    ("iwx_send_cmd_status(sc, &hcmd, &newBaid)", "IWX actual BAID status sender"),
    ("sc, client->staId, tid, ssn, window, start, &baid",
     "IWX AP-client BAID owner"),
    ("IWX AP RX BA refused on legacy BAID firmware",
     "IWX legacy firmware fail-closed witness"),
    ("if (!baidMl)", "IWX AP legacy BAID refusal gate"),
    ("IWX AP RX BA firmware start=%u", "IWX runtime BAID witness"),
    ("case IWX_WIDE_ID(IWX_DATA_PATH_GROUP,\n                             IWX_RX_BAID_ALLOCATION_CONFIG_CMD):",
     "IWX BAID response completion dispatch"),
):
    require(iwx, needle, label)
ordered(iwx, "if (!baidMl)", "iwx_rx_baid_cfg_cmd(\n        sc, client->staId",
        "IWX legacy refusal before AP BAID command")

for source, prefix, label in (
    (iwm, "iwm", "IWM"),
    (iwx, "iwx", "IWX"),
):
    require(source, "itl_ap_firmware_defer_ba(client, &action)",
            f"{label} RX-completion publication")
    require(source, f"{prefix}_ap_process_deferred_ba",
            f"{label} client-task BA owner")
    require(source, f"{label} AP deferred RX ADDBA",
            f"{label} deferred command witness")
    ordered(source, f"{prefix}_ap_process_deferred_ba(",
            f"{prefix}_ap_set_client_rx_ba(",
            f"{label} client-task command ownership")

require(iwm,
        "static_cast<uint8_t>(runtime->broadcastQueueId),\n"
        "                    runtime->broadcastStaId",
        "IWM RX ADDBA response management queue owner")
require(iwx, "sc, response, runtime->broadcastQueueId",
        "IWX RX ADDBA response management queue owner")

for source, prefix, label in (
    (iwm_rx, "IWM", "IWM"),
    (iwx, "IWX", "IWX"),
):
    require(source, f"{prefix}_RX_MPDU_MFLG2_AMSDU",
            f"{label} descriptor A-MSDU witness")
    require(source, f"{prefix}_RX_MPDU_AMSDU_SUBFRAME_IDX_MASK",
            f"{label} descriptor subframe index")
    require(source, f"{prefix}_RX_MPDU_AMSDU_LAST_SUBFRAME",
            f"{label} descriptor last-subframe witness")

for needle, label in (
    ("uint16_t rxBaMask;", "IWN per-client BA ownership"),
    ("struct ItlApRxBaRuntime rxBa[kItlApRxBaTidCount]",
     "IWN per-client reorder ownership"),
    ("iwn_set_ap_client_rx_ba", "IWN BA API"),
    ("iwn_handle_ap_block_ack", "IWN BA RX API"),
):
    require(iwn_h, needle, label)
for needle, label in (
    ("node.id = apClientContext->stationId;", "IWN PAN client identity"),
    ("IWN_FLAG_SET_ADDBA", "IWN ADD_BA firmware command"),
    ("IWN_FLAG_SET_DELBA", "IWN DEL_BA firmware command"),
    ("itl_ap_block_ack_build_response", "IWN response builder"),
    ("IEEE80211_STATUS_REFUSED", "IWN fail-closed response"),
    ("iwn_stop_all_ap_client_rx_ba();", "IWN teardown fence"),
    ("const bool clientOwned = client != NULL && apClientNodeInstalled",
     "IWN materialized unicast owner gate"),
    ("tx->security = IWN_CIPHER_CCMP;", "IWN protected action encryption"),
    ("itl_ap_rx_ba_start(&apClientRxBa[tid]", "IWN reorder start"),
    ("itl_ap_rx_ba_stop(&apClientRxBa[tid]", "IWN reorder stop"),
    ("itl_ap_rx_ba_reorder(", "IWN pre-decap reorder dispatch"),
    ("tx->id = IWN5000_ID_PAN_BROADCAST;",
     "IWN non-data firmware owner independent of client aggregation"),
):
    require(iwn, needle, label)
ordered(iwn, "iwn_handle_ap_assoc_req(wh, len)",
        "iwn_handle_ap_block_ack(wh, len, apHardwareDecrypted)",
        "IWN BA dispatch after association routing")
ordered(iwn, "iwn_handle_ap_block_ack(wh, len, apHardwareDecrypted)",
        "iwn_handle_ap_disconnect(wh, len)",
        "IWN BA dispatch before primary STA fallback")

print("PASS: IWN/IWM/IWX AP receive A-MPDU ADDBA/DELBA contract")
PY
