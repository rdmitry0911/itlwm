#!/bin/sh
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

python3 - "$repo_root" <<'PY'
from pathlib import Path
import sys

root = Path(sys.argv[1])
shared = (root / "include/HAL/ItlApOpenRuntime.hpp").read_text()
runtime = (root / "include/HAL/ItlApFirmwareRuntime.hpp").read_text()
iwm_hal = (root / "itlwm/hal_iwm/ItlIwm.cpp").read_text()
iwm_mac = (root / "itlwm/hal_iwm/mac80211.cpp").read_text()
iwm_rx = (root / "itlwm/hal_iwm/rx.cpp").read_text()
iwm_reg = (root / "itlwm/hal_iwm/if_iwmreg.h").read_text()
iwm_tx = (root / "itlwm/hal_iwm/tx.cpp").read_text()
iwx = (root / "itlwm/hal_iwx/ItlIwx.cpp").read_text()
bridge = (root / "itlwm/hal_iwn/ItlIwn.cpp").read_text()


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
    ("config->authUpper == 0", "open auth admission"),
    ("config->credentialLength == 0", "empty credential admission"),
    ("config->rsnIELength == 0", "empty RSN admission"),
    ("IEEE80211_FC0_SUBTYPE_PROBE_RESP", "probe response"),
    ("IEEE80211_AUTH_OPEN_RESPONSE", "open auth response"),
    ("IEEE80211_FC0_SUBTYPE_ASSOC_RESP", "association response"),
    ("IEEE80211_FC1_DIR_TODS", "ToDS decapsulation"),
    ("IEEE80211_FC1_DIR_FROMDS", "FromDS encapsulation"),
    ("LLC_SNAP_LSAP", "LLC/SNAP conversion"),
    ("kItlApOpenRxDisconnect", "disconnect classification"),
):
    require(shared, needle, label)

for field in (
    "queueId", "clientAid", "clientMac", "clientStationMac",
    "clientAssociationPending", "clientAuthenticated", "clientAssociated",
    "clientAuthorized", "clientStationInstalled",
):
    require(runtime, field, f"runtime {field}")

for family, hal, lower, task_sig, rx_sig, add_name in (
    ("IWM", iwm_hal, iwm_mac, "iwm_ap_client_task(void *arg)",
     "iwm_ap_handle_rx(struct iwm_softc *sc,", "iwm_ap_add_client_sta"),
    ("IWX", iwx, iwx, "iwx_ap_client_task(void *arg)",
     "iwx_ap_handle_rx(struct iwx_softc *sc,", "iwx_ap_add_client_sta"),
):
    start = body(hal, "startAPMode(const struct ItlHalApConfig *config)")
    require(start, "itl_ap_client_config_supported(config)",
            f"{family} open/WPA2 start")
    require(hal, "transmitAPData(mbuf_t packet)", f"{family} AP TX entry")
    require(hal, "getAPTxFreeSpace() const", f"{family} AP free space")
    free_space = body(hal, "getAPTxFreeSpace() const")
    require(free_space, "client->clientAssociated",
            f"{family} pre-association AP TX gate")
    require(lower, "ap_frame = true", f"{family} raw AP TX ownership")
    task = body(lower, task_sig)
    require(task, "ItlApFirmwareClientRuntime *client",
            f"{family} per-client firmware state")
    require(task, "for (size_t index = 0; index < limit; index++)",
            f"{family} bounded client task table")
    require(task, add_name, f"{family} deferred firmware station add")
    require(task, "itl_ap_open_build_assoc_success", f"{family} deferred assoc reply")
    rx = body(lower, rx_sig)
    require(rx, "clientAssociationPending = true", f"{family} RX association latch")
    require(rx, "ap_client_task", f"{family} association task enqueue")
    if add_name in rx:
        raise SystemExit(f"FAIL: {family} RX interrupt synchronously adds firmware station")
    require(rx, "ml_enqueue(apFrames, result.ethernetPacket)",
            f"{family} AP Ethernet RX handoff")
    require(lower, "airportItlwmRequestAPTxDequeue", f"{family} completion dequeue")

require(body(iwm_mac, "iwm_ap_add_internal_sta(struct iwm_softc *sc,"),
        "command.assoc_id = htole16(assocId)",
        "IWM ADD_STA AID")
require(iwm_reg, "IWM_DQA_AP_CLIENT_QUEUE", "IWM dedicated AP client queue")
require(iwm_reg, "IWM_DQA_AP_CLIENT_QUEUE_COUNT 4",
        "IWM four dedicated AP client queues")
require(body(iwm_tx, "iwm_alloc_tx_ring(iwm_softc *sc,"),
        "qid >= IWM_DQA_AP_CLIENT_QUEUE + IWM_DQA_AP_CLIENT_QUEUE_COUNT",
        "IWM AP client DMA ring allocation")
require(body(iwm_mac, "iwm_ap_add_client_sta(struct iwm_softc *sc,"),
        "IWM_DQA_AP_CLIENT_QUEUE + clientIndex",
        "IWM AP client queue selection")
require(body(iwx, "iwx_ap_add_internal_sta(struct iwx_softc *sc,"),
        "command.assoc_id = htole16(assocId)",
        "IWX ADD_STA AID")

require(iwm_rx, "iwm_ap_handle_rx(sc, m, mbuf_pkthdr_len(m),",
        "IWM RX role classifier")
require(iwm_mac, "if_input_ap(&sc->sc_ic.ic_if, &apMl);", "IWM AP RX drain")
iwm_completion = body(iwm_mac,
    "iwm_rx_tx_cmd(struct iwm_softc *sc, struct iwm_rx_packet *pkt,")
if not (iwm_completion.find("if (txd->ap_frame)") <
        iwm_completion.find("if (qid > IWM_LAST_AGG_TX_QUEUE)")):
    raise SystemExit("FAIL: IWM AP DQA completion is behind STA agg-QID reject")
require(iwm_completion, "IWM_AGG_SSN_TO_TXQ_IDX(ssn)",
        "IWM AP completion reclaim")
require(iwx, "iwx_ap_handle_rx(sc, m, mbuf_pkthdr_len(m),",
        "IWX RX role classifier")
require(iwx, "if_input_ap(&sc->sc_ic.ic_if, &apMl);", "IWX AP RX drain")

query = body(bridge, "airportItlwmQueryAPTxFreeSpace(ItlHalService *service)")
for helper in (
    "ItlIwn", "airportItlwmQueryIwmAPTxFreeSpace",
    "airportItlwmQueryIwxAPTxFreeSpace",
):
    require(query, helper, f"paired AP free-space bridge {helper}")

iwx_raw = body(iwx, "iwx_ap_send_raw_frame(struct iwx_softc *sc,")
if "qfullmsk" in iwx_raw or "1 << ring->qid" in iwx_raw:
    raise SystemExit("FAIL: IWX dynamic AP QID must not enter 32-bit qfull mask")
require(iwx_raw, "ring->qid & 0x1f", "IWX firmware TX sequence QID")

iwx_reset = body(iwx, "iwx_reset_tx_ring(struct iwx_softc *sc,")
require(iwx_reset, "sizeof(sc->qfullmsk) * NBBY",
        "bounded IWX dynamic-QID reset")

print("PASS: paired IWM/IWX open-compatible AP runtime contract")
PY
