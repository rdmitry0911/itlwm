#!/usr/bin/env bash
# Source-level contract for the software-CCMP half of the asynchronous PMF
# handoff.  The Tahoe kernel target is not available to host CI, so this
# proves the ownership and ordering boundaries directly from the sources.
set -euo pipefail

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
fail() { printf 'FAIL: %s\n' "$*" >&2; exit 1; }

python3 - "$root" <<'PY'
from pathlib import Path
import sys

root = Path(sys.argv[1])
crypto_h = (root / "itl80211/openbsd/net80211/ieee80211_crypto.h").read_text()
crypto = (root / "itl80211/openbsd/net80211/ieee80211_crypto.c").read_text()
ccmp = (root / "itl80211/openbsd/net80211/ieee80211_crypto_ccmp.c").read_text()
proto = (root / "itl80211/openbsd/net80211/ieee80211_proto.c").read_text()
node = (root / "itl80211/openbsd/net80211/ieee80211_node.c").read_text()
var = (root / "itl80211/openbsd/net80211/ieee80211_var.h").read_text()


def fail(message):
    raise SystemExit(f"software-CCMP MFP lifetime: {message}")


def require(text, needle, label):
    if needle not in text:
        fail(f"{label}: missing {needle!r}")


def ordered(text, label, *needles):
    pos = 0
    for needle in needles:
        found = text.find(needle, pos)
        if found < 0:
            fail(f"{label}: missing ordered {needle!r}")
        pos = found + len(needle)


def body(text, marker, label):
    start = text.find(marker)
    if start < 0:
        fail(f"missing {label}")
    opening = text.find("{", start)
    if opening < 0:
        fail(f"missing opening brace for {label}")
    depth = 0
    for pos in range(opening, len(text)):
        if text[pos] == "{":
            depth += 1
        elif text[pos] == "}":
            depth -= 1
            if depth == 0:
                return text[opening + 1:pos]
    fail(f"unterminated {label}")


# A live software descriptor has an explicit ownership mark and a private
# retirement queue.  It is separate from the BIP reader protocol because
# CCMP copies AES state before releasing the selected-BSS lock.
for needle in (
    "IEEE80211_KEY_PAE_MFP_LIVE",
    "ieee80211_ccmp_key_publish_retire_locked",
    "ieee80211_ccmp_key_unpublish_retire",
    "ieee80211_ccmp_lifetime_drain",
):
    require(crypto_h, needle, "public ownership API")
require(var, "ic_ccmp_retired", "per-interface retired-context queue")
require(var, "ic_ccmp_next_generation", "per-interface CCMP publication generation")
require(ccmp, "publication_generation", "non-reusable CCMP publication token")
require(ccmp, "TAILQ_INSERT_TAIL(&ic->ic_ccmp_retired", "retire queue")
require(ccmp, "ieee80211_ccmp_reap", "out-of-lock reaper")
publish = body(ccmp, "ieee80211_ccmp_key_publish_retire_locked(",
               "CCMP publication")
ordered(publish, "CCMP publication assigns generation before descriptor copy",
        "newctx->publication_generation = ++ic->ic_ccmp_next_generation;",
        "*slot = *new_key;")

tx = body(ccmp, "ieee80211_ccmp_pae_tx_snapshot(", "CCMP TX snapshot")
ordered(tx, "TX snapshot under selected-BSS lock",
        "IOSimpleLockLockDisableInterrupt(lock)",
        "if ((key->k_flags & IEEE80211_KEY_PAE_MFP_LIVE) == 0)",
        "*snapshot = *key;",
        "snapshot->k_tsc = ++key->k_tsc;", "*ctx_snapshot = *ctx;",
        "IOSimpleLockUnlockEnableInterrupt(lock, irq)")
rx = body(ccmp, "ieee80211_ccmp_pae_rx_snapshot(", "CCMP RX snapshot")
ordered(rx, "RX snapshot under selected-BSS lock",
        "IOSimpleLockLockDisableInterrupt(lock)",
        "if ((key->k_flags & IEEE80211_KEY_PAE_MFP_LIVE) == 0)",
        "*snapshot = *key;",
        "*ctx_snapshot = *ctx;", "*generation = ctx->publication_generation;",
        "IOSimpleLockUnlockEnableInterrupt(lock, irq)")
commit = body(ccmp, "ieee80211_ccmp_pae_rx_commit(", "CCMP RX commit")
ordered(commit, "RX commit checks current non-reusable publication token",
        "IOSimpleLockLockDisableInterrupt(lock)",
        "ctx->publication_generation == generation",
        "if (pn > *rsc)", "*rsc = pn",
        "IOSimpleLockUnlockEnableInterrupt(lock, irq)")

encrypt = body(ccmp, "ieee80211_ccmp_encrypt(", "CCMP encrypt")
ordered(encrypt, "encrypt consumes only the TX snapshot for PAE-live keys",
        "ieee80211_ccmp_pae_tx_snapshot", "txkey = &key_snapshot;",
        "ctx = &ctx_snapshot;", "ivp[0] = txkey->k_tsc")
require(encrypt, "explicit_bzero(&ctx_snapshot", "encrypt snapshot wipe")
decrypt = body(ccmp, "ieee80211_ccmp_decrypt(", "CCMP decrypt")
ordered(decrypt, "decrypt commits only after MIC validation",
        "ieee80211_ccmp_pae_rx_snapshot", "ieee80211_ccmp_get_pn",
        "timingsafe_bcmp", "ieee80211_ccmp_pae_rx_commit")
require(decrypt, "if (!ieee80211_ccmp_pae_rx_commit", "fail-closed rekey race")
require(decrypt, "explicit_bzero(&ctx_snapshot", "decrypt snapshot wipe")

# Publish must validate before BIP's only fallible transfer, then use the
# retirement helper rather than directly overwriting a live software key.
finish = body(proto, "ieee80211_pae_mfp_txn_finish_publish_locked(",
              "atomic PAE finish")
ordered(finish, "CCMP validation precedes BIP publication",
        "ieee80211_ccmp_key_publishable_locked",
        "ieee80211_bip_key_publish_retire_locked")
require(finish, "ieee80211_ccmp_key_publish_retire_locked",
        "atomic PTK/GTK retirement handoff")
complete = body(proto, "ieee80211_pae_mfp_txn_complete(",
                "PAE completion")
ordered(complete, "CCMP reap occurs after backend finish",
        "finish_error = (*finish)(ic, id);", "ieee80211_ccmp_reap(ic)")

# Delete and final node teardown ask the PAE owner under the leaf lock.  No
# unlocked LIVE probe may route a half-published descriptor to raw free.
unpublish = body(ccmp, "int\nieee80211_ccmp_key_unpublish_retire(",
                 "atomic CCMP unpublish")
ordered(unpublish, "unpublish atomically detaches a legacy value",
        "IOSimpleLockLockDisableInterrupt(lock)",
        "if ((key->k_flags & IEEE80211_KEY_PAE_MFP_LIVE) == 0)",
        "*out = *key;", "explicit_bzero(key, sizeof(*key));",
        "error = ENOENT", "IOSimpleLockUnlockEnableInterrupt(lock, irq)")
require(unpublish, "return EOPNOTSUPP", "no-lock legacy fallback")
delete = body(crypto, "ieee80211_delete_key(", "generic key delete")
ordered(delete, "generic atomic CCMP unpublish",
        "ieee80211_ccmp_key_unpublish_retire", "if (error == 0)",
        "if (error == ENOENT)", "delete_key = &callback_key;",
        "else if (error != EOPNOTSUPP)", "switch (delete_key->k_cipher)")
cleanup = body(node, "ieee80211_node_cleanup_internal(", "node cleanup")
ordered(cleanup, "node-finalization atomic CCMP retire",
        "ieee80211_ccmp_key_unpublish_retire", "if (error == ENOENT)",
        "ieee80211_delete_key(ic, ni, &detached_key)")
destroy = body(proto, "ieee80211_pae_selected_bss_lock_destroy(",
               "selected-BSS lock destroy")
require(destroy, "ieee80211_ccmp_lifetime_drain", "terminal lifetime drain")
drain = body(ccmp, "int\nieee80211_ccmp_lifetime_drain(",
             "CCMP lifetime drain")
require(drain, "ic->ic_bss->ni_pairwise_key.k_flags",
        "terminal pairwise-key lifetime drain")

print("PASS: software-CCMP PAE publication, snapshot, rekey retirement, and teardown contracts")
PY
