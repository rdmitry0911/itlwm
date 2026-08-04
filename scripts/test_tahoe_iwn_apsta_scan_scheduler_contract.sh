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

assert "bool apStaScanPriorityActive;" in iwn_hpp
assert "bool apStaAuthPriorityActive;" in iwn_hpp
assert "int iwn_set_ap_sta_scan_priority(bool);" in iwn_hpp
assert "int iwn_set_ap_sta_auth_priority(bool);" in iwn_hpp
assert "int iwn_clear_ap_sta_pan_priority();" in iwn_hpp

pan = iwn[
    iwn.index("int ItlIwn::iwn_send_ap_pan_params("):
    iwn.index("int ItlIwn::iwn_send_ap_stop_pan_params()")
]
for needle in (
    "static_cast<uint32_t>(dtimPeriod) * beaconInterval * 3",
    "const uint16_t minimumSlotWidth = 20;",
    "apStaScanPriorityActive || apStaAuthPriorityActive",
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
priority = submit.index("iwn_set_ap_sta_scan_priority(true)")
doorbell = submit.index(
    "iwn_cmd_with_doorbell_hook(sc, IWN_CMD_SCAN"
)
assert priority < doorbell, \
    "STA PAN priority must cross the command ring before IWN_CMD_SCAN"
assert "if (ap_sta_pan_priority_changed)" in submit
assert "iwn_set_ap_sta_scan_priority(false)" in submit

stop = iwn[
    iwn.index("case IWN_STOP_SCAN:"):
    iwn.index("case IWN5000_CALIBRATION_RESULT:")
]
continuation = stop.index(
    "iwn_scan_continue(sc, IEEE80211_CHAN_5GHZ"
)
continuation_break = stop.index("break;", continuation)
restore = stop.index("iwn_set_ap_sta_scan_priority(false)")
assert continuation < continuation_break < restore, \
    "2.4 -> 5 GHz continuation must retain STA PAN priority"
assert stop.index("iwn_scan_lease_claim_terminal") < restore, \
    "only an exact final scan terminal may restore APSTA scheduling"

auth = iwn[
    iwn.index("iwn_auth(struct iwn_softc *sc, int arg)"):
    iwn.index("int ItlIwn::\niwn_run(")
]
unassociated_rxon = auth.index("iwn_cmd(sc, IWN_CMD_RXON")
priority = auth.index("iwn_set_ap_sta_auth_priority(true)")
txpower = auth.index("ops->set_txpower(sc, 1)")
assert unassociated_rxon < priority < txpower, \
    "DVM recomputes PAN slots after the candidate-channel BSS RXON"

newstate = iwn[
    iwn.index("iwn_newstate(struct ieee80211com *ic"):
    iwn.index("void ItlIwn::\niwn_iter_func")
]
assert "authWillCommitRxon" in newstate
assert "nstate == IEEE80211_S_AUTH" in newstate
assert "!authWillCommitRxon" in newstate
assert "APSTA auth coalesced duplicate reset RXON" in newstate
rxon_reset = newstate.index("sc->rxon.associd = 0;")
rxon_filter_reset = newstate.index(
    "sc->rxon.filter &= ~htole32(IWN_FILTER_BSS);", rxon_reset
)
logical_assoc_reset = newstate.index(
    "that->apStaBssAssociated = false;", rxon_filter_reset
)
scan_submission = newstate.index(
    "that->iwn_scan(sc, IEEE80211_CHAN_2GHZ", logical_assoc_reset
)
assert rxon_reset < rxon_filter_reset < logical_assoc_reset < scan_submission, \
    "RUN -> SCAN must clear the APSTA BSS association before scan submission"

rsn_scan_fence = iwn[
    iwn.index("iwn_rsn_join_scan_blocked("):
    iwn.index("int ItlIwn::\niwn_newstate_preflight(")
]
for needle in (
    "ic->ic_state == IEEE80211_S_AUTH",
    "ic->ic_state == IEEE80211_S_ASSOC",
    "ic->ic_state == IEEE80211_S_RUN",
    "IEEE80211_F_RSNON",
    "ic->ic_bss == NULL",
    "!ic->ic_bss->ni_port_valid",
):
    assert needle in rsn_scan_fence, \
        f"missing protected auth/assoc/RUN join scan fence: {needle}"

preflight = iwn[
    iwn.index("iwn_newstate_preflight(struct ieee80211com *ic"):
    iwn.index("void ItlIwn::\niwn_scan_lease_replay_task")
]
assert preflight.index("iwn_rsn_join_scan_blocked(ic)") < \
    preflight.index("iwn_scan_lease_defer_scan"), \
    "RUN -> SCAN must be fenced before association epoch teardown"

scan_start = iwn[
    iwn.index("iwn_scan_start(struct iwn_softc *sc, uint16_t flags"):
    iwn.index("int ItlIwn::\niwn_scan(struct iwn_softc *sc")
]
assert scan_start.index("iwn_rsn_join_scan_blocked(ic)") < \
    scan_start.index("iwn_scan_lease_reserve"), \
    "every physical scan owner must be fenced before lease reservation"

run = iwn[
    iwn.index("iwn_run(struct iwn_softc *sc)"):
    iwn.index("iwn_pae_mfp_txn_submit(")
]
add_bss = run.index("iwn_add_bss_node(sc, ni)")
associated_rxon = run.index("iwn_cmd(sc, IWN_CMD_RXON")
replay_beacon = run.index("iwn_send_ap_beacon(&apFirmwareConfig)")
steady_pan = run.index("iwn_clear_ap_sta_pan_priority()")
assert associated_rxon < add_bss < replay_beacon < steady_pan, \
    "the retained PAN beacon must be replayed after BSS installation"

reset = iwn[
    iwn.index("void ItlIwn::iwn_reset_ap_runtime_state()"):
    iwn.index("void ItlIwn::iwn_set_ap_scan_transition_blocked(")
]
assert "apStaScanPriorityActive = false;" in reset
assert "apStaAuthPriorityActive = false;" in reset

scan_priority = iwn[
    iwn.index("int ItlIwn::iwn_set_ap_sta_scan_priority(bool active)"):
    iwn.index("int ItlIwn::iwn_set_ap_sta_auth_priority(bool active)")
]
assert "apStaScanPriorityActive = active;" in scan_priority
assert "if (active)\n        apStaAuthPriorityActive = false;" in scan_priority

auth_priority = iwn[
    iwn.index("int ItlIwn::iwn_set_ap_sta_auth_priority(bool active)"):
    iwn.index("int ItlIwn::iwn_clear_ap_sta_pan_priority()")
]
assert "apStaAuthPriorityActive = active;" in auth_priority
assert "apStaScanPriorityActive =" not in auth_priority, \
    "a late scan terminal must not erase an active AUTH owner"

print("PASS: Tahoe IWN APSTA scan/auth PAN scheduler contract")
PY
