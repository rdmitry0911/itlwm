#!/usr/bin/env bash
# One-pass static/build contract for the inactive product SAE foundation.
#
# This proves discovery/ABI prerequisites and simultaneously proves that they
# are not a runtime SAE enable: the current PLTI ABI, Open-System auth path,
# MFP capability, and PSK-only configuration remain unchanged.
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
tmpdir="$(mktemp -d "${TMPDIR:-/tmp}/airportitlwm-sae-product.XXXXXX")"
cleanup() {
    rm -rf "$tmpdir"
}
trap cleanup EXIT

clang -std=c11 -Wall -Wextra -Werror \
    -I"$root/itl80211/openbsd/net80211" \
    "$root/tests/net80211_sae_policy_test.c" \
    -o "$tmpdir/net80211-sae-policy"
"$tmpdir/net80211-sae-policy"

clang -std=c11 -Wall -Wextra -Werror \
    -I"$root/include/ClientKit" \
    "$root/tests/tahoe_sae_relay_abi_layout_test.c" \
    -o "$tmpdir/sae-relay-c"
"$tmpdir/sae-relay-c"

clang++ -std=c++14 -Wall -Wextra -Werror -x c++ \
    -I"$root/include/ClientKit" \
    "$root/tests/tahoe_sae_relay_abi_layout_test.c" \
    -o "$tmpdir/sae-relay-cpp"
"$tmpdir/sae-relay-cpp"

python3 - "$root" <<'PY'
from pathlib import Path
import sys


root = Path(sys.argv[1])
relay = (root / "include/ClientKit/AirportItlwmSaeRelayV1.h").read_text()
policy = (root / "itl80211/openbsd/net80211/ieee80211_sae_policy.h").read_text()
ieee80211_h = (root / "itl80211/openbsd/net80211/ieee80211.h").read_text()
crypto_h = (root / "itl80211/openbsd/net80211/ieee80211_crypto.h").read_text()
crypto_c = (root / "itl80211/openbsd/net80211/ieee80211_crypto.c").read_text()
input_c = (root / "itl80211/openbsd/net80211/ieee80211_input.c").read_text()
output_c = (root / "itl80211/openbsd/net80211/ieee80211_output.c").read_text()
node_h = (root / "itl80211/openbsd/net80211/ieee80211_node.h").read_text()
ioctl_h = (root / "itl80211/openbsd/net80211/ieee80211_ioctl.h").read_text()
v2 = (root / "AirportItlwm/AirportItlwmV2.cpp").read_text()
v2_hpp = (root / "AirportItlwm/AirportItlwmV2.hpp").read_text()


def fail(message):
    raise SystemExit(f"SAE product foundation contract: {message}")


def require(text, needle, label):
    if needle not in text:
        fail(f"missing {label}: {needle}")


def forbid(text, needle, label):
    if needle in text:
        fail(f"unexpected {label}: {needle}")


def struct_body(text, name):
    marker = f"struct {name} {{"
    start = text.find(marker)
    if start < 0:
        fail(f"missing relay record {name}")
    end = text.find("\n};", start)
    if end < 0:
        fail(f"unterminated relay record {name}")
    return text[start:end]


# Product relay ABI is naturally aligned, append-only, and binds every reply
# to a particular controller, UserClient, BSS, and attempt. It remains a
# declaration-only contract until the kext state machine owns it.
for token in (
    "kAirportItlwmSaeRelayWaitTargetSelector = 2",
    "kAirportItlwmSaeRelaySubmitReplySelector = 3",
    "kAirportItlwmSaeRelayWaitAuthEventSelector = 4",
    "kAirportItlwmSaeRelayCompleteSelector = 5",
    "kAirportItlwmSaeRelayAbortSelector = 6",
    "kAirportItlwmSaeRelaySelectorCount = 7",
    "kAirportItlwmSaeRelayV1MaxAuthBodyLength 768u",
):
    require(relay, token, "append-only relay ABI")
forbidden_relay_forms = ("__attribute__((packed))", "#pragma pack", "password[",
                         "pwe[", "kck[", "rsnxe_payload")
for token in forbidden_relay_forms:
    forbid(relay, token, "credential/raw-layout relay ABI")

for record in ("AirportItlwmSaeTargetV1", "AirportItlwmSaeAuthEventV1",
               "AirportItlwmSaeAuthReplyV1", "AirportItlwmSaeCompletionV1",
               "AirportItlwmSaeAbortV1"):
    body = struct_body(relay, record)
    for field in ("uint32_t version;", "uint32_t size;",
                  "uint64_t generation;", "uint64_t association_epoch;",
                  "controller_nonce", "client_cookie", "uint8_t bssid[",
                  "uint8_t sta["):
        require(body, field, f"{record} replay binding")
for record in ("AirportItlwmSaeAuthEventV1", "AirportItlwmSaeAuthReplyV1",
               "AirportItlwmSaeCompletionV1", "AirportItlwmSaeAbortV1"):
    require(struct_body(relay, record), "uint64_t event_sequence;",
            f"{record} event ordering")
require(relay, "kAirportItlwmSaeRelayRsnxeH2e = 1u << 0",
        "canonical product RSNXE H2E fact")
require(v2_hpp, "#include <ClientKit/AirportItlwmSaeRelayV1.h>",
        "Tahoe compile-only relay ABI inclusion")

# The current table is intentionally still the legacy two-selector carrier.
# New selectors cannot be accidentally exposed before the lifecycle/FSM owner
# exists, while 0/1 remain byte-for-byte pinned.
for token in (
    "kAirportItlwmUserClientMethod_DeliverPMK = 0",
    "kAirportItlwmUserClientMethod_WaitAssociationTarget = 1",
    "kAirportItlwmUserClientMethod_NumMethods",
):
    require(v2, token, "legacy PLTI selector")
forbid(v2, "kAirportItlwmUserClientMethod_WaitSaeTarget",
       "premature SAE PLTI selector wiring")

# RSN AKM type 8 is recognized only as a discovery/KDF taxonomy. The active
# device configuration remains PSK-only, and no association output advertises
# an AKM it cannot execute.
require(crypto_h, "IEEE80211_AKM_SAE", "RSN SAE AKM taxonomy")
sha256_start = crypto_h.find("ieee80211_is_sha256_akm")
require(crypto_h[sha256_start:], "akm == IEEE80211_AKM_SAE",
        "SAE SHA-256 KDF taxonomy")
require(input_c, "case 8:    /* Simultaneous Authentication of Equals */",
        "RSN-only SAE suite parser")
parse_start = input_c.find("ieee80211_parse_rsn_akm")
parse_end = input_c.find("\n/*\n * Parse an RSN element", parse_start)
if parse_start < 0 or parse_end < 0:
    fail("missing bounded RSN AKM parser")
parse_akm = input_c[parse_start:parse_end]
wpa_end = parse_akm.find("} else if")
if wpa_end < 0 or "case 8:" in parse_akm[:wpa_end]:
    fail("SAE must not be accepted through the WPA1 vendor selector")
require(crypto_c, "ic->ic_rsnakms = IEEE80211_AKM_PSK;",
        "active PSK-only AKM configuration")
forbid(crypto_c, "IEEE80211_AKM_SAE", "active SAE AKM configuration")
forbid(ioctl_h, "IEEE80211_WPA_AKM_SAE", "raw ioctl SAE enable")
forbid(output_c, "IEEE80211_AKM_SAE", "premature RSN SAE output")

# RSNXE is parsed only from scan input and stored as fixed facts. Malformed,
# duplicate, and unmodeled forms survive only as fail-closed flags; the raw IE
# table is not an authority for a future Agent relay.
for token in (
    "IEEE80211_SAE_SCAN_RSNXE_PRESENT",
    "IEEE80211_SAE_SCAN_RSNXE_H2E",
    "IEEE80211_SAE_SCAN_UNMODELED",
    "IEEE80211_SAE_SCAN_MALFORMED",
    "IEEE80211_SAE_SCAN_AKM_AMBIGUOUS",
    "field_len != payload_len",
    "payload_len > 16",
    "ieee80211_sae_scan_akm_is_ambiguous",
):
    require(policy, token, "strict RSNXE normalizer")
require(ieee80211_h, "IEEE80211_ELEMID_RSNXE          = 244",
        "RSNXE element identity")
require(node_h, "u_int8_t\t\tni_sae_scan_flags;",
        "normalized node scan facts")
require(input_c, "#include <net80211/ieee80211_sae_policy.h>",
        "scan policy include")
require(input_c, "case IEEE80211_ELEMID_RSNXE:", "RSNXE scan switch")
require(input_c, "sae_scan_malformed = 1;", "malformed scan marker")
require(input_c, "ni->ni_sae_scan_flags = sae_scan_malformed",
        "scan fact reset")
require(input_c, "ieee80211_sae_scan_rsnxe_flags(\n                rsnxe + 2, rsnxe[1])",
        "RSNXE normalized at scan boundary")
require(input_c, "rsn->rsn_nknownakms = 0;",
        "recognized AKM census reset")
require(input_c, "rsn->rsn_nunknownakms++;",
        "unknown AKM census")
require(input_c, "IEEE80211_SAE_SCAN_AKM_AMBIGUOUS",
        "ambiguous SAE scan fact")
require(input_c, "if (frm != efrm)\n\t\tsae_scan_malformed = 1;",
        "dangling IE tail scan marker")
require(input_c, "if (frm[1] < 1) {\n\t\t\t\tic->ic_stats.is_rx_elem_toosmall++;\n\t\t\t\tsae_scan_malformed = 1;",
        "zero-length extension scan guard")
if input_c.count("IEEE80211_ELEMID_RSNXE") != 1:
    fail("RSNXE must be handled only by the beacon/probe scan path")

print("PASS: Tahoe inactive product SAE discovery and relay foundations")
PY
