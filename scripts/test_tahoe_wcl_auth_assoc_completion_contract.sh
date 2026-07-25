#!/usr/bin/env bash
# Static regression gate for Tahoe's two-stage WCL association boundary:
# generic Core status (0x4e/0x08), then a strictly candidate-owned
# JoinAdapter completion (0xd3/0x1c).
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

python3 - "$root" <<'PY'
from pathlib import Path
import sys

root = Path(sys.argv[1])
v2 = (root / "AirportItlwm/AirportItlwmV2.cpp").read_text()
sky = (root / "AirportItlwm/AirportItlwmSkywalkInterface.cpp").read_text()
owner = (root / "AirportItlwm/TahoeOwnerRegistry.hpp").read_text()
header = (root / "include/Airport/apple80211_var.h").read_text()
parity = (root / "AirportItlwm/TahoePayloadParity.hpp").read_text()
unit = (root / "tests/tahoe_payload_builders_test.cpp").read_text()


def fail(message):
    raise SystemExit(f"Tahoe WCL auth/assoc completion: {message}")


def require(text, needle, label):
    if needle not in text:
        fail(f"missing {label}: {needle}")


def forbid(text, needle, label):
    if needle in text:
        fail(f"unexpected {label}: {needle}")


def ordered(text, label, *needles):
    cursor = 0
    for needle in needles:
        pos = text.find(needle, cursor)
        if pos < 0:
            fail(f"{label} missing ordered token: {needle}")
        cursor = pos + len(needle)


def body(text, marker, label):
    start = text.find(marker)
    if start < 0:
        fail(f"missing {label}")
    opening = text.find("{", start)
    if opening < 0:
        fail(f"missing body for {label}")
    depth = 0
    for pos in range(opening, len(text)):
        if text[pos] == "{":
            depth += 1
        elif text[pos] == "}":
            depth -= 1
            if depth == 0:
                return text[opening + 1:pos]
    fail(f"unterminated {label}")


def between(text, begin, end, label):
    start = text.find(begin)
    if start < 0:
        fail(f"missing start for {label}: {begin}")
    finish = text.find(end, start)
    if finish < 0:
        fail(f"missing end for {label}: {end}")
    return text[start:finish]


# ABI: generic status and JoinManager completion are intentionally distinct.
for token in (
        "APPLE80211_WCL_ASSOC_STATUS_LEN 8",
        "struct apple80211_wcl_assoc_status_event",
        "APPLE80211_WCL_AUTH_ASSOC_COMPLETE_LEN 0x1c",
        "struct apple80211_wcl_auth_assoc_complete_event",
        "APPLE80211_M_WCL_AUTH_ASSOC_EVENT    78",
        "APPLE80211_M_WCL_AUTH_ASSOC_COMPLETE 211",
        "authAssocCompleteEventHandler accepts this exact 0x1c",
        "independent 0x4e association-status carrier above must be sent first",
):
    require(header, token, "two-carrier header ABI")

for token in (
        "secondary_state) == 0x02",
        "auth_seen) == 0x04",
        "bssid) == 0x05",
        "auth_status) == 0x0c",
        "assoc_reason) == 0x18",
):
    require(header, token, "0xd3 packed layout assertion")

for token in (
        '"wcl-assoc-status"',
        '"wcl-auth-assoc-complete"',
        "0x08",
        "0x1c",
        "postTahoeWclAuthAssocCompleteGated",
):
    require(parity, token, "payload parity inventory")

for token in (
        "WCL association-status uses AppleBCMWLAN handleAssocEvent selector 0x4e",
        "WCLJoinManager completion uses its distinct selector 0xd3",
        "WCL join auth/assoc completion is exactly 0x1c bytes",
        "successful join completion has the reference auth and assoc result tuple",
):
    require(unit, token, "standalone ABI regression")

# The generic status is published first for every successful association.  The
# gated completion attempt is deliberately best effort afterwards: a rejected
# 0xd3 must never retract or hide the already-issued 0x4e status bulletin.
assoc_case = between(v2,
                     "case IEEE80211_EVT_STA_ASSOC_DONE:",
                     "case IEEE80211_EVT_STA_RSN_HANDSHAKE_DONE:",
                     "STA_ASSOC_DONE case")
ordered(assoc_case, "0x4e before candidate completion",
        "buildTahoeWclAssocStatusPayload(0, 0, &assocStatus)",
        "APPLE80211_M_WCL_AUTH_ASSOC_EVENT",
        "captureTahoeWclAuthAssocCompletionRequest(ic, &request)",
        "gate->runAction(postTahoeWclAuthAssocCompleteGated",
        "return;")
forbid(assoc_case, "APPLE80211_M_WCL_AUTH_ASSOC_COMPLETE",
       "direct ungated 0xd3 selector in STA_ASSOC_DONE")

# Capture is restricted to the normal pre-RUN association state and carries a
# value snapshot tied to the exact selected-BSS epoch.
capture = body(v2, "static bool captureTahoeWclAuthAssocCompletionRequest(",
               "completion snapshot capture")
for token in (
        "ic->ic_state != IEEE80211_S_ASSOC",
        "ieee80211_pae_assoc_epoch_current(ic)",
        "ieee80211_pae_selected_bss_copyout_current",
        "IEEE80211_ADDR_EQ(request->selected.bssid, ic->ic_bss->ni_bssid)",
):
    require(capture, token, "S_ASSOC selected-BSS capture gate")

# The owner match prevents a generic/public association, alternate candidate,
# stale owner, or duplicate publication from entering the JoinManager path.
matches = body(v2, "static bool tahoeWclAuthAssocCompletionMatchesOwner(",
               "candidate owner matcher")
for token in (
        "owner.hasCarrier",
        "owner.selectedFromCandidate",
        "owner.authAssocCompletionArmed",
        "owner.authAssocCompletionPublished",
        "owner.candidateCount == 0",
        "selected.ssid_len != owner.ssidLength",
        "owner.selectedBssid",
        "owner.candidateBssid",
        "memcmp(selected.ssid, owner.ssid, selected.ssid_len)",
):
    require(matches, token, "candidate owner fence")

publisher = body(v2, "static IOReturn postTahoeWclAuthAssocCompleteGated(",
                 "gated 0xd3 publisher")
ordered(publisher, "gated completion validation before publication",
        "ic->ic_state != IEEE80211_S_ASSOC",
        "ieee80211_pae_assoc_epoch_current(ic) != request->associationEpoch",
        "ieee80211_pae_selected_bss_copyout_current",
        "ieee80211_pae_selected_bss_identity_matches",
        "tahoeWclAuthAssocCompletionMatchesOwner",
        "buildTahoeWclAuthAssocCompletePayload",
        "owner.authAssocCompletionPublished = true",
        "APPLE80211_M_WCL_AUTH_ASSOC_COMPLETE")
for token in (
        "sizeof(payload)",
        "if (result != kIOReturnSuccess)",
        "owner.authAssocCompletionPublished = false",
):
    require(publisher, token, "one-shot completion publication")

deauth_case = between(v2,
                      "case IEEE80211_EVT_STA_DEAUTH:",
                      "case IEEE80211_EVT_SCAN_DONE:",
                      "STA_DEAUTH case")
require(deauth_case, "clearTahoeWclAuthAssocCompletionLeaseGated",
        "deauthentication completion-lease clear")

clear = body(sky, "clearExternalPmkEligibilityLocked(const char *reason_tag)",
             "shared association lifecycle clear")
require(clear, "getTahoeOwnerRegistry().association =",
        "shared cancellation clears candidate owner")

public_assoc = body(sky, "setASSOCIATE(struct apple80211_assoc_data *ad)",
                    "public association setter")
require(public_assoc, "getTahoeOwnerRegistry().association =",
        "public association clears old WCL owner")

wcl_assoc = body(sky, "setWCL_ASSOCIATEImpl(apple80211AssocCandidates *candidates)",
                 "WCL association setter")
for token in (
        "A replacement WCL carrier starts a new candidate ledger",
        "associationOwner.authAssocCompletionArmed = true",
        "associationOwner.authAssocCompletionPublished = false",
):
    require(wcl_assoc, token, "WCL lease lifecycle")

reassoc = body(sky, "setWCL_REASSOC(apple80211_reassoc *data)",
               "WCL reassociation setter")
require(reassoc, "getTahoeOwnerRegistry().association =",
        "reassociation retires join-completion lease")

abort = body(sky, "setWCL_JOIN_ABORT(apple80211_wcl_abort_join *data)",
             "WCL join abort setter")
require(abort, 'clearExternalPmkEligibilityLocked("setWCL_JOIN_ABORT")',
        "join abort clears candidate owner")

for token in (
        "authAssocCompletionArmed",
        "authAssocCompletionPublished",
        "selectedBssid",
        "candidateBssid",
):
    require(owner, token, "association owner state")

print("Tahoe WCL auth/assoc completion contract: PASS")
PY
