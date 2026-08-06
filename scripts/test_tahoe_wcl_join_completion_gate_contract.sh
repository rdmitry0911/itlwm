#!/usr/bin/env bash
# Static regression gate for PostOffice admission of the RSN/WCL completion
# carriers. Tahoe sendMail requires the controller work loop to be inGate().
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

python3 - "$root" <<'PY'
from pathlib import Path
import sys


root = Path(sys.argv[1])
source = (root / "AirportItlwm/AirportItlwmV2.cpp").read_text()
net_proto = (
    root / "itl80211/openbsd/net80211/ieee80211_proto.c"
).read_text()
net_var = (
    root / "itl80211/openbsd/net80211/ieee80211_var.h"
).read_text()


def fail(message):
    raise SystemExit(f"Tahoe WCL join completion gate: {message}")


def body(marker):
    start = source.find(marker)
    if start < 0:
        fail(f"missing marker: {marker}")
    opening = source.find("{", start)
    if opening < 0:
        fail(f"missing body: {marker}")
    depth = 0
    for position in range(opening, len(source)):
        if source[position] == "{":
            depth += 1
        elif source[position] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1:position]
    fail(f"unterminated body: {marker}")


def ordered(text, *needles):
    cursor = 0
    for needle in needles:
        position = text.find(needle, cursor)
        if position < 0:
            fail(f"missing ordered token: {needle}")
        cursor = position + len(needle)


action = body("static IOReturn postTahoeWclJoinCompletionGated(")
ordered(
    action,
    "runContinuation",
    "AirportItlwm::postRsnHandshakeDoneGated(",
    "postTahoeWclProtectedRunCompletionGated(controller, rawReason)",
)
for token in (
        "IO80211PostOffice::sendMail",
        "inGate()",
        "do not call setLinkState here",
):
    if token not in action:
        fail(f"missing producer-context invariant: {token}")
if "setLinkState(" in action:
    fail("producer action must not force parent link state")

protected = body("static IOReturn postTahoeWclProtectedRunCompletionGated(")
ordered(
    protected,
    "ic->ic_state != IEEE80211_S_RUN",
    "ieee80211_pae_selected_bss_copyout_current",
    "tahoeWclProtectedJoinCompletionMatchesOwner",
    "owner->connectCompletionPublished = true",
    "owner->joinTerminalObserved = true",
    "postTahoeWclLinkUpInd(controller, rawReason)",
    "postTahoeWclConnectCompleteEvent(controller)",
)
if "setLinkState(" in protected:
    fail("protected RUN terminal must not force parent link state")

protected_match = body(
    "static bool tahoeWclProtectedJoinCompletionMatchesOwner(")
for token in (
    "owner.rsnIeLength != 0",
    "bss->ni_rsnakms == IEEE80211_AKM_SAE",
    "TahoeAssociationAuthContracts::mayUseDirectSaeWclCredential",
):
    if token not in protected_match:
        fail(f"missing request-RSN/current-BSS security fence: {token}")

parent_action = body("IOReturn AirportItlwm::\nsetLinkStateGated(")
for token in (
        "AppleBCMWLANNetAdapter::handleLink is the",
        "sole 0xd8 producer",
        "WCLNetManager::updateLinkState(true, false, true",
        "owns only the inherited IO80211 link-state publication",
):
    if token not in parent_action:
        fail(f"missing reference-backed single-owner invariant: {token}")
if "postTahoeWclLinkUpInd(" in parent_action:
    fail("parent link-state action must not duplicate the WCL 0xd8 producer")

rsn_case_start = source.find("case IEEE80211_EVT_STA_RSN_HANDSHAKE_DONE:")
rsn_case_end = source.find("case IEEE80211_EVT_STA_RSN_RUN_DONE:", rsn_case_start)
if rsn_case_start < 0 or rsn_case_end < 0:
    fail("missing RSN_HANDSHAKE_DONE case")
rsn_case = source[rsn_case_start:rsn_case_end]
ordered(
    rsn_case,
    "gate->runAction(postTahoeWclJoinCompletionGated",
    "(void *)(uintptr_t)false",
    "return;",
)
for forbidden in (
        "postTahoeWclLinkUpInd(that",
        "postTahoeWclConnectCompleteEvent(that",
        "deprecatedOpenGate",
        "deprecatedCloseGate",
):
    if forbidden in rsn_case:
        fail(f"ungated/manual gate completion path present: {forbidden}")

run_case_start = source.find("case IEEE80211_EVT_STA_RSN_RUN_DONE:")
run_case_end = source.find("case IEEE80211_EVT_STA_DEAUTH:", run_case_start)
if run_case_start < 0 or run_case_end < 0:
    fail("missing RSN_RUN_DONE case")
run_case = source[run_case_start:run_case_end]
ordered(
    run_case,
    "gate->runAction(postTahoeWclJoinCompletionGated",
    "(void *)(uintptr_t)true",
    "return;",
)
for forbidden in (
        "postRsnHandshakeDoneGated",
        "APPLE80211_M_RSN_HANDSHAKE_DONE",
        "handleKeyDone",
):
    if forbidden in run_case:
        fail(f"RUN continuation repeats key terminal: {forbidden}")

if "IEEE80211_EVT_STA_RSN_RUN_DONE              22" not in net_var:
    fail("missing protected RUN event identity")
ordered(
    net_proto,
    "case IEEE80211_S_RUN:",
    "ni->ni_port_valid",
    "IEEE80211_EVT_STA_RSN_RUN_DONE",
)

print("Tahoe WCL join completion gate contract: PASS")
PY
