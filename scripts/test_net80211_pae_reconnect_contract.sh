#!/usr/bin/env bash
# Regression contract for the async PMF handoff across a BSS replacement.
#
# A backend submit is intentionally called after the selected-BSS leaf has
# dropped.  This test protects the two rules that make a late result benign:
# it can retire only its own generic record, and its ingress can never force
# the replacement association into the old deauth/SCAN terminal.
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

python3 - "$root" <<'PY'
from pathlib import Path
import sys


root = Path(sys.argv[1])
proto = (root / "itl80211/openbsd/net80211/ieee80211_proto.c").read_text()
pae_input = (root / "itl80211/openbsd/net80211/ieee80211_pae_input.c").read_text()


def fail(message: str) -> None:
    raise SystemExit(f"PAE reconnect contract: {message}")


def body(text: str, marker: str, label: str) -> str:
    start = text.find(marker)
    if start < 0:
        fail(f"missing {label}")
    opening = text.find("{", start)
    if opening < 0:
        fail(f"missing body for {label}")
    depth = 0
    for index in range(opening, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[opening + 1:index]
    fail(f"unterminated {label}")


def require(text: str, token: str, label: str) -> None:
    if token not in text:
        fail(f"missing {label}: {token}")


def ordered(text: str, label: str, *tokens: str) -> None:
    cursor = 0
    for token in tokens:
        position = text.find(token, cursor)
        if position < 0:
            fail(f"{label} missing ordered token: {token}")
        cursor = position + len(token)


abort_exact = body(proto, "ieee80211_pae_mfp_txn_abort_exact(",
                   "exact generic abort")
for token in (
    "ic->ic_pae_mfp_txn.active && ic->ic_pae_mfp_txn.id == id",
    "ic->ic_pae_mfp_txn.assoc_epoch == epoch",
    "ic->ic_pae_mfp_txn.ni == ni",
    "current = ic->ic_bss == ni",
    "cancel_id = ieee80211_pae_mfp_txn_cancel_locked(ic, &prepared);",
    "(*cancel)(ic, cancel_id);",
    "return cancel_id != 0 && current;",
):
    require(abort_exact, token, "exact-id abort fence")

begin = body(proto, "ieee80211_pae_mfp_txn_begin(", "PMF transaction begin")
ordered(begin, "callback snapshot is leaf-protected",
        "irq = IOSimpleLockLockDisableInterrupt(lock);",
        "submit = ic->ic_pae_mfp_txn_submit;",
        "cancel = ic->ic_pae_mfp_txn_cancel;",
        "finish = ic->ic_pae_mfp_txn_finish;")
ordered(begin, "stale BSS is not a false busy owner",
        "if (epoch == 0 || ic->ic_bss != ni)", "return ECANCELED;",
        "if (ic->ic_pae_mfp_txn.active)", "return EBUSY;")
ordered(begin, "failed submit retires only its own id",
        "error = (*submit)(ic, id, epoch, ni, &key_copy, stage);",
        "ieee80211_pae_mfp_txn_abort_exact(ic, id, epoch, ni)",
        "return ECANCELED;")
ordered(begin, "backend rejection is not false async acceptance",
        "if (error == EBUSY)", "return EIO;")
if "ieee80211_pae_mfp_txn_abort(ic);" in begin:
    fail("submit failure still performs an unqualified generic abort")

msg3 = body(pae_input, "void\nieee80211_recv_4way_msg3(", "Msg3 ingress")
ordered(msg3, "Msg3 stale result is silent",
        "mfp_error = ieee80211_pae_mfp_msg3_begin",
        "if (mfp_error == ECANCELED)", "return;",
        "if (mfp_error != ENOENT)", "goto deauth;")

group = body(pae_input, "void\nieee80211_recv_rsn_group_msg1(",
             "group-key ingress")
ordered(group, "group stale result is silent",
        "mfp_error = ieee80211_pae_mfp_group_begin",
        "if (mfp_error == ECANCELED)", "return;",
        "if (mfp_error != ENOENT)", "goto deauth;")

print("PASS: stale PMF submit cannot abort or scan a replacement BSS")
PY
