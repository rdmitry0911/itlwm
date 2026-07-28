#!/usr/bin/env bash
# Semantic and source-order regression contract for bounded status-30 retry.
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
tmpdir="$(mktemp -d "${TMPDIR:-/tmp}/net80211-assoc-comeback.XXXXXX")"
cleanup() {
    rm -rf "$tmpdir"
}
trap cleanup EXIT

clang -std=c11 -Wall -Wextra -Werror \
    -I"$root/itl80211/openbsd/net80211" \
    "$root/tests/net80211_assoc_comeback_test.c" \
    -o "$tmpdir/net80211-assoc-comeback"
"$tmpdir/net80211-assoc-comeback"

python3 - "$root" <<'PY'
from pathlib import Path
import sys


root = Path(sys.argv[1])
input_c = (root / "itl80211/openbsd/net80211/ieee80211_input.c").read_text()
ieee_c = (root / "itl80211/openbsd/net80211/ieee80211.c").read_text()
proto_c = (root / "itl80211/openbsd/net80211/ieee80211_proto.c").read_text()


def fail(message):
    raise SystemExit(f"net80211 association-comeback contract: {message}")


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


def ordered(text, label, *tokens):
    cursor = 0
    for token in tokens:
        position = text.find(token, cursor)
        if position < 0:
            fail(f"{label} missing ordered token: {token}")
        cursor = position + len(token)


assoc = body(input_c, "void\nieee80211_recv_assoc_resp(",
             "association response")
ordered(
    assoc,
    "status-30 preservation precedes the terminal association fence",
    "status == IEEE80211_STATUS_TRY_AGAIN_LATER",
    "ieee80211_assoc_comeback_parse",
    "ic->ic_assoc_comeback_pending = 1",
    "ic->ic_mgt_timer = (int)comeback.timeout_seconds",
    "return;",
    "if (status != IEEE80211_STATUS_SUCCESS)",
    "ieee80211_pae_assoc_epoch_begin(ic)",
)

watchdog = body(ieee_c, "void\nieee80211_watchdog(",
                "management watchdog")
ordered(
    watchdog,
    "comeback resend precedes timeout cancellation",
    "ic->ic_assoc_comeback_pending",
    "IEEE80211_FC0_SUBTYPE_REASSOC_REQ",
    "IEEE80211_FC0_SUBTYPE_ASSOC_REQ",
    "IEEE80211_SEND_MGMT(ic, ic->ic_bss, subtype, 0)",
    "goto done;",
    "ieee80211_pae_assoc_epoch_begin(ic)",
    "ieee80211_wcl_reassoc_post_failure",
)

newstate_start = proto_c.find("int\nieee80211_newstate(")
newstate_end = proto_c.find("\nvoid\nieee80211_set_link_state(", newstate_start)
if newstate_start < 0 or newstate_end < 0:
    fail("missing bounded state transition source")
newstate = proto_c[newstate_start:newstate_end]
ordered(
    newstate,
    "state transition clears delayed comeback owner before publication",
    "ic->ic_assoc_comeback_tu = 0",
    "ic->ic_assoc_comeback_pending = 0",
    "ic->ic_assoc_comeback_reassoc = 0",
    "ic->ic_assoc_comeback_retries = 0",
    "ic->ic_state = nstate",
)

print("PASS: bounded association comeback preserves SAE owner before retry")
PY
