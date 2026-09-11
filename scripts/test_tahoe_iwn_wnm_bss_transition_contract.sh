#!/usr/bin/env bash
# Source contract for the runtime-proven IWN 802.11v BTM reconnect path.
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

python3 - "$root" <<'PY'
from pathlib import Path
import sys


root = Path(sys.argv[1])
core_c = (root / "itl80211/openbsd/net80211/ieee80211.c").read_text()
input_c = (root / "itl80211/openbsd/net80211/ieee80211_input.c").read_text()
pae_input_c = (root / "itl80211/openbsd/net80211/ieee80211_pae_input.c").read_text()
node_c = (root / "itl80211/openbsd/net80211/ieee80211_node.c").read_text()
output_c = (root / "itl80211/openbsd/net80211/ieee80211_output.c").read_text()
proto_c = (root / "itl80211/openbsd/net80211/ieee80211_proto.c").read_text()
proto_h = (root / "itl80211/openbsd/net80211/ieee80211_proto.h").read_text()
skywalk = (root / "AirportItlwm/AirportItlwmSkywalkInterface.cpp").read_text()
iwn = (root / "itlwm/hal_iwn/ItlIwn.cpp").read_text()
var_h = (root / "itl80211/openbsd/net80211/ieee80211_var.h").read_text()


def fail(message: str) -> None:
    raise SystemExit(f"IWN WNM BTM contract: {message}")


def require(text: str, token: str, label: str) -> None:
    if token not in text:
        fail(f"missing {label}: {token}")


def order(text: str, label: str, *tokens: str) -> None:
    cursor = 0
    for token in tokens:
        position = text.find(token, cursor)
        if position < 0:
            fail(f"{label} missing ordered token: {token}")
        cursor = position + len(token)


require(output_c, "IEEE80211_EXTCAP_BSS_TRANSITION >> 16",
        "association-request BSS Transition advertisement")
require(output_c, "ic->ic_caps & IEEE80211_C_WNM_BSS_TRANSITION",
        "backend-scoped BSS Transition advertisement")
require(iwn, "IEEE80211_C_WNM_BSS_TRANSITION",
        "IWN BSS Transition capability")
order(input_c, "protected BTM request ownership",
      "ieee80211_wnm_bss_transition_arm(ic, ni->ni_bssid,",
      "ieee80211_begin_wnm_bgscan(&ic->ic_if)",
      "ieee80211_wnm_bss_transition_defer_fresh_scan(ic)",
      "ieee80211_wnm_bss_transition_clear(ic);",
      "IEEE80211_WNM_BSS_TM_REJECT_NO_SUITABLE")
require(proto_c, "transition->candidate_confirmed = 1;",
        "fresh target confirmation")
require(proto_c, "transition->fresh_scan_pending = 1;",
        "busy-scan deferral owner")
order(proto_c, "exact Neighbor Report channel snapshot",
      "ieee80211_wnm_bss_transition_scan_start(struct ieee80211com *ic)",
      "transition->scan_starting = 1;",
      "ieee80211_wnm_bss_transition_target_channel(struct ieee80211com *ic,",
      "transition->active != 0 && transition->scan_starting != 0",
      "*target_channel = transition->target_channel;")
order(core_c, "BTM-only physical scan admission",
      "if (!ieee80211_wnm_bss_transition_scan_start(ic))",
      "error = ic->ic_bgscan_start(ic, 0);",
      "ieee80211_wnm_bss_transition_scan_end(ic);")
order(input_c, "BTM preempts an older background census",
      "ieee80211_wnm_bss_transition_defer_fresh_scan(ic)",
      "ic->ic_bgscan_abort != NULL",
      "ic->ic_bgscan_abort(ic) == 0",
      "timeout_add_msec(&ic->ic_wnm_bgscan_retry_timeout, 1)")
order(core_c, "bounded fresh-scan retry",
      "ieee80211_wnm_bgscan_retry_timeout(void *arg)",
      "ieee80211_begin_wnm_bgscan(ifp)",
      "ieee80211_wnm_bss_transition_retry_fresh_scan(ic)",
      "timeout_add_msec(&ic->ic_wnm_bgscan_retry_timeout, 100);",
      "IEEE80211_WNM_BSS_TM_REJECT_NO_SUITABLE")
require(node_c, "wnm_target != 1",
        "confirmed WNM target override for stale desired BSSID")
order(node_c, "preexisting scan cannot satisfy BTM",
      "ieee80211_wnm_bss_transition_fresh_scan_pending(ic)",
      "timeout_add_msec(&ic->ic_wnm_bgscan_retry_timeout, 1);",
      "ni = RB_MIN(ieee80211_tree, &ic->ic_tree);")
order(node_c, "accept before source leave",
      "ieee80211_wnm_bss_transition_confirm_candidate(",
      "wnm_source = ic->ic_bss;",
      "ieee80211_wnm_bss_transition_tx_fence_arm(ic,",
      "IEEE80211_F_TX_MGMT_ONLY",
      "IEEE80211_WNM_BSS_TM_ACCEPT, wnm_target_bssid",
      "IEEE80211_FC0_SUBTYPE_DEAUTH")
order(proto_c, "exact management TX completion fence",
      "ieee80211_wnm_bss_transition_tx_fence_arm(",
      "transition->tx_fence_generation = next;",
      "ieee80211_wnm_bss_transition_tx_fence_classify(",
      "IEEE80211_ACTION_WNM_BSS_TRANS_RESP",
      "IEEE80211_REASON_BSS_TRANSITION_DISASSOC",
      "ieee80211_wnm_bss_transition_tx_fence_submit(",
      "ieee80211_wnm_bss_transition_tx_fence_submit_failed(",
      "ieee80211_wnm_bss_transition_tx_fence_complete(",
      "transition->tx_fence_completed |= kind;",
      "ieee80211_node_wnm_reconnect(ic, ni);")
for token in (
        "ieee80211_wnm_bss_transition_tx_fence_classify(",
        "ieee80211_wnm_bss_transition_tx_fence_submit(ic,",
        "ieee80211_wnm_bss_transition_tx_fence_submit_failed(",
        "data->wnm_tx_fence_generation = wnm_tx_fence_generation;",
        "ieee80211_wnm_bss_transition_tx_fence_complete(ic,"):
    require(iwn, token, "IWN descriptor carries exact BTM fence")
order(node_c, "TX completion before reconnect scan",
      "ieee80211_node_wnm_reconnect(struct ieee80211com *ic,",
      "IEEE80211_F_TX_MGMT_ONLY",
      "ic->ic_sae_wnm_roam_start != NULL",
      "(*ic->ic_sae_wnm_roam_start)(ic, ni) != 0",
      "ieee80211_new_state(ic, IEEE80211_S_SCAN,",
      "IEEE80211_NEWSTATE_ARG_WNM_RECONNECT_HOLD")
require(proto_h, "IEEE80211_NEWSTATE_ARG_WNM_RECONNECT_HOLD (-4)",
        "private BTM reconnect hold marker")
order(iwn, "confirmed BTM reconnect does not launch a second scan",
      "const bool wnm_reconnect_hold =",
      "arg == IEEE80211_NEWSTATE_ARG_WNM_RECONNECT_HOLD",
      "if (wnm_reconnect_hold) {",
      "return 0;",
      "that->iwn_scan(sc, IEEE80211_CHAN_2GHZ")
order(iwn, "BTM exact-channel scan plan",
      "wnm_exact_channel =",
      "iwn_scan_lease_wnm_target_channel(sc, lease_serial,",
      "if (wnm_exact_channel &&",
      "ieee80211_chan2ieee(ic, c) != wnm_target_channel",
      "if (hdr->nchan == 0)")
order(iwn, "BTM channel is fenced to the admitted scan lease",
      "(void)ieee80211_wnm_bss_transition_target_channel(ic,",
      "iwn_scan_lease_reserve(sc, owner, upper_generation,",
      "direct_sae_scan_generation,",
      "wnm_target_channel)")
require(iwn, "sc->sc_scan_lease.wnm_target_channel = wnm_target_channel;",
        "per-lease BTM channel snapshot")
order(iwn, "BTM exact-channel terminal",
      "const bool wnm_exact_channel =",
      "iwn_scan_lease_wnm_target_channel(sc, 0,",
      "(sc->sc_flags & IWN_FLAG_HAS_5GHZ) &&",
      "!wnm_exact_channel")
order(iwn, "IWN BTM aborts only a live background lease",
      "iwn_wnm_bgscan_abort(struct ieee80211com *ic)",
      "(sc->sc_flags & IWN_FLAG_BGSCAN) == 0",
      "iwn_scan_lease_mark_abort(sc, IWN_SCAN_LEASE_NONE, 0,",
      "IWN_CMD_SCAN_ABORT")
order(iwn, "BTM retry wins the abort-terminal race",
      "ieee80211_wnm_bss_transition_scan_owns_admission(ic)",
      "if (bgscan != 0 && !wnm_scan &&",
      "ieee80211_wnm_bss_transition_fresh_scan_pending(ic)")
order(skywalk, "fresh WCL credential retarget",
      "ieee80211_wnm_bss_transition_copy_retarget(",
      "const struct ether_addr *bssid = wnm_retarget",
      "directRequest.confirmedWnmCandidate = wnm_retarget;",
      "startIwnDirectSaeCredential(&directRequest",
      "ieee80211_wnm_bss_transition_consume(")
order(proto_c, "confirmed BTM target promotes one direct-SAE selection",
      "ieee80211_sae_wcl_request_admit_confirmed_wnm_candidate(",
      "request->phase == IEEE80211_SAE_WCL_REQUEST_PENDING",
      "transition->candidate_confirmed != 0",
      "IEEE80211_ADDR_EQ(request->bssid, transition->target_bssid)",
      "request->phase = IEEE80211_SAE_WCL_REQUEST_SCAN_ISSUED;")
order(skywalk, "confirmed BTM SAE uses the retained candidate directly",
      "if (request->confirmedWnmCandidate &&",
      "!lowerAdmissionRequiresFreshScan) {",
      "ieee80211_sae_wcl_request_admit_confirmed_wnm_candidate(",
      "tahoeJoinCachedWclCandidate(",
      "ieee80211_sae_wcl_request_bound_current(ic, ic->ic_bss)",
      "} else {",
      "ieee80211_sae_wcl_request_resume_scan(ic, generation)")
for token in ("ic_sae_roam_port_valid", "ic_sae_wnm_roam_start"):
    require(var_h, token, "driver-resident SAE roam hook")
order(iwn, "IWN publishes and withdraws private SAE roam hooks",
      "ic->ic_sae_roam_port_valid = ItlIwn::iwn_sae_roam_port_valid;",
      "ic->ic_sae_wnm_roam_start = ItlIwn::iwn_sae_wnm_roam_start;",
      "ic->ic_sae_roam_port_valid = NULL;",
      "ic->ic_sae_wnm_roam_start = NULL;")
order(iwn, "driver-resident SAE targeted owner",
      "iwn_sae_targeted_roam_start(struct ieee80211com *ic,",
      "iwn_sae_engine_callback_enter(sc)",
      "sc->sc_sae_wcl_credential_active",
      "credential = sc->sc_sae_wcl_credential;",
      "source_generation = credential.request_generation",
      "ieee80211_sae_wcl_request_retarget_run(ic, source,",
      "ieee80211_match_bss(ic, candidate, 0)",
      "that->stageSaeWclCredential(&credential)",
      "LOWER_RETARGET_ACCEPTED",
      "ieee80211_node_join_bss(ic, candidate)",
      "ieee80211_sae_wcl_request_bound_current(ic, ic->ic_bss)",
      "ieee80211_wnm_bss_transition_consume(ic, source_ssid,")
if "IEEE80211_NEWSTATE_ARG_WNM_RECONNECT_HOLD" in iwn[iwn.find(
        "iwn_sae_targeted_roam_start(struct ieee80211com *ic,"):
        iwn.find("iwn_sae_wnm_roam_start(struct ieee80211com *ic,")]:
    fail("targeted SAE owner must preserve RUN until lower retarget acceptance")
order(iwn, "driver-resident SAE BTM target wrapper",
      "iwn_sae_wnm_roam_start(struct ieee80211com *ic,",
      "ieee80211_wnm_bss_transition_copy_retarget(ic, source_ssid,",
      "iwn_sae_targeted_roam_start(ic, source, target_bssid, true)")
order(pae_input_c, "normal RSN port validates retained SAE credential",
      "ni->ni_port_valid = 1;",
      "ic->ic_sae_roam_port_valid != NULL",
      "(*ic->ic_sae_roam_port_valid)(ic, ni)",
      "IEEE80211_EVT_STA_RSN_HANDSHAKE_DONE")
order(proto_c, "software-PMF port validates retained SAE credential",
      "port_became_valid",
      "ic->ic_sae_roam_port_valid != NULL",
      "(*ic->ic_sae_roam_port_valid)(ic, snapshot.ni)",
      "IEEE80211_EVT_STA_RSN_HANDSHAKE_DONE")

print("PASS: IWN BTM scans an exact target and reuses only its validated active-ESS SAE credential for immediate driver-resident roam")
PY
