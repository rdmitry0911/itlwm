#!/bin/sh
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

python3 - "$repo_root" <<'PY'
from pathlib import Path
import sys

root = Path(sys.argv[1])
runtime = (root / "include/HAL/ItlApFirmwareRuntime.hpp").read_text()
framing = (root / "include/HAL/ItlApOpenRuntime.hpp").read_text()
donor = (root / "itl80211/openbsd/net80211/ieee80211_input.c").read_text()
iwm = (root / "itlwm/hal_iwm/mac80211.cpp").read_text()
iwx = (root / "itlwm/hal_iwx/ItlIwx.cpp").read_text()


def require(source: str, needle: str, label: str) -> None:
    if needle not in source:
        raise SystemExit(f"FAIL: missing {label}: {needle}")


def body(source: str, signature: str) -> str:
    start = source.find(signature)
    if start < 0:
        raise SystemExit(f"FAIL: missing function: {signature}")
    opening = source.find("{", start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening:index + 1]
    raise SystemExit(f"FAIL: unterminated function: {signature}")


donor_assoc = body(donor, "ieee80211_recv_assoc_req(struct ieee80211com *ic,")
for needle, label in (
    ("(reassoc ? 10 : 4)", "donor fixed-body split"),
    ("IEEE80211_FC0_SUBTYPE_REASSOC_RESP", "donor reassoc response"),
):
    require(donor_assoc, needle, label)

require(runtime, "bool clientReassociationPending",
        "asynchronous reassociation identity")
parse = body(framing, "itl_ap_open_parse_assoc(struct ItlApFirmwareRuntime *runtime,")
for needle, label in (
    ("IEEE80211_FC0_SUBTYPE_REASSOC_REQ", "reassociation request admission"),
    ("reassociation ? 10 : 4", "4/10-byte IE offset split"),
    ("result->associationIEOffset = headerLength + fixedLength",
     "deferred IE ownership offset"),
    ("result->reassociation = reassociation", "deferred response subtype"),
):
    require(parse, needle, label)

response = body(framing,
    "itl_ap_open_build_assoc_success(const struct ItlApFirmwareRuntime *runtime,")
require(response, "IEEE80211_FC0_SUBTYPE_REASSOC_RESP",
        "matching reassociation response")

for family, source, task_sig, rx_sig in (
    ("IWM", iwm, "iwm_ap_client_task(void *arg)",
     "iwm_ap_handle_rx(struct iwm_softc *sc,"),
    ("IWX", iwx, "iwx_ap_client_task(void *arg)",
     "iwx_ap_handle_rx(struct iwx_softc *sc,"),
):
    rx = body(source, rx_sig)
    require(rx, "result.associationIEOffset", f"{family} 4/10-byte IE copy")
    require(rx, "client->clientReassociationPending = result.reassociation",
            f"{family} deferred reassociation latch")
    task = body(source, task_sig)
    require(task, "client->clientReassociationPending",
            f"{family} deferred reassociation consume")
    require(task, "IEEE80211_APSTA_EVENT_REASSOC",
            f"{family} non-duplicating upper station event")
    require(task, "itl_ap_open_build_assoc_success",
            f"{family} matching deferred reply")

print("PASS: paired IWM/IWX AP reassociation/reconnect contract")
PY
