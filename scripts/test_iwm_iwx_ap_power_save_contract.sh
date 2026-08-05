#!/bin/sh
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

python3 - "$repo_root" <<'PY'
from pathlib import Path
import sys

root = Path(sys.argv[1])
state = (root / "include/HAL/ItlApFirmwareRuntime.hpp").read_text()
framing = (root / "include/HAL/ItlApOpenRuntime.hpp").read_text()
iwm_front = (root / "itlwm/hal_iwm/ItlIwm.cpp").read_text()
iwm_back = (root / "itlwm/hal_iwm/mac80211.cpp").read_text()
iwx = (root / "itlwm/hal_iwx/ItlIwx.cpp").read_text()


def require(source: str, needle: str, label: str) -> None:
    if needle not in source:
        raise SystemExit(f"FAIL: missing {label}: {needle}")


for needle, label in (
    ("kPowerSaveQueueLength = 16", "bounded AP PS queue"),
    ("mbuf_t powerSaveQueue", "driver-owned sleeping-client packets"),
    ("bool clientPowerSave", "client PM state"),
    ("bool timSet", "TIM state"),
    ("itl_ap_firmware_power_save_purge(client)", "disconnect/sleep purge"),
    ("mbuf_freem(packet)", "queued packet release"),
):
    require(state, needle, label)

for needle, label in (
    ("itl_ap_power_save_should_buffer", "unicast PS gate"),
    ("ethernet.ether_type != htons(ETHERTYPE_PAE)", "EAPOL bypass"),
    ("itl_ap_power_save_enqueue", "FIFO enqueue"),
    ("itl_ap_power_save_requeue_front", "failed delivery rollback"),
    ("bitmapOffset = runtime->beacon[offset + 4] & 0xfe",
     "partial virtual bitmap offset"),
    ("aidByte = client->clientAid >> 3", "per-client AID bitmap selection"),
    ("IEEE80211_FC1_PWR_MGT", "station PM-bit observation"),
    ("kItlApOpenRxPowerState", "null-data PM edge"),
    ("IEEE80211_FC0_SUBTYPE_PS_POLL", "PS-Poll classification"),
    ("LE_READ_2(poll->i_aid) & 0x3fff", "PS-Poll AID fence"),
    ("IEEE80211_FC1_MORE_DATA", "More Data delivery flag"),
):
    require(framing, needle, label)

for family, front, back, prefix in (
    ("IWM", iwm_front, iwm_back, "IWM"),
    ("IWX", iwx, iwx, "IWX"),
):
    for needle, label in (
        ("itl_ap_power_save_should_buffer", "sleeping unicast buffer"),
        ("itl_ap_power_save_set_tim(&apRuntime, client, true", "TIM arm"),
        ("queueWasEmpty", "failed first-TIM enqueue rollback"),
        ("itl_ap_power_save_dequeue(client)", "ownership rollback"),
    ):
        require(front, needle, f"{family} {label}")
    for needle, label in (
        (f"{prefix}_STA_MODIFY_SLEEPING_STA_TX_COUNT",
         "firmware PS release count"),
        (f"{prefix}_STA_SLEEP_STATE_PS_POLL", "firmware PS-Poll reason"),
        (f"{prefix}_STA_SLEEP_STATE_MOREDATA", "firmware More Data state"),
        (f"{prefix}_STA_FLG_PS", "firmware awake transition"),
        ("sleep_tx_count = htole16(1)", "one-frame service period"),
        ("itl_ap_power_save_requeue_front", "failed raw TX requeue"),
        ("powerSaveQueueCount != 0", "awake queue drain"),
        ("kItlApOpenRxPsPoll", "one-frame PS-Poll delivery"),
        ("iwm_ap_update_power_save_tim" if family == "IWM" else
         "iwx_ap_update_power_save_tim", "firmware beacon TIM update"),
    ):
        require(back, needle, f"{family} {label}")

# The IWX backend is always the new TX API.  Firmware has already committed
# the peer wake transition when RX reports the PM-bit change; issuing a
# synchronous ADD_STA wake command from iwx_ap_handle_rx both diverges from
# iwlwifi and fails with EWOULDBLOCK on the notification workloop.
iwx_rx = iwx[iwx.index("bool ItlIwx::\niwx_ap_handle_rx"):
             iwx.index("int ItlIwx::\niwx_ap_update_quotas")]
if "iwx_ap_modify_client_power_state(\n                this, sc, &apRuntime, client, true, false)" in iwx_rx:
    raise SystemExit("FAIL: IWX new-TX RX path must not issue ADD_STA wake")
require(iwx_rx, "client->clientPowerSave = result.powerSave",
        "IWX firmware-owned peer PM transition")
require(iwx_rx, "reserve\n         * ADD_STA sleep_tx_count for an actual PS-Poll frame release",
        "IWX new-TX peer wake rationale")

print("PASS: paired IWM/IWX AP client power-save/TIM/PS-Poll contract")
PY
