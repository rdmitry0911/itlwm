#!/bin/sh
# Static cross-family guard for AP-side transmit A-MPDU negotiation.
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
        raise SystemExit(f"FAIL: AP TX A-MPDU contract lacks {label}")

def ordered(text, first, second, label):
    a = text.find(first)
    b = text.find(second, a + 1)
    if a < 0 or b < 0:
        raise SystemExit(f"FAIL: AP TX A-MPDU order lacks {label}")

wire = read("include/HAL/ItlApBlockAckRuntime.hpp")
runtime = read("include/HAL/ItlApOpenRuntime.hpp")
firmware = read("include/HAL/ItlApFirmwareRuntime.hpp")
iwn_h = read("itlwm/hal_iwn/ItlIwn.hpp")
iwn_reg = read("itlwm/hal_iwn/if_iwnreg.h")
iwn = read("itlwm/hal_iwn/ItlIwn.cpp")
iwm_h = read("itlwm/hal_iwm/ItlIwm.hpp")
iwm_owner = read("itlwm/hal_iwm/ItlIwm.cpp")
iwm = read("itlwm/hal_iwm/mac80211.cpp")
iwx_h = read("itlwm/hal_iwx/ItlIwx.hpp")
iwx = read("itlwm/hal_iwx/ItlIwx.cpp")

for needle, label in (
    ("kItlApBlockAckAddResponse", "ADDBA response wire state"),
    ("struct ItlApTxBaRuntime", "shared per-TID state"),
    ("kItlApTxBaStartThreshold", "bounded start threshold"),
    ("kItlApTxBaRequestPacketTimeout", "bounded request retry"),
    ("kItlApTxBaBlocked", "per-association fallback state"),
    ("itl_ap_tx_ba_advance_sequence", "firmware sequence mirror"),
    ("itl_ap_tx_ba_response_matches", "token/TID response fence"),
    ("runtime->token == action->token", "dialog-token validation"),
    ("runtime->tid == action->tid", "TID validation"),
    ("IEEE80211_ACTION_ADDBA_RESP", "ADDBA response parser"),
    ("IEEE80211_ADDBA_BA_POLICY", "immediate BA policy"),
    ("IEEE80211_BA_MAX_WINSZ", "bounded transmit window"),
    ("itl_ap_block_ack_build_request", "ADDBA request builder"),
    ("itl_ap_block_ack_build_delete", "DELBA builder"),
    ("protectedFrame ? IEEE80211_FC1_PROTECTED", "PMF action protection"),
):
    require(wire, needle, label)

for needle, label in (
    ("kItlApOpenRxAddBaResponse", "shared response disposition"),
    ("result->baTid = action.tid;", "response TID propagation"),
    ("itl_ap_open_build_tx_addba_request", "shared request allocation"),
    ("itl_ap_open_build_tx_delba", "shared teardown allocation"),
):
    require(runtime, needle, label)

for needle, label in (
    ("uint16_t clientTxBaMask;", "per-client agreement ownership"),
    ("clientTxSequence[kItlApRxBaTidCount]", "per-client sequence ownership"),
    ("clientTxBa[kItlApRxBaTidCount]", "per-client request ownership"),
    ("itl_ap_tx_ba_reset(&client->clientTxBa[tid])", "client reset fence"),
):
    require(firmware, needle, label)

for needle, label in (
    ("iwn_set_ap_client_tx_ba", "IWN TX BA API"),
    ("uint16_t txBaMask;", "IWN per-client agreement ownership"),
    ("txBaQueue[kItlApRxBaTidCount]", "IWN per-RA/TID queue map"),
    ("txSequence[kItlApRxBaTidCount]", "IWN sequence ownership"),
):
    require(iwn_h, needle, label)
for needle, label in (
    ("IWN_IPAN_AUX_QUEUE       10", "IWN PAN auxiliary queue identity"),
    ("IWN_IPAN_FIRST_AGG_QUEUE (IWN_IPAN_AUX_QUEUE + 1)",
     "IWN PAN dynamic aggregate pool boundary"),
):
    require(iwn_reg, needle, label)
for needle, label in (
    ("IWN_IPAN_FIRST_AGG_QUEUE : com.first_agg_txq", "IWN reference-order dynamic aggregate queue search"),
    ("candidate < com.ntxqs; candidate++", "IWN ascending DVM aggregate queue allocation"),
    ("qid >= sc->first_agg_txq && qid < sc->ntxqs", "IWN complete aggregate DMA pool"),
    ("available->queued == 0", "IWN empty queue admission"),
    ("available->first_tb != NULL", "IWN AP DMA queue admission"),
    ("apClientTxBaEnablePending", "IWN ADD_STA completion fence"),
    ("after ADD_STA", "IWN scheduler activation witness"),
    ("apClientTxSequence[tid] & 0x0fff", "IWN live post-fence aggregate SSN"),
    ("requested_ssn=%u activation_ssn=%u", "IWN negotiated/live SSN witness"),
    ("apClientHt ? IWN_AMPDU_MAX", "IWN association-time aggregate LQ limit"),
    ("iwn_ap_ampdu_tx_start", "IWN explicit scheduler start"),
    ("iwn_ap_ampdu_tx_stop", "IWN explicit scheduler stop"),
    ("IWN5000_SCHED_QUEUE_OFFSET(qid), 0", "IWN recycled aggregate queue context reset"),
    ("frameLimit) << 16 | frameLimit", "IWN negotiated SCD aggregate frame limit"),
    ("does not modify it in iwl_trans_pcie_txq_enable()", "IWN DVM SCD interrupt-mask parity"),
    ("txBa->window", "IWN negotiated station LQ aggregate limit"),
    ("const int lqError = iwn_send_ap_client_link_quality()", "IWN post-ADDBA LQ update"),
    ("IWN_LINK_QUAL_FLAGS_SET_STA_TLC_RTS", "IWN aggregate RTS link-quality contract"),
    ("device timeout AP aggregate descriptor", "IWN dynamic queue watchdog witness"),
    ("IWN5000_SCHED_TRANS_TBL(qid)", "IWN dynamic RA/TID translation"),
    ("IWN5000_ID_PAN_CLIENT", "IWN PAN station translation"),
    ("iwnIpanTid2Fifo", "IWN PAN aggregate FIFO ownership"),
    ("4, 0, 0, 4, 2, 2, 5, 5", "IWN PAN TID-to-FIFO mapping"),
    ("qosData ? 0 : IWN_TX_AUTO_SEQ", "IWN QoS sequence ownership"),
    ("(apClientTxSequence[0] & 0x0fff) <<", "IWN QoS sequence mirror"),
    ("flags |= IWN_TX_NEED_PROTECTION", "IWN aggregate protection"),
    ("IWN_TX_AMPDU_CCMP", "IWN protected aggregate flag"),
    ("IWN_AMPDU_MAX", "IWN aggregate link-quality limit"),
    ("iwn_rx_compressed_ba", "IWN compressed BA completion"),
    ("DVM exposes DEST_PS as TX_FILTERED even for an aggregation", "IWN aggregate power-save filter parity"),
    ("iwn_queue_ap_ps_packet(apPsFilteredPacket, true)", "IWN aggregate power-save requeue"),
    ("txdata->ap_data", "IWN AP descriptor completion owner"),
    ("airportItlwmRequestAPTxDequeue", "IWN Skywalk wake"),
    ("IWN AP TX ADDBA request", "IWN request witness"),
    ("IWN AP TX ADDBA response", "IWN response witness"),
    ("terminalAggregateFailure", "IWN terminal aggregate failure classifier"),
    ("kItlApTxBaBlocked", "IWN per-association non-aggregate fallback"),
    ("IWN AP TX BA fallback", "IWN stable PAN fallback witness"),
    ("qid == apClientTxBaQueue[cba->tid]", "IWN dynamic BA completion"),
):
    require(iwn, needle, label)
ordered(iwn, "iwn_stop_all_ap_client_tx_ba();",
        "iwn_stop_all_ap_client_rx_ba();", "IWN TX teardown before RX/node")
ordered(iwn, "itl_ap_tx_ba_response_matches(txBa, &action)",
        "iwn_set_ap_client_tx_ba(", "IWN validate before scheduler start")

require(iwm_h, "iwm_ap_set_client_tx_ba", "IWM TX BA API")
for needle, label in (
    ("itl_ap_tx_ba_note_data(&client->clientTxBa[0])", "IWM traffic trigger"),
    ("iwm_ap_send_raw_frame", "IWM request transport"),
):
    require(iwm_owner, needle, label)
for needle, label in (
    ("iwm_disable_txq(sc, queueId, tid, 0)", "IWM queue transition"),
    ("iwm_enable_txq(sc, client->staId", "IWM AP station queue owner"),
    ("start ? 1 : 0", "IWM aggregate scheduler mode"),
    ("command.sta_id = client->staId;", "IWM AP station modify"),
    ("runtime->macId, runtime->macColor", "IWM AP MAC context"),
    ("client->clientTxBaMask != 0", "IWM aggregate LQ limit"),
    ("tx_resp->frame_count > 1", "IWM aggregate TX response deferral"),
    ("ba_notif->sta_id == apClient->staId", "IWM AP BA owner"),
    ("apRunning", "IWM AP-only BA completion admission"),
    ("itl_ap_tx_ba_advance_sequence", "IWM firmware sequence mirror"),
    ("IWM AP TX ADDBA response", "IWM response witness"),
):
    require(iwm, needle, label)
ordered(iwm, "itl_ap_tx_ba_response_matches(txBa, &action)",
        "iwm_ap_set_client_tx_ba(", "IWM validate before queue transition")

require(iwx_h, "iwx_ap_set_client_tx_ba", "IWX TX BA API")
for needle, label in (
    ("itl_ap_tx_ba_note_data(&client->clientTxBa[0])", "IWX traffic trigger"),
    ("IWX_TX_CMD_FLG_SEQ_CTL", "IWX firmware sequence owner"),
    ("TVQM allocated this queue for the exact AP sta_id/TID",
     "IWX TVQM RA/TID ownership"),
    ("client->clientTxBaMask |= bit", "IWX agreement transition"),
    ("client->queueId == qid", "IWX AP BA queue owner"),
    ("apRunning", "IWX AP-only BA completion admission"),
    ("itl_ap_tx_ba_advance_sequence", "IWX firmware sequence mirror"),
    ("airportItlwmRequestAPTxDequeue", "IWX Skywalk wake"),
    ("IWX AP TX ADDBA response", "IWX response witness"),
):
    require(iwx, needle, label)
ordered(iwx, "itl_ap_tx_ba_response_matches(txBa, &action)",
        "iwx_ap_set_client_tx_ba(", "IWX validate before agreement transition")

print("PASS: IWN/IWM/IWX AP transmit A-MPDU ADDBA/DELBA contract")
PY
