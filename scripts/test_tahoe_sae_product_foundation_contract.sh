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

bash "$root/scripts/test_tahoe_openwrt_mbedtls_sae_intake_contract.sh"

clang -std=c11 -Wall -Wextra -Werror \
    -I"$root/itl80211/openbsd/net80211" \
    "$root/tests/net80211_sae_policy_test.c" \
    -o "$tmpdir/net80211-sae-policy"
"$tmpdir/net80211-sae-policy"

clang -std=c11 -Wall -Wextra -Werror \
    -I"$root/itl80211/openbsd/net80211" \
    "$root/tests/net80211_sae_auth_contract_test.c" \
    -o "$tmpdir/net80211-sae-auth-contract-c"
"$tmpdir/net80211-sae-auth-contract-c"

clang++ -std=c++14 -Wall -Wextra -Werror -x c++ \
    -I"$root/itl80211/openbsd/net80211" \
    "$root/tests/net80211_sae_auth_contract_test.c" \
    -o "$tmpdir/net80211-sae-auth-contract-cpp"
"$tmpdir/net80211-sae-auth-contract-cpp"

# The future pure-SAE admission and relay FSM are deliberately standalone
# until their kernel/UserClient owners exist.  Compile their semantic models
# as both C and C++ on every foundation run so their ABI-neutral boundaries
# cannot silently rot while the product path remains fail-closed.
clang -std=c11 -Wall -Wextra -Werror \
    -I"$root/itl80211/openbsd" \
    -I"$root/itl80211/openbsd/net80211" \
    "$root/tests/net80211_sae_admission_test.c" \
    -o "$tmpdir/net80211-sae-admission-c"
"$tmpdir/net80211-sae-admission-c"

clang++ -std=c++14 -Wall -Wextra -Werror -x c++ \
    -I"$root/itl80211/openbsd" \
    -I"$root/itl80211/openbsd/net80211" \
    "$root/tests/net80211_sae_admission_test.c" \
    -o "$tmpdir/net80211-sae-admission-cpp"
"$tmpdir/net80211-sae-admission-cpp"

clang -std=c11 -Wall -Wextra -Werror \
    -I"$root/include" \
    -I"$root/include/ClientKit" \
    "$root/tests/tahoe_sae_relay_fsm_v1_test.c" \
    -o "$tmpdir/tahoe-sae-relay-fsm-c"
"$tmpdir/tahoe-sae-relay-fsm-c"

clang++ -std=c++14 -Wall -Wextra -Werror -x c++ \
    -I"$root/include" \
    -I"$root/include/ClientKit" \
    "$root/tests/tahoe_sae_relay_fsm_v1_test.c" \
    -o "$tmpdir/tahoe-sae-relay-fsm-cpp"
"$tmpdir/tahoe-sae-relay-fsm-cpp"

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
auth_contract = (root / "itl80211/openbsd/net80211/ieee80211_sae_auth_contract.h").read_text()
auth_test = (root / "tests/net80211_sae_auth_contract_test.c").read_text()
ieee80211_h = (root / "itl80211/openbsd/net80211/ieee80211.h").read_text()
crypto_h = (root / "itl80211/openbsd/net80211/ieee80211_crypto.h").read_text()
crypto_c = (root / "itl80211/openbsd/net80211/ieee80211_crypto.c").read_text()
input_c = (root / "itl80211/openbsd/net80211/ieee80211_input.c").read_text()
output_c = (root / "itl80211/openbsd/net80211/ieee80211_output.c").read_text()
proto_c = (root / "itl80211/openbsd/net80211/ieee80211_proto.c").read_text()
node_h = (root / "itl80211/openbsd/net80211/ieee80211_node.h").read_text()
node_c = (root / "itl80211/openbsd/net80211/ieee80211_node.c").read_text()
priv_h = (root / "itl80211/openbsd/net80211/ieee80211_priv.h").read_text()
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


def ordered(text, label, *needles):
    position = -1
    for needle in needles:
        position = text.find(needle, position + 1)
        if position < 0:
            fail(f"missing ordered {label}: {needle}")


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

# Algorithm 3 and its two standard transaction values are available as a
# self-contained host-order taxonomy only.  The classifier deliberately has
# no frame, status, credential, retry, anti-clogging, epoch, or authorization
# semantics, and it has no production caller yet.
for token in (
    "#include <stdint.h>",
    "#define IEEE80211_AUTH_ALG_SAE 0x0003u",
    "IEEE80211_SAE_AUTH_TRANSACTION_COMMIT = 1u",
    "IEEE80211_SAE_AUTH_TRANSACTION_CONFIRM = 2u",
    "IEEE80211_SAE_AUTH_STATE_INVALID = 0",
    "IEEE80211_SAE_AUTH_STATE_COMMIT",
    "IEEE80211_SAE_AUTH_STATE_CONFIRM",
    "ieee80211_sae_auth_state_from_fixed_fields",
    "algorithm != IEEE80211_AUTH_ALG_SAE",
):
    require(auth_contract, token, "Algorithm-3 host-order taxonomy")
for token in (
    "not an SAE state machine",
    "does not interpret",
    "status, body, pointers, credentials, group selection, retries,",
    "anti-clogging, epochs, or authorization.",
):
    require(auth_contract, token, "taxonomy scope boundary")
require(ieee80211_h, '#include "ieee80211_sae_auth_contract.h"',
        "canonical Algorithm-3 taxonomy inclusion")
require(auth_test, '#include "ieee80211_sae_auth_contract.h"',
        "standalone Algorithm-3 taxonomy unit test")
for text, label in ((input_c, "net80211 RX"),
                    (output_c, "net80211 TX"),
                    (proto_c, "net80211 protocol"),
                    (crypto_c, "net80211 crypto"),
                    (v2, "Tahoe controller")):
    forbid(text, "ieee80211_sae_auth_contract.h",
           "premature Algorithm-3 production include in " + label)
    forbid(text, "IEEE80211_AUTH_ALG_SAE",
           "premature Algorithm-3 production use in " + label)
require(input_c, "if (algo != IEEE80211_AUTH_ALG_OPEN)",
        "active Open-System-only RX gate")
require(output_c, "LE_WRITE_2(frm, IEEE80211_AUTH_ALG_OPEN)",
        "active Open-System-only TX builder")
require(proto_c, "IEEE80211_AUTH_OPEN_REQUEST",
        "active Open-System request producer")

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

# RSNXE, ExtCap, raw rate membership selectors, and SAE_EXT_KEY are parsed
# only from scan input and stored as fixed facts. Malformed, duplicate,
# unmodeled, unsupported, and inconsistent forms survive only as fail-closed
# flags; the raw IE table is not an authority for a future Agent relay.
for token in (
    "IEEE80211_SAE_SCAN_RSNXE_PRESENT",
    "IEEE80211_SAE_SCAN_RSNXE_H2E",
    "IEEE80211_SAE_SCAN_UNMODELED",
    "IEEE80211_SAE_SCAN_MALFORMED",
    "IEEE80211_SAE_SCAN_AKM_AMBIGUOUS",
    "IEEE80211_SAE_SCAN_EXTCAP_PASSWORD_ID",
    "IEEE80211_SAE_SCAN_EXTCAP_PASSWORD_ID_EXCLUSIVE",
    "IEEE80211_SAE_SCAN_EXTCAP_SAE_PK_EXCLUSIVE",
    "IEEE80211_SAE_SCAN_RSNXE_SAE_PK",
    "IEEE80211_SAE_SCAN_H2E_ONLY_SELECTOR",
    "IEEE80211_SAE_SCAN_SAE_EXT_KEY",
    "IEEE80211_SAE_SCAN_UNSUPPORTED",
    "IEEE80211_SAE_SCAN_PROFILE_INCONSISTENT",
    "IEEE80211_SAE_SCAN_LEGACY_WPA_PRESENT",
    "IEEE80211_SAE_SCAN_RSN_TAIL_MALFORMED",
    "IEEE80211_SAE_SCAN_CENSUS_COMPLETE",
    "field_len != payload_len",
    "payload_len > 16",
    "ieee80211_sae_scan_extcap_bit_is_set",
    "ieee80211_sae_scan_extcap_flags",
    "ieee80211_sae_scan_has_h2e_only_selector",
    "ieee80211_sae_scan_is_extended_key_akm",
    "ieee80211_sae_scan_rsn_tail_is_partial",
    "ieee80211_sae_scan_vendor_ie_is_truncated",
    "ieee80211_sae_scan_note_legacy_wpa",
    "ieee80211_sae_scan_cipher_is_ambiguous",
    "IEEE80211_SAE_SCAN_STRICT_PROFILE_ALLOWED_MASK",
    "ieee80211_sae_scan_profile_is_strict",
    "ieee80211_sae_scan_finalize_flags",
    "ieee80211_sae_scan_finalize_complete_flags",
    "ieee80211_sae_scan_akm_is_ambiguous",
):
    require(policy, token, "strict RSNXE normalizer")
require(ieee80211_h, "IEEE80211_ELEMID_RSNXE          = 244",
        "RSNXE element identity")
require(node_h, "u_int32_t\t\tni_sae_scan_flags;",
        "normalized node scan facts")
require(priv_h, "rsn_nsae_ext_key;",
        "SAE_EXT_KEY census without an SAE-PK alias")
require(input_c, "#include <net80211/ieee80211_sae_policy.h>",
        "scan policy include")
require(input_c, "case IEEE80211_ELEMID_RSNXE:", "RSNXE scan switch")
require(input_c, "case IEEE80211_ELEMID_XCAPS:", "ExtCap scan switch")
require(input_c, "sae_scan_malformed = 1;", "malformed scan marker")
require(input_c, "ni->ni_sae_scan_flags = sae_scan_malformed",
        "scan fact reset")
require(input_c, "ieee80211_sae_scan_rsnxe_flags(\n                rsnxe + 2, rsnxe[1])",
        "RSNXE normalized at scan boundary")
require(input_c, "ieee80211_sae_scan_extcap_flags(\n                xcap + 2, xcap[1])",
        "ExtCap normalized at scan boundary")
require(input_c, "ieee80211_sae_scan_has_h2e_only_selector(rates + 2,",
        "H2E-only Rates selector normalized before rate setup")
require(input_c, "ieee80211_sae_scan_has_h2e_only_selector(xrates + 2,",
        "H2E-only XRates selector normalized before rate setup")
require(input_c, "rsn->rsn_nsae_ext_key++",
        "SAE_EXT_KEY census parser")
require(input_c, "memcmp(frm, IEEE80211_OUI, 3) == 0 &&\n\t\t\t    ieee80211_sae_scan_is_extended_key_akm(frm[3])",
        "RSN-only extended-SAE census")
require(policy, "IEEE80211_SAE_AKM_EXT_KEY\t\t24u",
        "SAE_EXT_KEY type census")
require(policy, "IEEE80211_SAE_AKM_FT_EXT_KEY\t\t25u",
        "FT-SAE-EXT-KEY type census")
require(input_c, "IEEE80211_SAE_SCAN_SAE_EXT_KEY",
        "SAE_EXT_KEY passive scan fact")
require(input_c, "IEEE80211_SAE_SCAN_CIPHER_AMBIGUOUS",
        "pairwise cipher-count ambiguity fact")
require(input_c, "ieee80211_sae_scan_cipher_is_ambiguous(",
        "pairwise cipher-count ambiguity normalizer")
require(policy, "IEEE80211_SAE_SCAN_LEGACY_WPA_PRESENT",
        "raw legacy WPA presence fact")
require(input_c, "ieee80211_sae_scan_note_legacy_wpa(",
        "raw legacy WPA presence normalizer")
require(input_c, "IEEE80211_SAE_SCAN_RSN_TAIL_MALFORMED",
        "partial or trailing RSN-tail fact")
require(policy, "IEEE80211_SAE_SCAN_CENSUS_COMPLETE",
        "completed SAE census marker")
require(priv_h, "rsn_malformed_tail;",
        "partial or trailing RSN parser census")
require(input_c, "rsn->rsn_malformed_tail = 1;",
        "passive partial-RSN-tail marker")
require(policy, "ieee80211_sae_scan_rsn_tail_is_partial",
        "bounded partial RSN-tail normalizer")
rsn_body_start = input_c.find("ieee80211_parse_rsn_body(")
rsn_body_end = input_c.find("\nint\nieee80211_parse_rsn(", rsn_body_start)
if rsn_body_start < 0 or rsn_body_end < 0:
    fail("missing bounded RSN body parser")
rsn_body = input_c[rsn_body_start:rsn_body_end]
if rsn_body.count("ieee80211_sae_scan_rsn_tail_is_partial(") != 6:
    fail("every optional RSN field must retain a bounded partial-tail fact")
ordered(rsn_body, "final RSN tail handling", "frm += 4;",
        "if (frm != efrm)", "rsn->rsn_malformed_tail = 1;")
vendor_start = input_c.find("case IEEE80211_ELEMID_VENDOR:")
vendor_end = input_c.find("case IEEE80211_ELEMID_EXTENSION:", vendor_start)
if vendor_start < 0 or vendor_end < 0:
    fail("missing bounded vendor IE parser")
vendor = input_c[vendor_start:vendor_end]
ordered(vendor, "truncated vendor census",
        "ieee80211_sae_scan_vendor_ie_is_truncated(frm[1])",
        "sae_scan_malformed = 1;")
ordered(vendor, "duplicate WPA vendor census", "if (frm[5] == 1)",
        "if (wpaie != NULL)", "sae_scan_malformed = 1;", "wpaie = frm;")
require(input_c, "ieee80211_sae_scan_cipher_is_ambiguous(\n"
        "                    (rsn.rsn_akms & IEEE80211_AKM_SAE) != 0,",
        "SAE-only pairwise-count ambiguity guard")
for token in ("IEEE80211_CAPINFO_ESS", "IEEE80211_CAPINFO_IBSS",
              "IEEE80211_RSNCAP_NOPAIRWISE"):
    require(node_c, token, "strict selected-BSS infrastructure predicate")
require(input_c, "ieee80211_sae_scan_finalize_complete_flags(\n",
        "cross-IE profile finalization and census publication")
for duplicate in ("if (rates != NULL)", "if (xrates != NULL)",
                  "if (xcap != NULL)"):
    require(input_c, duplicate, "duplicate SAE scan-fact input fails closed")
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
