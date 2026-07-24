#!/usr/bin/env bash
# Static guard for the dormant generic bridge used by the future IWN-resident
# SAE engine.  It proves dispatch/fail-closed ordering only; it is not an
# on-air SAE or WPA3 association result.
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

python3 - "$root" <<'PY'
from pathlib import Path
import sys


root = Path(sys.argv[1])
var = (root / "itl80211/openbsd/net80211/ieee80211_var.h").read_text()
inp = (root / "itl80211/openbsd/net80211/ieee80211_input.c").read_text()
proto = (root / "itl80211/openbsd/net80211/ieee80211_proto.c").read_text()
core = (root / "itl80211/openbsd/net80211/ieee80211.c").read_text()


def fail(message: str) -> None:
    raise SystemExit(f"IWN SAE driver bridge contract: {message}")


def require(text: str, needle: str, label: str) -> int:
    position = text.find(needle)
    if position < 0:
        fail(f"missing {label}: {needle}")
    return position


def ordered(text: str, label: str, *needles: str) -> None:
    cursor = 0
    for needle in needles:
        position = text.find(needle, cursor)
        if position < 0:
            fail(f"{label} missing ordered token: {needle}")
        cursor = position + len(needle)


for token in (
        "ic_sae_auth_hold", "ic_sae_auth_owned",
        "ic_sae_engine_peer_event"):
    require(var, token, "dormant generic hook")

# S_AUTH must offer the private owner before any generic Open-System sender.
auth_case = require(proto, "case IEEE80211_S_AUTH:\n\t\t/*\n\t\t * A selected driver-owned SAE attempt",
                    "S_AUTH direct-owner state")
hold = proto.find("ic_sae_auth_hold", auth_case)
open_sender = proto.find("IEEE80211_FC0_SUBTYPE_AUTH, 1);", auth_case)
if hold < 0 or open_sender < 0 or hold > open_sender:
    fail("S_AUTH does not hold direct SAE before generic Open-System AUTH")
require(proto[auth_case:open_sender], "ic->ic_mgt_timer = IEEE80211_TRANS_WAIT;",
        "held AUTH watchdog")

# A direct peer handler gets the copied public frame first.  If it races out
# while the exact BSS remains owned, no controller/Agent fallback is allowed.
peer = require(inp, "ieee80211_recv_sae_peer_auth", "peer RX leaf")
peer_tail = inp[peer:]
ordered(peer_tail, "peer RX ownership order",
        "ic->ic_sae_engine_peer_event", "engine_result != 0",
        "ic->ic_sae_auth_owned(ic, ic->ic_bss)",
        "ic->ic_event_handler")

# Historic Open-System success must be rejected against the authoritative BSS,
# not an arbitrary RX node that may be stale.
open = require(inp, "/* only \"open\" auth mode is supported", "Open RX branch")
open_tail = inp[open:]
require(open_tail, "ic_sae_auth_owned(ic, ic->ic_bss)",
        "authoritative BSS Open fence")
if "ic_sae_auth_owned(ic, ni)" in open_tail[:2500]:
    fail("Open-System fence uses the untrusted RX node")

# The watchdog snapshots ownership before it invalidates the association
# epoch and therefore never re-enters the historical AUTH retry for SAE.
watchdog = require(core, "ieee80211_watchdog", "watchdog")
watchdog_tail = core[watchdog:]
ordered(watchdog_tail, "watchdog ownership fence",
        "sae_timeout_owned = ic->ic_sae_auth_owned(ic, ic->ic_bss);",
        "ieee80211_pae_assoc_epoch_begin(ic);",
        "!sae_timeout_owned")

print("PASS: generic IWN SAE bridge holds S_AUTH and rejects controller/Open-System fallback")
PY
