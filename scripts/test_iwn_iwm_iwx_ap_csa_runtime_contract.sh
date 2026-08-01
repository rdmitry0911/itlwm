#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

python3 - "$PROJECT_DIR" <<'PY'
from pathlib import Path
import sys

root = Path(sys.argv[1])
hal = (root / "include/HAL/ItlHalService.hpp").read_text()
runtime = (root / "include/HAL/ItlApFirmwareRuntime.hpp").read_text()
owner = (root / "AirportItlwm/AirportItlwmAPSTAOwner.cpp").read_text()
iwn = (root / "itlwm/hal_iwn/ItlIwn.cpp").read_text()
iwn_reg = (root / "itlwm/hal_iwn/if_iwnreg.h").read_text()
iwm = (root / "itlwm/hal_iwm/ItlIwm.cpp").read_text()
iwx = (root / "itlwm/hal_iwx/ItlIwx.cpp").read_text()
probe = (root / "AirportItlwmLabAPProbe/airport_itlwm_lab_ap_probe.c").read_text()


def require(source: str, needle: str, label: str) -> None:
    assert needle in source, f"missing {label}: {needle}"


for needle, label in (
    ("uint8_t mode;", "HAL CSA mode carrier"),
    ("uint8_t count;", "HAL private countdown carrier"),
):
    require(hal, needle, label)

for needle, label in (
    ("itl_ap_beacon_begin_csa", "CSA beacon insertion"),
    ("kItlApCsaElementId = 37", "802.11 CSA IE"),
    ("itl_ap_beacon_set_csa_count", "3-2-1 countdown update"),
    ("itl_ap_beacon_end_csa", "CSA terminal removal"),
    ("itl_ap_beacon_set_channel", "DS/HT channel commit"),
):
    require(runtime, needle, label)

for needle, label in (
    ("state.resetFlag329 |= kAirportItlwmAPSTACsaResetFlagBit;",
     "Apple running-edge CSA gate"),
    ("csa.mode = in->mode10;", "Apple +0x10 mode mapping"),
    ("csa.count = kItlApCsaDefaultCount;", "private default countdown"),
):
    require(owner, needle, label)

assert "IWN_CMD_WIPAN_P2P_CHANNEL_SWITCH" not in iwn_reg
assert "IWN_CMD_WIPAN_P2P_CHANNEL_SWITCH" not in iwn
for needle, label in (
    ("IWN_AP_STAGE_INITIAL_RXON", "proven DVM PAN restart state machine"),
    ("IWN_CMD_WIPAN_RXON", "safe CP/PAN firmware rebind"),
    ("IWN_AP_CSA_CLIENT_RESTORE_PREPARED",
     "connected-client preservation across rebind"),
    ("iwn_add_ap_client_node(apClientMac)",
     "target-channel client node recreation"),
    ("IWN_AP_CSA_CLIENT_RESTORE_GROUP_KEY",
     "target-channel group-key recreation"),
    ("IWN_AP_CSA_CLIENT_RESTORE_PAIRWISE_KEY",
     "target-channel pairwise-key recreation"),
    ("!apClientNodeInstalled", "data fence until restored ADD_STA"),
    ("IWN_AP_TRANSITION_SCAN_WAIT_SECONDS = 8",
     "dual-band scan terminal deadline"),
    ("iwn_set_ap_primary_tx_quiesced(false, false)",
     "failed AP-start TX rollback"),
):
    require(iwn, needle, f"IWN {label}")

for source, prefix in ((iwm, "iwm"), (iwx, "iwx")):
    for needle, label in (
        (f"{prefix}_ap_binding_cmd(sc, runtime, false)",
         "old binding withdrawal"),
        (f"{prefix}_phy_ctxt_update", "target PHY context update"),
        (f"{prefix}_ap_mac_ctxt_cmd", "live GO MAC context update"),
        (f"{prefix}_ap_binding_cmd(sc, runtime, true)",
         "target binding publication"),
        ("rollback_context:", "transactional context rollback"),
    ):
        require(source, needle, f"{prefix.upper()} {label}")

for source, label, countdown in (
    (iwn, "IWN", "apCsaCount > 1"),
    (iwm, "IWM", "csaCount > 1"),
    (iwx, "IWX", "csaCount > 1"),
):
    require(source, "triggerAPCSA(const struct ItlHalApCSA *csa)",
            f"{label} HAL implementation")
    require(source, countdown, f"{label} countdown")
    require(source, "timeout_add_msec", f"{label} TBTT-bounded scheduling")
    require(source, "AP CSA complete channel=", f"{label} terminal evidence")

for needle, label in (
    ("APPLE80211_IOC_SOFTAP_TRIGGER_CSA 349", "public selector 349"),
    ("struct airport_itlwm_softap_csa", "exact 0x15 carrier"),
    ("csa.mode10 = (uint8_t)csa_mode;", "public CSA mode input"),
    ("APPLE80211_IOC_SOFTAP_TRIGGER_CSA, 0", "live ioctl trigger"),
):
    require(probe, needle, label)

print("PASS: IWN/IWM/IWX AP CSA countdown and live channel-context contract")
PY
