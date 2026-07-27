#!/usr/bin/env bash
# Static contract for the SAE AKM-defined EAPOL-Key descriptor path.
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

python3 - "$root" <<'PY'
from pathlib import Path
import re
import sys


root = Path(sys.argv[1])
wire = (root / "itl80211/openbsd/net80211/ieee80211.h").read_text()
crypto = (root / "itl80211/openbsd/net80211/ieee80211_crypto.c").read_text()
pae_input = (
    root / "itl80211/openbsd/net80211/ieee80211_pae_input.c"
).read_text()
pae_output = (
    root / "itl80211/openbsd/net80211/ieee80211_pae_output.c"
).read_text()


def fail(message: str) -> None:
    raise SystemExit(f"net80211 SAE EAPOL-Key descriptor contract: {message}")


def require(text: str, needle: str, label: str) -> None:
    if needle not in text:
        fail(f"missing {label}: {needle}")


def block_after(source: str, opening: int, label: str) -> str:
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1:index]
    fail(f"unterminated {label}")


def body(source: str, name: str, label: str) -> str:
    pattern = re.compile(
        r"\b" + re.escape(name) + r"\s*\([^;{}]*\)\s*\{", re.S
    )
    match = pattern.search(source)
    if match is None:
        fail(f"missing {label}")
    return block_after(
        source, source.rfind("{", match.start(), match.end()), label
    )


def ordered(text: str, label: str, *needles: str) -> None:
    cursor = 0
    for needle in needles:
        index = text.find(needle, cursor)
        if index < 0:
            fail(f"{label} missing ordered token: {needle}")
        cursor = index + len(needle)


require(
    wire,
    "#define EAPOL_KEY_DESC_AKM_DEFINED\t0",
    "AKM-defined descriptor version",
)

input_body = body(
    pae_input, "ieee80211_eapol_key_input", "EAPOL-Key input"
)
ordered(
    input_body,
    "SAE descriptor admission before the generic V1-V3 fence",
    "desc = info & EAPOL_KEY_VERSION_MASK;",
    "if (ni->ni_rsnakms == IEEE80211_AKM_SAE)",
    "desc != EAPOL_KEY_DESC_AKM_DEFINED",
    "else if (desc < EAPOL_KEY_DESC_V1 || desc > EAPOL_KEY_DESC_V3)",
    "ieee80211_is_sha256_akm",
    "desc != EAPOL_KEY_DESC_V3",
)

output_body = body(
    pae_output, "ieee80211_send_eapol_key", "EAPOL-Key output"
)
ordered(
    output_body,
    "SAE descriptor emission before ordinary SHA256 V3",
    "if (ni->ni_rsnakms == IEEE80211_AKM_SAE)",
    "info |= EAPOL_KEY_DESC_AKM_DEFINED;",
    "else if (ieee80211_is_sha256_akm",
    "info |= EAPOL_KEY_DESC_V3;",
)

mic = body(crypto, "ieee80211_eapol_key_mic", "EAPOL-Key MIC")
ordered(
    mic,
    "SAE version-zero AES-CMAC",
    "case EAPOL_KEY_DESC_AKM_DEFINED:",
    "case EAPOL_KEY_DESC_V3:",
    "AES_CMAC_Init",
    "AES_CMAC_SetKey",
    "AES_CMAC_Final",
)

encrypt = body(
    crypto, "ieee80211_eapol_key_encrypt", "EAPOL-Key encryption"
)
ordered(
    encrypt,
    "SAE version-zero AES Key Wrap encryption",
    "case EAPOL_KEY_DESC_AKM_DEFINED:",
    "case EAPOL_KEY_DESC_V2:",
    "case EAPOL_KEY_DESC_V3:",
    "aes_key_wrap_set_key_wrap_only",
    "aes_key_wrap(",
)

decrypt = body(
    crypto, "ieee80211_eapol_key_decrypt", "EAPOL-Key decryption"
)
ordered(
    decrypt,
    "SAE version-zero AES Key Wrap decryption",
    "case EAPOL_KEY_DESC_AKM_DEFINED:",
    "case EAPOL_KEY_DESC_V2:",
    "case EAPOL_KEY_DESC_V3:",
    "aes_key_wrap_set_key",
    "aes_key_unwrap(",
)

print("net80211 SAE EAPOL-Key descriptor contract: PASS")
PY
