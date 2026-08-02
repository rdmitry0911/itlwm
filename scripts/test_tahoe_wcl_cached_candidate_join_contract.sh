#!/usr/bin/env bash
# Contract for the Tahoe JoinAdapter-equivalent cached-candidate path.
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

python3 - "$root" <<'PY'
from pathlib import Path
import sys

root = Path(sys.argv[1])
sky = (root / "AirportItlwm/AirportItlwmSkywalkInterface.cpp").read_text()
v2 = (root / "AirportItlwm/AirportItlwmV2.cpp").read_text()
v2h = (root / "AirportItlwm/AirportItlwmV2.hpp").read_text()

def fail(message):
    raise SystemExit(f"Tahoe WCL cached-candidate join contract: {message}")

def require(text, token, label):
    if token not in text:
        fail(f"missing {label}: {token}")

def ordered(text, label, *tokens):
    cursor = 0
    for token in tokens:
        found = text.find(token, cursor)
        if found < 0:
            fail(f"{label} missing ordered token: {token}")
        cursor = found + len(token)

def body(text, marker, label):
    start = text.find(marker)
    if start < 0:
        fail(f"missing {label}")
    opening = text.find("{", start)
    if opening < 0:
        fail(f"missing body for {label}")
    depth = 0
    for position in range(opening, len(text)):
        if text[position] == "{":
            depth += 1
        elif text[position] == "}":
            depth -= 1
            if depth == 0:
                return text[opening + 1:position]
    fail(f"unterminated {label}")

idle = body(v2, "bool AirportItlwm::associationScanOwnersIdle() const",
            "combined physical scan-owner idle fence")
ordered(idle, "single-lock idle census",
        "IOSimpleLockLockDisableInterrupt(admissionLock)",
        "!standard.cachedTerminalPending",
        "!standard.cachedTerminalPublishing",
        "TahoeStandardScanContracts::idle(&standard.physicalState)",
        "!wcl.settingUp", "!wcl.stopping", "!wcl.tearingDown",
        "TahoeWclPhysicalScanContracts::Phase::Idle",
        "IOSimpleLockUnlockEnableInterrupt(admissionLock, irq)")
require(v2h, "bool associationScanOwnersIdle() const;",
        "controller idle-fence declaration")

join = body(sky,
            "static bool\ntahoeJoinCachedWclCandidate(struct ieee80211com *ic,",
            "cached-candidate join helper")
ordered(join, "strict cached-candidate admission",
        "!scanOwnersIdle",
        "ic->ic_opmode != IEEE80211_M_STA",
        "ic->ic_state != IEEE80211_S_SCAN",
        "ic->ic_wcl_scan_active",
        "ieee80211_find_node(ic, bssid)",
        "candidate->ni_fails != 0",
        "candidate->ni_chan == IEEE80211_CHAN_ANYC",
        "candidate->ni_esslen != ic->ic_des_esslen",
        "memcmp(candidate->ni_essid, ic->ic_des_essid",
        "ieee80211_match_bss(ic, candidate, 0) != 0",
        "CACHED_CANDIDATE_DIRECT_JOIN",
        "ieee80211_node_join_bss(ic, candidate)")

wcl = body(sky,
           "IOReturn AirportItlwmSkywalkInterface::\nsetWCL_ASSOCIATEImpl",
           "WCL association")
ordered(wcl, "owner-before-direct-join-before-fallback",
        "associationOwner.authAssocCompletionArmed = true",
        "getTahoeOwnerRegistry().association =",
        "AirportItlwmPostPltiTraceBeginEpisode(ic);",
        "associationOwner.selectedFromCandidate",
        "tahoeJoinCachedWclCandidate(",
        "associationScanOwnersIdle()",
        "if (!joinedCachedCandidate)",
        "ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);")

print("PASS: Tahoe WCL joins an exact stable cached candidate directly and retains a directed-scan fallback")
PY
