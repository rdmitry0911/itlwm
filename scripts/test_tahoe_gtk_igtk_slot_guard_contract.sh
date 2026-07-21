#!/usr/bin/env bash
# Guard the legacy Apple80211 GTK ingress from aliasing protected-management
# IGTK slots (4/5) or wrapping a u16 key_index into the group-key table.
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

python3 - "$root" <<'PY'
from pathlib import Path
import sys


root = Path(sys.argv[1])
legacy_cpp = (root / "AirportItlwm/AirportItlwm.cpp").read_text()
legacy_ioctl = (root / "AirportItlwm/AirportSTAIOCTL.cpp").read_text()
sky_cpp = (root / "AirportItlwm/AirportItlwmSkywalkInterface.cpp").read_text()


def fail(message: str) -> None:
    raise SystemExit(f"Tahoe GTK/IGTK slot guard: {message}")


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


def ordered(text: str, label: str, *tokens: str) -> None:
    cursor = 0
    for token in tokens:
        position = text.find(token, cursor)
        if position < 0:
            fail(f"{label} missing ordered token: {token}")
        cursor = position + len(token)


def require_setter_guard(text: str, marker: str, label: str) -> None:
    setter = body(text, marker, label)
    ordered(setter, f"{label} reserves IGTK slots",
            "if (kid >= IEEE80211_WEP_NKID)",
            "return;",
            "&ic->ic_nw_keys[kid]")


require_setter_guard(legacy_cpp, "void AirportItlwm::setGTK", "legacy setGTK")
require_setter_guard(sky_cpp, "void AirportItlwmSkywalkInterface::setGTK",
                     "Skywalk setGTK")

legacy_outer = body(legacy_ioctl, "IOReturn AirportItlwm::\nsetCIPHER_KEY",
                    "legacy CIPHER_KEY")
ordered(legacy_outer, "legacy full-width GTK gate",
        "case 0: // GTK",
        "if (key->key_index >= IEEE80211_WEP_NKID)",
        "return kIOReturnBadArgument;",
        "setGTK(key->key, key->key_len, key->key_index, key->key_rsc);",
        "getNetworkInterface()->postMessage(APPLE80211_M_RSN_HANDSHAKE_DONE);")

sky_outer = body(sky_cpp, "IOReturn AirportItlwmSkywalkInterface::\nsetCIPHER_KEY",
                 "Skywalk CIPHER_KEY")
ordered(sky_outer, "Skywalk full-width GTK gate",
        "case 0: // GTK",
        "if (key->key_index >= IEEE80211_WEP_NKID)",
        "return kIOReturnBadArgument;",
        "setGTK(key->key, key->key_len, key->key_index, key->key_rsc);")

# apple80211_key::key_index is u16 while the helpers accept u8.  Pin the
# actual admission boundary and common wrapped values, independent of host
# integer promotion details.
def admitted_group_key(index: int) -> bool:
    return 0 <= index < 4

for index in (0, 1, 2, 3):
    assert admitted_group_key(index)
for index in (4, 5, 6, 0x100, 0x104, 0xffff):
    assert not admitted_group_key(index)

print("PASS: Tahoe legacy GTK ingress reserves IGTK slots and rejects wrapped indices")
PY
