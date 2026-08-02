#!/bin/sh
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

python3 - "$repo_root" <<'PY'
from pathlib import Path
import sys

root = Path(sys.argv[1])
runtime = (root / "include/HAL/ItlApFirmwareRuntime.hpp").read_text()
framing = (root / "include/HAL/ItlApOpenRuntime.hpp").read_text()
hal = (root / "include/HAL/ItlHalService.hpp").read_text()
owner = (root / "AirportItlwm/AirportItlwmAPSTAOwner.cpp").read_text()
iwn_h = (root / "itlwm/hal_iwn/ItlIwn.hpp").read_text()
iwn = (root / "itlwm/hal_iwn/ItlIwn.cpp").read_text()
iwm_front = (root / "itlwm/hal_iwm/ItlIwm.cpp").read_text()
iwm_back = (root / "itlwm/hal_iwm/mac80211.cpp").read_text()
iwm_reg = (root / "itlwm/hal_iwm/if_iwmreg.h").read_text()
iwm_tx = (root / "itlwm/hal_iwm/tx.cpp").read_text()
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
    ("kItlApFirmwareMaxClients = 5", "reference five-client table"),
    ("ItlApFirmwareClientRuntime clients[kItlApFirmwareMaxClients]",
     "runtime client slots"),
    ("itl_ap_firmware_find_client", "station-MAC lookup"),
    ("itl_ap_firmware_allocate_client", "free-slot allocation"),
    ("runtime->firstClientStaId + index", "unique firmware station IDs"),
    ("client->clientAid = static_cast<uint16_t>(index + 1)", "unique AIDs"),
    ("itl_ap_firmware_find_tx_client", "destination-client TX routing"),
    ("itl_ap_firmware_set_client_limit", "live bounded maxassoc update"),
    ("effective = MAX(effective, static_cast<uint32_t>(index + 1))",
     "live-client-preserving maxassoc reduction"),
    ("itl_ap_firmware_client_reset(&runtime->clients[index])",
     "all-client secret/queue teardown"),
):
    require(runtime, needle, label)

for per_client_state in (
    "clientPairwiseKey[16]", "clientRxPn[16]", "struct ieee80211_sae_ap *sae",
    "uint8_t pmk[IEEE80211_PMK_LEN]", "struct ieee80211_ptk ptk",
    "uint64_t replayCounter", "mbuf_t powerSaveQueue", "bool timSet",
):
    require(runtime, per_client_state, f"per-client state {per_client_state}")

for needle, label in (
    ("size_t clientIndex", "RX client identity"),
    ("IEEE80211_STATUS_TOOMANY", "full-table rejection"),
    ("itl_ap_firmware_client_reset(client);", "failed SAE slot reclamation"),
    ("itl_ap_firmware_find_client(runtime, wh->i_addr2)",
     "per-source data lookup"),
    ("itl_ap_firmware_find_client(runtime, poll->i_ta)",
     "per-source PS-Poll lookup"),
    ("client->clientRxPn[tid]", "per-client replay fence"),
    ("client->replayCounter", "per-client 4-way replay state"),
    ("client->clientAid & 7", "per-client TIM bit"),
):
    require(framing, needle, label)

require(hal, "virtual IOReturn setAPMaxStations(uint32_t maxStations)",
        "live maxassoc HAL bridge")
require(owner, "setAPMaxStations(payload)", "Apple MIS_MAX_STA live forwarding")

for needle, label in (
    ("struct IwnApClientRuntime", "IWN per-client runtime type"),
    ("apClients[kItlApFirmwareMaxClients]", "IWN five-client table"),
    ("uint8_t stationId;", "IWN per-client DVM station identity"),
    ("uint16_t aid;", "IWN per-client association identity"),
    ("struct ieee80211_key pairwiseSoftwareKey;",
     "IWN per-client software key"),
    ("struct ieee80211_sae_ap *sae;", "IWN per-client SAE owner"),
    ("mbuf_t psQueue[IWN_AP_PS_QUEUE_LEN]", "IWN per-client PS queue"),
    ("struct ItlApRxBaRuntime rxBa[kItlApRxBaTidCount]",
     "IWN per-client RX BA"),
    ("struct ItlApTxBaRuntime txBa[kItlApRxBaTidCount]",
     "IWN per-client TX BA"),
):
    require(iwn_h, needle, label)

iwn_allocate = body(iwn, "ItlIwn::iwn_allocate_ap_client(")
require(iwn_allocate, "IWN5000_ID_PAN_CLIENT + freeIndex",
        "IWN unique dynamic station IDs")
require(iwn_allocate, "client->aid = static_cast<uint16_t>(freeIndex + 1)",
        "IWN unique AIDs")
iwn_materialize = body(iwn, "ItlIwn::iwn_submit_next_ap_client_materialization()")
require(iwn_materialize, "apClients[index].commandPending",
        "IWN serialized ADD_STA owner")
require(iwn_materialize, "iwn_add_ap_client_node(client->mac)",
        "IWN per-slot ADD_STA")
for needle, label in (
    ("iwn_find_ap_client(request->i_addr2)", "IWN source-MAC RX routing"),
    ("completedClient = iwn_find_ap_client_by_id(",
     "IWN firmware-completion station-ID routing"),
    ("iwn_find_ap_client(ethernetHeader.ether_dhost)",
     "IWN unicast destination TX routing"),
    ("iwn_reset_ap_client(&apClients[index], true, true)",
     "IWN all-client runtime teardown"),
    ("apCsaRestoreIndex < kItlApFirmwareMaxClients",
     "IWN all-client CSA restore walk"),
):
    require(iwn, needle, label)
iwn_maxassoc = body(iwn, "setAPMaxStations(uint32_t maxStations)")
require(iwn_maxassoc,
        "effective = MAX(effective, static_cast<uint32_t>(index + 1))",
        "IWN live-client-preserving maxassoc reduction")
iwn_key = body(iwn, "iwn_install_ap_ccmp_key(bool pairwise")
require(iwn_key, "apClientContext->stationId : IWN5000_ID_PAN_BROADCAST",
        "IWN per-client PTK station ID")

for family, front, back, task_signature, rx_signature in (
    ("IWM", iwm_front, iwm_back, "iwm_ap_client_task(void *arg)",
     "iwm_ap_handle_rx(struct iwm_softc *sc,"),
    ("IWX", iwx, iwx, "iwx_ap_client_task(void *arg)",
     "iwx_ap_handle_rx(struct iwx_softc *sc,"),
):
    maxassoc = body(front, "setAPMaxStations(uint32_t maxStations)")
    require(maxassoc, "itl_ap_firmware_set_client_limit",
            f"{family} live cap")
    task = body(back, task_signature)
    require(task, "for (size_t index = 0; index < limit; index++)",
            f"{family} all-client association worker")
    require(task, "ItlApFirmwareClientRuntime *client",
            f"{family} per-client association state")
    rx = body(back, rx_signature)
    require(rx, "result.clientIndex", f"{family} RX client routing")
    require(rx, "itl_ap_firmware_client_reset(client)",
            f"{family} isolated disconnect teardown")
    stop_signature = "iwm_stop_ap_resources(struct iwm_softc *sc," if family == "IWM" else \
                     "iwx_stop_ap_mode(struct iwx_softc *sc,"
    stop = body(back, stop_signature)
    require(stop,
            "for (size_t index = 0; index < kItlApFirmwareMaxClients; index++)",
            f"{family} all-client firmware teardown")
    set_key = body(front, "setAPKey(const struct ItlHalApKey *key)")
    require(set_key, "2 + itl_ap_firmware_client_index(&apRuntime, client)",
            f"{family} unique PTK slot")
    require(set_key, "2 + kItlApFirmwareMaxClients",
            f"{family} non-colliding GTK slot")
    require(set_key, "timingsafe_bcmp(apRuntime.groupKey, key->keyData",
            f"{family} idempotent shared GTK install")

require(iwm_reg, "IWM_DQA_AP_CLIENT_QUEUE_COUNT 5",
        "five IWM AP data queues")
require(iwm_back, "IWM_DQA_AP_CLIENT_QUEUE + clientIndex",
        "IWM slot-to-queue mapping")
require(iwm_tx, "IWM_DQA_AP_CLIENT_QUEUE + IWM_DQA_AP_CLIENT_QUEUE_COUNT",
        "IWM DMA allocation for all AP queues")
require(body(iwx, "iwx_ap_add_client_sta(struct iwx_softc *sc,"),
        "iwx_ap_add_internal_sta", "IWX per-client TVQM allocation")

print("PASS: IWN/IWM/IWX bounded five-client AP contract")
PY
