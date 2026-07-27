#!/usr/bin/env bash
# Admission contract for the ordinary WCL CIPHER_PWD -> local WPA-PSK PMK
# route.  This route is intentionally distinct from direct SAE password
# handling and from the keychain/PLTI external-PMK fallback.
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
tmp="$(mktemp -d /tmp/aiam-wcl-pwd-contract.XXXXXX)"
trap 'rm -rf "$tmp"' EXIT

cat >"$tmp/auth_contract.cpp" <<'CPP'
#include "AirportItlwm/TahoeAssociationAuthContracts.hpp"

using namespace TahoeAssociationAuthContracts;

int main()
{
    if (!mayDeriveWpaPskPmkFromWclPassword(kAuthWpaPsk) ||
        !mayDeriveWpaPskPmkFromWclPassword(kAuthWpa2Psk) ||
        !mayDeriveWpaPskPmkFromWclPassword(kAuthSha256Psk) ||
        !mayDeriveWpaPskPmkFromWclPassword(
            kAuthWpaPsk | kAuthWpa2Psk) ||
        mayDeriveWpaPskPmkFromWclPassword(0) ||
        mayDeriveWpaPskPmkFromWclPassword(kAuthWpa) ||
        mayDeriveWpaPskPmkFromWclPassword(kAuthWpa2) ||
        mayDeriveWpaPskPmkFromWclPassword(kAuthSha2568021x) ||
        mayDeriveWpaPskPmkFromWclPassword(kAuthWpa3Sae) ||
        mayDeriveWpaPskPmkFromWclPassword(
            kAuthWpa3Sae | kAuthWpa2Psk) ||
        mayDeriveWpaPskPmkFromWclPassword(
            kAuthWpa2Psk | (1U << 31)))
        return 1;
    return 0;
}
CPP

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -I"$root" \
    "$tmp/auth_contract.cpp" -o "$tmp/auth_contract"
"$tmp/auth_contract"

python3 - "$root" <<'PY'
from hashlib import pbkdf2_hmac
from pathlib import Path
import re
import sys


root = Path(sys.argv[1])
sky = (root / "AirportItlwm/AirportItlwmSkywalkInterface.cpp").read_text()
auth = (root / "AirportItlwm/TahoeAssociationAuthContracts.hpp").read_text()
regdiag = (root / "include/ClientKit/AirportItlwmRegDiag.h").read_text()
v2 = (root / "AirportItlwm/AirportItlwmV2.cpp").read_text()


def fail(message):
    raise SystemExit(f"WCL WPA-PSK password PMK contract: {message}")


def require(text, needle, label):
    if needle not in text:
        fail(f"missing {label}: {needle}")


def ordered(text, label, *needles):
    cursor = 0
    for needle in needles:
        pos = text.find(needle, cursor)
        if pos < 0:
            fail(f"{label} missing ordered token: {needle}")
        cursor = pos + len(needle)


def body(text, marker, label):
    start = text.find(marker)
    if start < 0:
        fail(f"missing {label}")
    opening = text.find("{", start)
    if opening < 0:
        fail(f"missing body for {label}")
    depth = 0
    for pos in range(opening, len(text)):
        if text[pos] == "{":
            depth += 1
        elif text[pos] == "}":
            depth -= 1
            if depth == 0:
                return text[opening + 1:pos]
    fail(f"unterminated {label}")


predicate = body(
    auth, "inline bool mayDeriveWpaPskPmkFromWclPassword",
    "exact PSK password predicate")
require(predicate, "return isAuditedPskPmkAuth(authtypeUpper);",
        "shared exact PSK-only authorization")

assoc = body(
    sky, "IOReturn AirportItlwmSkywalkInterface::\nsetWCL_ASSOCIATEImpl",
    "WCL association ingress")
start = assoc.find("const bool wclPskPasswordCarrier =")
end = assoc.find(
    "const TahoeExternalPmkScanResumeContracts::Facts scanResumeFacts", start)
if start < 0 or end < 0:
    fail("missing bounded WCL password-derived PMK block")
route = assoc[start:end]

ordered(
    route, "credential validation, derivation, install, and erasure",
    "wcl_key_cipher == APPLE80211_CIPHER_PWD",
    "mayDeriveWpaPskPmkFromWclPassword(auth_upper)",
    "raw_ssid_len == 0",
    "raw_ssid_len > APPLE80211_MAX_SSID_LEN",
    "wcl_key_len < kItlSaeWclCredentialV1PassphraseMinLength",
    "wcl_key_len > kItlSaeWclCredentialV1PassphraseMaxLength",
    "password[i] < 0x20 || password[i] > 0x7e",
    "memcpy(wclPskPassphrase, password, wcl_key_len)",
    "pbkdf2_sha1(",
    "wclPskPassphrase, ssid, raw_ssid_len, 4096",
    "sizeof(wclPskDerivedPmk)",
    "wclPskPasswordReady = true;",
    "explicit_bzero(wclPskPassphrase",
    "const bool directWclLocalPmk =",
    "directWclPmk || wclPskPasswordReady",
    "directWclLocalPmkBytes",
    "associateSSID(",
    "directWclLocalPmk ? IEEE80211_PMK_LEN : 0",
    "0, directWclLocalPmk, !directWclLocalPmk",
    "explicit_bzero(wclPskDerivedPmk")

for needle in (
    '"WCL_PWD"',
    "kAirportItlwmRegDiagPmkDecisionRejectInput",
    "kAirportItlwmRegDiagPmkDecisionRejectLength",
    "kAirportItlwmRegDiagPmkDecisionAccepted",
):
    require(route, needle, "credential-safe diagnostic decision")
for forbidden in (
    "publishPendingAssocTarget(",
    "waitForExternalPmkReady(",
    "AgentLookupProjectPSK",
    "AgentDerivePMK_PBKDF2",
):
    if forbidden in route:
        fail(f"password-derived local PMK route borrows external Agent path: {forbidden}")
if re.search(r"XYLog\([^;]*(?:password|passphrase|DerivedPmk)", route, re.I | re.S):
    fail("credential bytes or buffers reach a log statement")

require(regdiag, "kAirportItlwmRegDiagPmkSourceWclPwd = 5",
        "non-secret WCL password PMK source ID")
ordered(v2, "WCL password PMK source mapping",
        'strcmp(sourceTag, "WCL_PWD") == 0',
        "kAirportItlwmRegDiagPmkSourceWclPwd")

# IEEE 802.11i published WPA-PSK test vector.
pmk = pbkdf2_hmac("sha1", b"password", b"IEEE", 4096, 32)
if pmk.hex() != "f42c6fc52df0ebef9ebb4b90b38a5f902e83fe1b135a70e23aed762e9710a12e":
    fail("PBKDF2-SHA1/4096/32 reference vector mismatch")

print("PASS: bounded WCL CIPHER_PWD derives a local WPA-PSK PMK without Agent fallback")
PY
