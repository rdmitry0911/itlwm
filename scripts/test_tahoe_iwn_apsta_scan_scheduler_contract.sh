#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

python3 - "$PROJECT_DIR" <<'PY'
from pathlib import Path
import sys

root = Path(sys.argv[1])
iwn_hpp = (root / "itlwm/hal_iwn/ItlIwn.hpp").read_text()
iwn = (root / "itlwm/hal_iwn/ItlIwn.cpp").read_text()

assert "bool apStaPanPriorityActive;" in iwn_hpp
assert "int iwn_set_ap_sta_pan_priority(bool);" in iwn_hpp

pan = iwn[
    iwn.index("int ItlIwn::iwn_send_ap_pan_params("):
    iwn.index("int ItlIwn::iwn_send_ap_stop_pan_params()")
]
for needle in (
    "static_cast<uint32_t>(dtimPeriod) * beaconInterval * 3",
    "const uint16_t minimumSlotWidth = 20;",
    "if (apStaPanPriorityActive)",
    "bssSlotWidth = admissionRemainder;",
    "panSlotWidth = minimumSlotWidth;",
    "bssSlotWidth = minimumSlotWidth;",
    "panSlotWidth = admissionRemainder;",
):
    assert needle in pan, f"missing DVM APSTA PAN policy: {needle}"

submit = iwn[
    iwn.index("iwn_scan_submit(struct iwn_softc *sc, uint16_t flags"):
    iwn.index("void ItlIwn::\niwn_scan_abort(")
]
priority = submit.index("iwn_set_ap_sta_pan_priority(true)")
doorbell = submit.index(
    "iwn_cmd_with_doorbell_hook(sc, IWN_CMD_SCAN"
)
assert priority < doorbell, \
    "STA PAN priority must cross the command ring before IWN_CMD_SCAN"
assert "if (ap_sta_pan_priority_changed)" in submit
assert "iwn_set_ap_sta_pan_priority(false)" in submit

stop = iwn[
    iwn.index("case IWN_STOP_SCAN:"):
    iwn.index("case IWN5000_CALIBRATION_RESULT:")
]
continuation = stop.index(
    "iwn_scan_continue(sc, IEEE80211_CHAN_5GHZ"
)
continuation_break = stop.index("break;", continuation)
restore = stop.index("iwn_set_ap_sta_pan_priority(false)")
assert continuation < continuation_break < restore, \
    "2.4 -> 5 GHz continuation must retain STA PAN priority"
assert stop.index("iwn_scan_lease_claim_terminal") < restore, \
    "only an exact final scan terminal may restore APSTA scheduling"

auth = iwn[
    iwn.index("iwn_auth(struct iwn_softc *sc, int arg)"):
    iwn.index("int ItlIwn::\niwn_run(")
]
assert auth.index("iwn_set_ap_sta_pan_priority(true)") < auth.index(
    "iwn_cmd(sc, IWN_CMD_RXON"
), "AUTH must receive its DVM admission window before RXON"

run = iwn[
    iwn.index("iwn_run(struct iwn_softc *sc)"):
    iwn.index("iwn_pae_mfp_txn_submit(")
]
assert run.index("iwn_add_bss_node(sc, ni)") < run.index(
    "iwn_set_ap_sta_pan_priority(false)"
), "associated STA/AP balance requires the committed BSS context"

reset = iwn[
    iwn.index("void ItlIwn::iwn_reset_ap_runtime_state()"):
    iwn.index("void ItlIwn::iwn_set_ap_scan_transition_blocked(")
]
assert "apStaPanPriorityActive = false;" in reset

print("PASS: Tahoe IWN APSTA scan/auth PAN scheduler contract")
PY
