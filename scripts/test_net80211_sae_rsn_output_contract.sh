#!/usr/bin/env bash
# Source-level contract for the narrow direct-WCL SAE RSN-output exception.
# The live predicate is owned by ieee80211_proto.c; this test pins the only
# output-side consequence: a single RSN type-8 suite for that exact binding.
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

python3 - "$root" <<'PY'
from pathlib import Path
import sys


root = Path(sys.argv[1])
output_c = (root / "itl80211/openbsd/net80211/ieee80211_output.c").read_text()


def fail(message):
    raise SystemExit(f"net80211 direct-WCL SAE RSN-output contract: {message}")


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


def ordered(text, label, *needles):
    position = 0
    for needle in needles:
        found = text.find(needle, position)
        if found < 0:
            fail(f"{label} missing ordered token: {needle}")
        position = found + len(needle)


rsn = body(output_c, "u_int8_t *\nieee80211_add_rsn_body(",
           "RSN serializer")
sae_gate = "if (!wpa && (ni->ni_rsnakms & IEEE80211_AKM_SAE) &&\n" \
           "        ieee80211_sae_wcl_request_bound_current(ic, ni))"
if sae_gate not in rsn:
    fail("SAE suite must require both RSN (not WPA1) and exact WCL binding")

sae_start = rsn.find(sae_gate)
sae_end = rsn.find("\n    }", sae_start)
if sae_start < 0 or sae_end < 0:
    fail("missing bounded SAE output clause")
sae_clause = rsn[sae_start:sae_end]
for token in (
        "memcpy(frm, oui, 3); frm += 3;",
        "*frm++ = 8;",
        "count++;",
):
    if token not in sae_clause:
        fail(f"gated SAE clause lacks serializer step: {token}")
if sae_clause.count("*frm++ = 8;") != 1 or sae_clause.count("count++;") != 1:
    fail("gated SAE case must emit exactly one type-8 suite and increment once")
if rsn.count("*frm++ = 8;") != 1:
    fail("RSN serializer may contain only the one direct-WCL SAE suite")

# Existing AKMs remain independently serialized before the SAE exception.
ordered(rsn, "legacy AKM preservation before SAE",
        "IEEE80211_AKM_8021X", "*frm++ = 1;",
        "IEEE80211_AKM_PSK", "*frm++ = 2;",
        "IEEE80211_AKM_SHA256_8021X", "*frm++ = 5;",
        "IEEE80211_AKM_SHA256_PSK", "*frm++ = 6;",
        sae_gate)


def akm_suite_types(wpa, akms, bound):
    """Behavioral model for the source-pinned AKM-list clauses above."""
    IEEE8021X = 0x00000001
    PSK = 0x00000002
    SHA256_8021X = 0x00000004
    SHA256_PSK = 0x00000008
    SAE = 0x00000010
    suites = []
    if akms & IEEE8021X:
        suites.append(1)
    if akms & PSK:
        suites.append(2)
    if not wpa and akms & SHA256_8021X:
        suites.append(5)
    if not wpa and akms & SHA256_PSK:
        suites.append(6)
    if not wpa and akms & SAE and bound:
        suites.append(8)
    return suites


# Exact, bound direct-WCL SAE selection: count one, type 8.
assert akm_suite_types(False, 0x00000010, True) == [8]
# SAE discovery alone cannot alter association output: count zero, no type 8.
assert akm_suite_types(False, 0x00000010, False) == []
# WPA1 vendor IEs must never carry RSN SAE type 8, even with a bound request.
assert akm_suite_types(True, 0x00000010, True) == []
# A bound request cannot manufacture SAE when the negotiated AKM lacks it.
assert akm_suite_types(False, 0x00000002, True) == [2]
# The gated exception adds SAE without suppressing legacy AKMs.
assert akm_suite_types(False, 0x00000012, True) == [2, 8]
assert akm_suite_types(False, 0x00000012, False) == [2]

print("PASS: direct-WCL SAE emits one RSN type-8 suite only when bound")
PY
