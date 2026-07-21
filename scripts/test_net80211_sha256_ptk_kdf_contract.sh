#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
source="$root/itl80211/openbsd/net80211/ieee80211_crypto.c"

/usr/bin/python3 - "$source" <<'PY'
import hashlib
import hmac
import pathlib
import sys


def sha256_kdf(pmk, aa, spa, anonce, snonce, length):
    context = min(aa, spa) + max(aa, spa) + min(anonce, snonce) + max(anonce, snonce)
    label = b"Pairwise key expansion"
    lbits = (length * 8).to_bytes(2, "little")
    output = b""
    for counter in range(1, (length + 31) // 32 + 1):
        output += hmac.new(
            pmk,
            counter.to_bytes(2, "little") + label + context + lbits,
            hashlib.sha256,
        ).digest()
    return output[:length]


def sha1_prf(pmk, aa, spa, anonce, snonce, length):
    context = min(aa, spa) + max(aa, spa) + min(anonce, snonce) + max(anonce, snonce)
    label = b"Pairwise key expansion\x00"
    output = b""
    for counter in range((length + 19) // 20):
        output += hmac.new(pmk, label + context + bytes([counter]), hashlib.sha1).digest()
    return output[:length]


pmk = bytes(range(0x00, 0x20))
aa = bytes.fromhex("001122334455")
spa = bytes.fromhex("66778899aabb")
anonce = bytes(range(0x00, 0x20))
snonce = bytes(range(0x20, 0x40))

expected_sha256 = bytes.fromhex(
    "b3975256bc5390d217b02b7f531c2228"
    "211b29189c62e902662d47294de6e1b2"
    "183fbbf8a9e9a2d3b722923764f3d2f8"
)
assert sha256_kdf(pmk, aa, spa, anonce, snonce, 48) == expected_sha256
assert sha256_kdf(pmk, spa, aa, snonce, anonce, 48) == expected_sha256
sha256_ptk_carrier = expected_sha256 + bytes(64 - len(expected_sha256))
assert len(sha256_ptk_carrier) == 64
assert sha256_ptk_carrier[:48] == expected_sha256
assert sha256_ptk_carrier[48:] == bytes(16)

expected_sha1 = bytes.fromhex(
    "79c315817da2ce917e3264eeac39f908"
    "edafe78205ab10e5fa4d85505235f696"
    "ad89092024c34524f07b21e1f7baf3b4"
    "76e2b3f064f810eb140708be968d02f3"
)
assert sha1_prf(pmk, aa, spa, anonce, snonce, 64) == expected_sha1

text = pathlib.Path(sys.argv[1]).read_text()
for anchor in (
    "memset(ptk, 0, sizeof(*ptk));",
    "kdf = ieee80211_is_sha256_akm(akm) ? ieee80211_kdf : ieee80211_prf;",
    "? sizeof(ptk_label) - 1 : sizeof(ptk_label);",
    "? MIN(48, sizeof(*ptk)) : sizeof(*ptk);",
    "ptk_label, ptk_label_len,",
    "(u_int8_t *)ptk, ptk_len);",
):
    assert anchor in text, anchor
assert text.index("memset(ptk, 0, sizeof(*ptk));") < text.index(
    "(*kdf)(pmk, IEEE80211_PMK_LEN"
)

print("PASS: net80211 SHA256 PTK KDF and zero-tail contract")
PY
