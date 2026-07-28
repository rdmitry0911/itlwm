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
node_c = (root / "itl80211/openbsd/net80211/ieee80211_node.c").read_text()
output_c = (root / "itl80211/openbsd/net80211/ieee80211_output.c").read_text()
proto_c = (root / "itl80211/openbsd/net80211/ieee80211_proto.c").read_text()
skywalk = (root / "AirportItlwm/AirportItlwmSkywalkInterface.cpp").read_text()
iwn = (root / "itlwm/hal_iwn/ItlIwn.cpp").read_text()


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
      "IEEE80211_WNM_BSS_TM_ACCEPT, wnm_target_bssid",
      "IEEE80211_FC0_SUBTYPE_DEAUTH",
      "ic->ic_bss->ni_unref_cb = ieee80211_node_wnm_reconnect;")
order(node_c, "TX completion before reconnect scan",
      "ieee80211_node_wnm_reconnect(struct ieee80211com *ic,",
      "IEEE80211_F_TX_MGMT_ONLY",
      "ieee80211_new_state(ic, IEEE80211_S_SCAN,")
order(skywalk, "fresh WCL credential retarget",
      "ieee80211_wnm_bss_transition_copy_retarget(",
      "const struct ether_addr *bssid = wnm_retarget",
      "startIwnDirectSaeCredential(&directRequest",
      "ieee80211_wnm_bss_transition_consume(")

print("PASS: IWN BTM advertises support, scans an exact target, accepts before leave, and retargets fresh WCL SAE credentials")
PY
