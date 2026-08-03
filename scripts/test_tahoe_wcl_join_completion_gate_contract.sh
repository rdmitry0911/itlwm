#!/usr/bin/env bash
# Static regression gate for PostOffice admission of the RSN/WCL completion
# carriers. Tahoe sendMail requires the controller work loop to be inGate().
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

python3 - "$root" <<'PY'
from pathlib import Path
import sys


source = (Path(sys.argv[1]) / "AirportItlwm/AirportItlwmV2.cpp").read_text()


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
    "AirportItlwm::postRsnHandshakeDoneGated(",
    "postTahoeWclLinkUpInd(controller, rawReason)",
    "postTahoeWclConnectCompleteEvent(controller)",
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
rsn_case_end = source.find("case IEEE80211_EVT_STA_DEAUTH:", rsn_case_start)
if rsn_case_start < 0 or rsn_case_end < 0:
    fail("missing RSN_HANDSHAKE_DONE case")
rsn_case = source[rsn_case_start:rsn_case_end]
ordered(
    rsn_case,
    "gate->runAction(postTahoeWclJoinCompletionGated",
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

print("Tahoe WCL join completion gate contract: PASS")
PY
