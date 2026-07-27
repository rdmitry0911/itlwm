#!/usr/bin/env bash
# Ensure a successful Tahoe public ASSOCIATE is a complete join intent.
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

python3 - "$root" <<'PY'
from pathlib import Path
import sys


root = Path(sys.argv[1])
sky = (root / "AirportItlwm/AirportItlwmSkywalkInterface.cpp").read_text()


def fail(message: str) -> None:
    raise SystemExit(f"public associate scan-resume contract: {message}")


def body(text: str, marker: str) -> str:
    start = text.find(marker)
    if start < 0:
        fail("missing public setASSOCIATE")
    opening = text.find("{", start)
    depth = 0
    for position in range(opening, len(text)):
        if text[position] == "{":
            depth += 1
        elif text[position] == "}":
            depth -= 1
            if depth == 0:
                return text[opening + 1:position]
    fail("unterminated public setASSOCIATE")


public = body(sky, "setASSOCIATE(struct apple80211_assoc_data *ad)")
tokens = (
    "assocResult = associateSSID(",
    "if (assocResult == kIOReturnSuccess &&",
    "TahoeAssociationAuthContracts::mayUseLocalPskPmk(",
    "ieee80211_public_initial_bssid_pin_arm(",
    "if (assocResult == kIOReturnSuccess) {",
    "ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);",
)
cursor = 0
for token in tokens:
    found = public.find(token, cursor)
    if found < 0:
        fail(f"missing ordered successful-resume token: {token}")
    cursor = found + len(token)

success = body(public, "if (assocResult == kIOReturnSuccess) {")
if success.count("ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);") != 1:
    fail("successful association does not own one exact scan resume")
if "AirportItlwmPostPltiTraceBeginEpisode" in public:
    fail("public association was mislabeled as a WCL PMK-ready episode")

print("Tahoe public associate scan-resume contract: PASS")
PY
