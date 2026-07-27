#!/usr/bin/env bash
# Ensure a successful Tahoe public ASSOCIATE is a complete join intent.
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

python3 - "$root" <<'PY'
from pathlib import Path
import sys


root = Path(sys.argv[1])
sky = (root / "AirportItlwm/AirportItlwmSkywalkInterface.cpp").read_text()
proto_h = (
    root / "itl80211/openbsd/net80211/ieee80211_proto.h"
).read_text()
proto = (
    root / "itl80211/openbsd/net80211/ieee80211_proto.c"
).read_text()
iwn = (root / "itlwm/hal_iwn/ItlIwn.cpp").read_text()


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
    "ieee80211_new_state(\n"
    "                ic, IEEE80211_S_SCAN,\n"
    "                IEEE80211_NEWSTATE_ARG_PUBLIC_ASSOCIATE);",
)
cursor = 0
for token in tokens:
    found = public.find(token, cursor)
    if found < 0:
        fail(f"missing ordered successful-resume token: {token}")
    cursor = found + len(token)

success = body(public, "if (assocResult == kIOReturnSuccess) {")
if success.count("IEEE80211_NEWSTATE_ARG_PUBLIC_ASSOCIATE") != 1:
    fail("successful association does not own one exact fresh-scan resume")
if "AirportItlwmPostPltiTraceBeginEpisode" in public:
    fail("public association was mislabeled as a WCL PMK-ready episode")

required_header = (
    "#define IEEE80211_NEWSTATE_ARG_PUBLIC_ASSOCIATE (-3)",
    "(_arg) == IEEE80211_NEWSTATE_ARG_PUBLIC_ASSOCIATE",
)
for token in required_header:
    if token not in proto_h:
        fail(f"missing private restart marker contract: {token}")

epoch = body(proto, "ieee80211_pae_assoc_epoch_note_newstate(")
if "arg == IEEE80211_NEWSTATE_ARG_PUBLIC_ASSOCIATE" not in epoch:
    fail("public restart does not preserve the newly armed BSSID provenance")
if "ieee80211_pae_assoc_epoch_begin_internal(ic, 1);" not in epoch:
    fail("public restart does not use the narrow preservation owner")

preflight = body(iwn, "iwn_newstate_preflight(")
ordered_preflight = (
    "arg == IEEE80211_NEWSTATE_ARG_PUBLIC_ASSOCIATE",
    "ic->ic_state == IEEE80211_S_SCAN &&\n"
    "        !public_associate_restart",
    "iwn_scan_lease_defer_scan(sc, nstate, arg, &serial, &submit_abort)",
    "IWN_CMD_SCAN_ABORT",
)
cursor = 0
for token in ordered_preflight:
    found = preflight.find(token, cursor)
    if found < 0:
        fail(f"missing ordered IWN abort/replay token: {token}")
    cursor = found + len(token)
if "sc->sc_scan_lease_replay_pending = true;" not in iwn:
    fail("IWN lease does not retain the fresh public scan for terminal replay")

print("Tahoe public associate scan-resume contract: PASS")
PY
