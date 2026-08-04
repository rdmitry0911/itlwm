#!/usr/bin/env bash
# Regression gate for the IWN hard-AP-outage reconnect path.
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

python3 - "$root" <<'PY'
from pathlib import Path
import sys


root = Path(sys.argv[1])
var = (root / "itl80211/openbsd/net80211/ieee80211_var.h").read_text()
iwnvar = (root / "itlwm/hal_iwn/if_iwnvar.h").read_text()
iwn = (root / "itlwm/hal_iwn/ItlIwn.cpp").read_text()
controller = (root / "AirportItlwm/AirportItlwmV2.cpp").read_text()
node = (root / "itl80211/openbsd/net80211/ieee80211_node.c").read_text()
proto = (root / "itl80211/openbsd/net80211/ieee80211_proto.c").read_text()


def fail(message: str) -> None:
    raise SystemExit(f"Tahoe IWN beacon-loss reconnect: {message}")


def body(text: str, marker: str, label: str) -> str:
    start = text.find(marker)
    if start < 0:
        fail(f"missing {label}")
    opening = text.find("{", start)
    if opening < 0:
        fail(f"missing body for {label}")
    depth = 0
    for index in range(opening, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[opening + 1:index]
    fail(f"unterminated {label}")


def ordered(text: str, label: str, *tokens: str) -> None:
    cursor = 0
    for token in tokens:
        position = text.find(token, cursor)
        if position < 0:
            fail(f"{label} missing ordered token: {token}")
        cursor = position + len(token)


if "#define IEEE80211_EVT_STA_BEACON_LOSS              21" not in var:
    fail("missing distinct beacon-loss event")
if "ic_sae_bss_loss_recover" not in var:
    fail("missing private scan-terminal recovery hook")
if "sc_sae_bss_loss_join_handoff_generation" not in iwnvar:
    fail("missing generation-fenced foreground terminal handoff")

notif_start = iwn.find("case IWN_BEACON_MISSED:")
notif_end = iwn.find("case IWN_UC_READY:", notif_start)
if notif_start < 0 or notif_end < 0:
    fail("missing bounded IWN_BEACON_MISSED notification case")
notif = iwn[notif_start:notif_end]
ordered(
    notif,
    "firmware threshold to reconnect transition",
    "missed = letoh32(miss->consecutive)",
    "missed > ic->ic_bmissthres && !ic->ic_mgt_timer",
    "IEEE80211_EVT_STA_BEACON_LOSS",
    "ieee80211_new_state(ic, IEEE80211_S_SCAN, -1)",
)
if "IEEE80211_FC0_SUBTYPE_PROBE_REQ" in notif:
    fail("beacon-loss path still arms the minute-long directed-probe timeout")
if "IEEE80211_EVT_STA_DEAUTH" in notif:
    fail("beacon loss must not impersonate a received deauthentication")

scan_terminal = body(
    node,
    "ieee80211_end_scan_controlled(",
    "generic scan terminal",
)
ordered(
    scan_terminal,
    "fresh census to private recovery owner",
    "ic->ic_des_esslen == 0",
    "ic->ic_sae_bss_loss_recover != NULL",
    "(*ic->ic_sae_bss_loss_recover)(ic) != 0",
)

admit = body(
    proto,
    "ieee80211_sae_wcl_request_admit_bss_loss_candidate(",
    "hard-loss SAE request admission",
)
for token in (
    "request->generation == generation",
    "request->phase == IEEE80211_SAE_WCL_REQUEST_PENDING",
    "request->association_epoch == 0",
    "ieee80211_sae_wcl_request_scan_policy_matches_locked(ic, request)",
    "request->phase = IEEE80211_SAE_WCL_REQUEST_SCAN_ISSUED",
):
    if token not in admit:
        fail(f"hard-loss admission missing exact policy fence: {token}")

arm = body(
    iwn,
    "static bool\niwn_sae_bss_loss_join_handoff_arm(",
    "foreground terminal handoff arm",
)
for token in (
    "sc->sc_scan_lease.owner == IWN_SCAN_LEASE_GENERIC_FOREGROUND",
    "sc->sc_scan_lease.phase == IWN_SCAN_LEASE_DRAINING",
    "sc->sc_scan_lease.command_submitted",
    "sc->sc_scan_lease.terminal_claimed",
    "!sc->sc_scan_lease.abort_requested",
    "!sc->sc_scan_lease.hardware_invalidated",
    "!sc->sc_scan_lease.publication_invalidated",
    "(sc->sc_flags & IWN_FLAG_SCANNING) == 0",
):
    if token not in arm:
        fail(f"foreground handoff admits an unproven scan: {token}")

promote = body(
    iwn,
    "static bool\niwn_sae_join_scan_block_promote(",
    "SAE join scan-block promotion",
)
ordered(
    promote,
    "hard-loss handoff transfer",
    "completing_bss_loss =",
    "sc->sc_sae_bss_loss_join_handoff_generation ==",
    "request_generation",
    "completing_bss_loss)",
    "sc->sc_sae_bss_loss_join_handoff_generation = 0",
    "sc->sc_sae_join_scan_block_generation = request_generation",
)

recover = body(
    iwn,
    "int ItlIwn::\niwn_sae_bss_loss_recover(",
    "driver-resident hard-loss recovery",
)
ordered(
    recover,
    "selected candidate to synchronous SAE AUTH",
    "iwn_sae_bss_loss_candidate_eligible(candidate, &credential)",
    "ieee80211_sae_wcl_request_begin",
    "ieee80211_sae_wcl_request_admit_bss_loss_candidate",
    "iwn_sae_bss_loss_join_handoff_arm(sc, generation)",
    "ieee80211_node_join_bss(ic, candidate)",
    "iwn_sae_bss_loss_join_handoff_completed(sc, generation)",
    "DRIVER_RESIDENT_BSS_LOSS_STARTED",
)

handler = body(
    controller,
    "void AirportItlwm::\neventHandler(",
    "controller event handler",
)
loss_start = handler.find("case IEEE80211_EVT_STA_BEACON_LOSS:")
scan_start = handler.find("case IEEE80211_EVT_SCAN_DONE:", loss_start)
if loss_start < 0 or scan_start < 0:
    fail("missing bounded controller beacon-loss case")
loss = handler[loss_start:scan_start]
ordered(
    loss,
    "WCL beacon-loss publication",
    "clearTahoeWclAuthAssocCompletionLeaseGated",
    "postTahoeWclLinkDownIndGated",
    "(void *)(uintptr_t)1U",
    "return;",
)
if "APPLE80211_M_DEAUTH_RECEIVED" in loss:
    fail("beacon-loss controller case publishes DEAUTH_RECEIVED")

print("Tahoe IWN beacon-loss reconnect contract: PASS")
PY
