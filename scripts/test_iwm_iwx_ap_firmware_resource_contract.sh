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

require(body(iwm_hal, "startAPMode(const struct ItlHalApConfig *config)"),
        "iwm_start_ap_resources(&com, &apRuntime)", "IWM HAL resource start")
require(body(iwm_hal, "stopAPMode()"),
        "iwm_stop_ap_resources(&com, &apRuntime)", "IWM HAL resource stop")
require(body(iwm_hal, "supportsAPMode() const"), "return false;",
        "IWM fail-closed public capability")

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

for field in ("byte_cnt", "flags", "template_id", "tim_idx", "tim_size",
              "ecsa_offset", "csa_offset", "frame[0]"):
    require(iwx_reg, field, f"IWX modern beacon {field}")
require(iwx_hal, "commandVersion != 11 && commandVersion != 12",
        "IWX API-68 beacon version gate")
require(iwx_hal, "IWX_MAC_BEACON_CCK", "IWX v11 beacon rate flag")
require(iwx_hal, "iwx_read_prph(sc, IWX_DEVICE_SYSTEM_TIME_REG)",
        "IWX firmware system-time TBTT")
require(iwx_hal, "IWX_FW_CTXT_ACTION_MODIFY", "IWX same-PHY binding modify")

iwx_station = body(iwx_hal, "iwx_ap_add_internal_sta(struct iwx_softc *sc,")
require_order(iwx_station, [
    "iwx_send_cmd_pdu_status(sc, IWX_ADD_STA",
    "iwx_tvqm_enable_txq_for_sta(",
], "IWX API-68 add-station-before-queue")
require(iwx_station, "*queueId = (uint16_t)assignedQueue;",
        "IWX firmware-assigned internal queue ownership")

iwx_start = body(iwx_hal, "iwx_start_ap_mode(struct iwx_softc *sc,")
require_order(iwx_start, [
    "iwx_ap_send_beacon_template(sc, runtime)",
    "iwx_ap_mac_ctxt_cmd(sc, runtime, IWX_FW_CTXT_ACTION_ADD)",
    "iwx_ap_binding_cmd(sc, runtime, true)",
    "runtime->multicastStaId,",
    "runtime->broadcastStaId,",
    "iwx_ap_update_quotas(sc, runtime, true)",
], "IWX Linux-donor AP bring-up")
require(iwx_start, "&runtime->multicastQueueId", "IWX multicast TVQM queue")
require(iwx_start, "&runtime->broadcastQueueId", "IWX broadcast TVQM queue")

iwx_stop = body(iwx_hal, "iwx_stop_ap_mode(struct iwx_softc *sc,")
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
