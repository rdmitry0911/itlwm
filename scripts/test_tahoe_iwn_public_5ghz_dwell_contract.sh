#!/usr/bin/env bash
# Guard the bounded passive dwell used by a foreground public association.
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

python3 - "$root" <<'PY'
from pathlib import Path
import sys


root = Path(sys.argv[1])
iwn = (root / "itlwm/hal_iwn/ItlIwn.cpp").read_text()


def fail(message: str) -> None:
    raise SystemExit(f"IWN public 5 GHz dwell contract: {message}")


def body(text: str, marker: str) -> str:
    start = text.find(marker)
    if start < 0:
        fail(f"missing {marker}")
    opening = text.find("{", start)
    depth = 0
    for position in range(opening, len(text)):
        if text[position] == "{":
            depth += 1
        elif text[position] == "}":
            depth -= 1
            if depth == 0:
                return text[opening + 1:position]
    fail(f"unterminated {marker}")


submit = body(iwn, "int ItlIwn::\niwn_scan_submit")
ordered = (
    "bool foreground_5ghz_directed_dwell = false;",
    "if (ic->ic_des_esslen != 0) {",
    "is_active = 1;",
    "foreground_5ghz_directed_dwell = bgscan == 0 && is_active != 0 &&\n"
    "        (flags & IEEE80211_CHAN_5GHZ) != 0;",
    "if (foreground_5ghz_directed_dwell &&\n"
    "            (c->ic_flags & IEEE80211_CHAN_PASSIVE) != 0 &&\n"
    "            (c->ic_flags & IEEE80211_CHAN_DFS) == 0)\n"
    "            dwell_passive = MAX(dwell_passive, 130);",
)
cursor = 0
for token in ordered:
    found = submit.find(token, cursor)
    if found < 0:
        fail(f"missing ordered bounded-dwell token: {token}")
    cursor = found + len(token)

assignment_start = submit.find(
    "foreground_5ghz_directed_dwell = bgscan == 0")
assignment_end = submit.find(";", assignment_start)
if assignment_start < 0 or assignment_end < assignment_start:
    fail("missing directed foreground ownership expression")
assignment = submit[assignment_start:assignment_end]
if "wcl_scan" in assignment:
    fail("public association dwell was incorrectly restricted to WCL")

guard_start = submit.find("if (foreground_5ghz_directed_dwell &&")
guard_end = submit.find("dwell_passive = MAX(dwell_passive, 130);",
                        guard_start)
guard = submit[guard_start:guard_end]
for token in (
    "IEEE80211_CHAN_PASSIVE",
    "IEEE80211_CHAN_DFS",
):
    if token not in guard:
        fail(f"missing passive/DFS safety guard: {token}")
if "dwell_active" in guard:
    fail("public passive dwell guard changes active transmit dwell")
if "chan->flags &= ~htole32(IWN_CHAN_PASSIVE)" in submit:
    fail("public dwell path clears the firmware passive flag")

print("IWN public 5 GHz dwell contract: PASS")
PY
