#!/bin/bash
set -euo pipefail

repo_root=$(cd "$(dirname "$0")/.." && pwd)

python3 - "$repo_root" <<'PY'
from pathlib import Path
import sys

root = Path(sys.argv[1])
owner = (root / "AirportItlwm/AirportItlwmAPSTAOwner.cpp").read_text()
owner_hpp = (root / "AirportItlwm/AirportItlwmAPSTAOwner.hpp").read_text()
controller = (root / "AirportItlwm/AirportItlwmV2.cpp").read_text()
controller_hpp = (root / "AirportItlwm/AirportItlwmV2.hpp").read_text()
iwn = (root / "itlwm/hal_iwn/ItlIwn.cpp").read_text()
iwn_hpp = (root / "itlwm/hal_iwn/ItlIwn.hpp").read_text()

for field in (
    "bool radioResetResumePending;",
    "bool radioResetWaitForPrimaryStaRun;",
    "uint16_t radioResetResumeWaitTicks;",
):
    assert field in owner_hpp, f"missing retained AP wake field: {field}"

prepare = owner[
    owner.index("void AirportItlwmAPSTAOwner::prepareForRadioReset()"):
    owner.index("IOReturn AirportItlwmAPSTAOwner::resumeAfterRadioReset()")
]
resume = owner[
    owner.index("IOReturn AirportItlwmAPSTAOwner::resumeAfterRadioReset()"):
    owner.index("void AirportItlwmAPSTAOwner::teardown()")
]

assert "radioResetWaitForPrimaryStaRun ||" in prepare
assert "ic->ic_state == IEEE80211_S_RUN" in prepare, \
    "sleep must preserve the last live primary-BSS observation"
assert prepare.index("radioResetWaitForPrimaryStaRun =") < \
       prepare.index("radioResetResumePending = true;"), \
    "the primary-STA replay dependency must be captured before AP replay arms"

runtime_sample = resume[
    resume.index("if (!radioResetResumePending)"):
    resume.index("if (radioResetWaitForPrimaryStaRun)")
]
assert "isApRunning()" in runtime_sample
assert "ic->ic_state == IEEE80211_S_RUN" in runtime_sample
assert "radioResetWaitForPrimaryStaRun =" in runtime_sample, \
    "watchdog must retain a pre-transition primary-STA RUN observation"

assert "if (radioResetWaitForPrimaryStaRun)" in resume
assert "ic->ic_state != IEEE80211_S_RUN" in resume
assert "return kIOReturnNotReady;" in resume
assert resume.index("ic->ic_state != IEEE80211_S_RUN") < \
       resume.index("const IOReturn result = startLowerIfReady();"), \
    "PAN replay must not precede the post-reset primary BSS RUN boundary"
assert "kAirportItlwmAPSTARadioResetPrimaryStaWaitTicks" in resume, \
    "AP-only fallback must remain bounded"
assert "radioResetWaitForPrimaryStaRun = false;" in resume

assert "bool apPrimaryTxQuiesced;" in iwn_hpp
assert "bool apStaBssAssociated;" in iwn_hpp
assert "bool iwn_ap_primary_tx_pending() const;" in iwn_hpp
start_ap = iwn[
    iwn.index("IOReturn ItlIwn::startAPMode("):
    iwn.index("IOReturn ItlIwn::stopAPMode()")
]
quiesce = start_ap.index(
    "iwn_set_ap_primary_tx_quiesced(true, false);"
)
drain = start_ap.index("iwn_ap_primary_tx_pending()", quiesce)
initial_rxon = start_ap.index(
    "iwn_cmd(&com, IWN_CMD_WIPAN_RXON", drain
)
assert quiesce < drain < initial_rxon, \
    "retained PAN replay must fence/drain primary TX before WIPAN_RXON"
assert "return kIOReturnNotReady;" in start_ap[drain:initial_rxon]

events = iwn[
    iwn.index("void ItlIwn::iwn_note_ap_firmware_event("):
    iwn.index("IOReturn ItlIwn::startAPMode(")
]
final_power = events.index("IWN_AP_STAGE_FINAL_POWER")
final_pan = events.index(
    "apFirmwareStage = IWN_AP_STAGE_FINAL_PAN_PARAMS;", final_power
)
balanced_pan = events.index(
    "iwn_send_ap_pan_params(&apFirmwareConfig);", final_pan
)
final_pan_reply = events.index(
    "apFirmwareStage == IWN_AP_STAGE_FINAL_PAN_PARAMS", balanced_pan
)
resume_tx = events.index(
    "iwn_set_ap_primary_tx_quiesced(false, true);", final_pan_reply
)
assert final_power < final_pan < balanced_pan < final_pan_reply < resume_tx, \
    "primary output must resume only after steady PAN scheduling completes"

pan_params = iwn[
    iwn.index("int ItlIwn::iwn_send_ap_pan_params("):
    iwn.index("int ItlIwn::iwn_set_ap_sta_scan_priority(")
]
assert "apFirmwareStage == IWN_AP_STAGE_FINAL_PAN_PARAMS" in pan_params
assert "bssSlotWidth = beaconInterval / 2;" in pan_params
assert "panSlotWidth = beaconInterval - bssSlotWidth;" in pan_params

clear_oactive = iwn[
    iwn.index("iwn_clear_oactive(struct iwn_softc *sc,"):
    iwn.index("bool ItlIwn::\niwn_tx_pending(")
]
assert "if (that->apPrimaryTxQuiesced)" in clear_oactive, \
    "ordinary TX_DONE must not reopen a transition-fenced primary queue"

reset_ring = iwn[
    iwn.index("iwn_reset_tx_ring(struct iwn_softc *sc,"):
    iwn.index("void ItlIwn::\niwn_free_tx_ring(")
]
assert "ring->read = 0;" in reset_ring, \
    "radio reset must retire the software read cursor with cur/queued"

watchdog = controller[
    controller.index("void AirportItlwm::watchdogAction("):
    controller.index("#if __IO80211_TARGET >= __MAC_26_0",
                     controller.index("void AirportItlwm::watchdogAction("))
]
assert "gate->runAction(resumeAPSTAAfterRadioResetGated)" in watchdog
assert "#define kWatchDogTimerPeriod 1000" in controller_hpp, \
    "bounded wait ticks rely on the one-second controller watchdog"

print("PASS: Tahoe APSTA wake replays drained primary BSS before retained PAN")
PY
