#!/bin/sh
# Regression gate for keeping public MCS validity strict while allowing the
# reference-valid empty MCS carrier in the mandatory WCL current-BSS pipeline.
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
source="$root/AirportItlwm/AirportItlwmSkywalkInterface.cpp"

python3 - "$source" <<'PY'
from pathlib import Path
import sys


source = Path(sys.argv[1]).read_text(encoding="utf-8")


def fail(message):
    raise SystemExit(f"Tahoe WCL empty-MCS contract: {message}")


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


public_mcs = body(
    "getMCS_INDEX_SETImpl(struct apple80211_mcs_index_set_data *ad)",
)
wcl = body(
    "getWCL_EXTENDED_BSS_INFO(apple80211_extended_bss_info *data)",
)
link_update = body(
    "setWCL_LINK_STATE_UPDATE(apple80211_wcl_update_link_state *data)",
)

for token in (
    "ad->version = APPLE80211_VERSION;",
    "ad->mcs_set_map[i] = ic->ic_bss->ni_rxmcs[i];",
    "if (!hasAnyMcsBit)",
    "return kApple80211ErrNoCachedValue;",
):
    if token not in public_mcs:
        fail(f"public MCS validity fence changed: {token}")

ordered = (
    "memset(carrier, 0, sizeof(*carrier));",
    "ret = getMCS_INDEX_SETImpl(&carrier->mcs_set);",
    "ret != kApple80211ErrNoCachedValue",
    "return kIOReturnSuccess;",
)
cursor = 0
for token in ordered:
    position = wcl.find(token, cursor)
    if position < 0:
        fail(f"missing ordered WCL token: {token}")
    cursor = position + len(token)

for token in (
    "updateMCSSet accepts a zero entry count",
    "installs it in IO80211BssManager",
    "not a WCL transport",
):
    if token not in wcl:
        fail(f"missing reference invariant: {token}")

if "setLinkStateInternal(" in link_update:
    fail("experimental direct link-up bridge is still present")

print("Tahoe WCL empty-MCS contract: PASS")
PY
