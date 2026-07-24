#!/usr/bin/env bash
# Static contract for the lab-gated pure-SAE WCL ingress.  It proves that one
# bounded CIPHER_PWD carrier can reach the driver-owned Commit/Confirm path;
# it does not prove PMK-to-4-way continuation or a completed WPA3 association.
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

python3 - "$root" <<'PY'
from pathlib import Path
import re
import sys


root = Path(sys.argv[1])
sky = (root / "AirportItlwm/AirportItlwmSkywalkInterface.cpp").read_text()
contracts = (root / "AirportItlwm/TahoeAssociationContracts.hpp").read_text()
owner_registry = (root / "AirportItlwm/TahoeOwnerRegistry.hpp").read_text()
build = (root / "scripts/build_tahoe.sh").read_text()


def fail(message: str) -> None:
    raise SystemExit(f"IWN pure-SAE WCL ingress contract: {message}")


def require(text: str, needle: str, label: str) -> None:
    if needle not in text:
        fail(f"missing {label}: {needle}")


def require_re(text: str, pattern: str, label: str) -> None:
    if re.search(pattern, text, re.S) is None:
        fail(f"missing {label}: /{pattern}/")


def forbid(text: str, needle: str, label: str) -> None:
    if needle in text:
        fail(f"unexpected {label}: {needle}")


def ordered(text: str, label: str, *needles: str) -> None:
    cursor = 0
    for needle in needles:
        position = text.find(needle, cursor)
        if position < 0:
            fail(f"{label} missing ordered token: {needle}")
        cursor = position + len(needle)


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


def function_body(source: str, marker: str, label: str) -> str:
    start = source.find(marker)
    if start < 0:
        fail(f"missing {label}")
    opening = source.find("{", start)
    if opening < 0:
        fail(f"missing body for {label}")
    return block_after(source, opening, label)


def strip_comments(source: str) -> str:
    return re.sub(r"/\*.*?\*/|//[^\n]*", "", source, flags=re.S)


def record_body(source: str, marker: str, label: str) -> str:
    start = source.find(marker)
    if start < 0:
        fail(f"missing {label}")
    opening = source.find("{", start)
    if opening < 0:
        fail(f"missing body for {label}")
    return block_after(source, opening, label)


# The private parser is absent from ordinary artifacts.  A runtime WCL field,
# an MFP capability bit, or a controller route must not be enough to enable it.
for token in (
        "#include <HAL/ItlSaeDriverTarget.h>",
        "#include <HAL/ItlSaeWclCredentialV1.h>",
        "#if defined(IWN_SOFTWARE_PMF_LAB_BUILD) && IWN_SOFTWARE_PMF_LAB_BUILD && \\",
        "ITL_SAE_DRIVER_CRYPTO_AVAILABLE",
        "#define AIRPORT_ITLWM_IWN_SAE_WCL_INGRESS 1",
        "#define AIRPORT_ITLWM_IWN_SAE_WCL_INGRESS 0",
        "IWN_SOFTWARE_PMF_LAB_BUILD=1",
        "IWN software-PMF lab build: direct SAE transport and PMK-to-RSN continuation are compiled; on-air WPA3/4-way success still requires physical validation.",
):
    require(sky if token.startswith(("#include", "#if", "ITL_", "#define")) else build,
            token, "laboratory-only ingress gate")

# Pin the carrier instead of accepting the similarly-shaped legacy WOW input.
for token in (
        "kWclAssociateIoucSelector = 0x1ba",
        "kWclAssociatePayloadLength = 0x6fc",
        "kWowParametersPayloadLength = 0x3ad8",
        "kApModeOffset = 0x0c",
        "kAuthLowerOffset = 0x10",
        "kAuthUpperOffset = 0x14",
        "kSsidLengthOffset = 0x1c",
        "kSsidOffset = 0x20",
        "kWclKeyLengthOffset = 0x44",
        "kWclKeyCipherTypeOffset = 0x48",
        "kWclKeyPasswordOffset = 0x50",
        "kCandidateCountOffset = 0x218",
        "kFirstCandidateBssidOffset = 0x220",
        "kCandidateStride = 0x12",
        "kMaximumCandidateCount == 69",
):
    require(contracts, token, "direct WCL carrier identity")
for field in ("ap_mode", "auth_lower", "auth_upper", "auth_flags",
              "raw_ssid_len", "wcl_key_len", "wcl_key_cipher",
              "candidate_count"):
    require_re(sky, rf"memcpy\s*\(\s*&{field}\s*,\s*raw\s*\+",
               f"unaligned-safe WCL {field} extraction")

association = function_body(
    sky, "IOReturn AirportItlwmSkywalkInterface::\nsetWCL_ASSOCIATEImpl",
    "WCL association ingress")
credential_at = association.find("struct ItlSaeWclCredentialV1 saeCredential")
if credential_at < 0:
    fail("missing private WCL credential carrier")
pure_opening = association.rfind("{", 0, credential_at)
if pure_opening < 0:
    fail("missing pure-SAE branch opening")
pure = block_after(association, pure_opening, "pure-SAE WCL branch")
guard_at = association.rfind("#if AIRPORT_ITLWM_IWN_SAE_WCL_INGRESS",
                            0, pure_opening)
guard_end = association.find("#endif", pure_opening)
if guard_at < 0 or guard_end < credential_at:
    fail("pure-SAE branch is not wholly lab-gated")

require(association[pure_opening - 160:pure_opening],
        "auth_upper == TahoeAssociationAuthContracts::kAuthWpa3Sae",
        "exact pure-SAE branch selector")
for token in (
        "ap_mode != APPLE80211_AP_MODE_INFRA",
        "raw_ssid_len == 0",
        "raw_ssid_len > kItlSaeWclCredentialV1SsidMaxLength",
        "candidate_count == 0",
        "candidate_count > TahoeAssociationContracts::kMaximumCandidateCount",
        "wcl_key_cipher != APPLE80211_CIPHER_PWD",
        "wcl_key_len < kItlSaeWclCredentialV1PassphraseMinLength",
        "wcl_key_len > kItlSaeWclCredentialV1PassphraseMaxLength",
        "ic->ic_state != IEEE80211_S_SCAN",
        "memcpy(saeBssid,",
        "TahoeAssociationContracts::kFirstCandidateBssidOffset",
        "itl_sae_wcl_credential_bssid_is_unicast_nonzero(saeBssid)",
        "saeCredential.ssid_len = static_cast<uint8_t>(raw_ssid_len)",
):
    require(pure, token, "pure-SAE ingress validation")
ordered(pure, "identity and diagnostic validation precede policy publication",
        "raw_ssid_len > kItlSaeWclCredentialV1SsidMaxLength",
        "memcpy(saeBssid,",
        "itl_sae_wcl_credential_bssid_is_unicast_nonzero(saeBssid)",
        "airportItlwmRegDiagShouldBlock(",
        "kAirportItlwmRegDiagBlockHiddenAssoc",
        "airportItlwmRegDiagRecordBlock(",
        "clearExternalPmkEligibilityLocked(\"setWCL_ASSOCIATE_pure_SAE\")")

begin_at = pure.find("saeGeneration = ieee80211_sae_wcl_request_begin(")
if begin_at < 0:
    fail("missing pure-SAE policy begin")
before_begin = strip_comments(pure[:begin_at])
for token in (
        "raw + TahoeAssociationContracts::kWclKeyPasswordOffset",
        "memcpy(saeCredential.password",
        "saeCredential.request_generation =",
):
    forbid(before_begin, token, "private credential access before policy begin")
ordered(pure, "successful begin precedes private password copy",
        "saeGeneration = ieee80211_sae_wcl_request_begin(",
        "if (saeGeneration == 0)",
        "goto sae_out;",
        "instance->getTahoeOwnerRegistry().association =",
        "TahoeOwnerRegistry::AssociationOwner{}",
        "saePassword = raw + TahoeAssociationContracts::kWclKeyPasswordOffset",
        "saeCredential.request_generation = saeGeneration;",
        "memcpy(saeCredential.password, saePassword")

# A pure request must erase stale PLTI/PMK eligibility before it publishes the
# generation.  It stages the bounded private copy before mutating current auth
# type or resuming selection; every later failure revokes and scrubs the same
# generation without borrowing a WCL buffer.
ordered(pure, "pure-SAE begin/stage/resume order",
        "clearExternalPmkEligibilityLocked(\"setWCL_ASSOCIATE_pure_SAE\")",
        "ieee80211_sae_wcl_request_begin(",
        "instance->getTahoeOwnerRegistry().association =",
        "saeCredential.request_generation = saeGeneration;",
        "itl_sae_wcl_credential_is_well_formed(&saeCredential)",
        "fHalService->stageSaeWclCredential(&saeCredential)",
        "setAUTH_TYPE(&authType)",
        "auto &saeAssociationOwner =",
        "const int saeScanResume = ieee80211_sae_wcl_request_resume_scan(")
ordered(pure, "coalesced direct scan returns retry rather than false success",
        "const int saeScanResume = ieee80211_sae_wcl_request_resume_scan(",
        "IEEE80211_SAE_WCL_REQUEST_RESUME_STARTED",
        "IEEE80211_SAE_WCL_REQUEST_RESUME_RETRY",
        "kIOReturnNotReady")
ordered(pure, "failure revokes staged private credential",
        "ieee80211_sae_wcl_request_clear_if_generation(",
        "fHalService->cancelSaeWclCredential(saeGeneration)",
        "instance->getTahoeOwnerRegistry().association =",
        "TahoeOwnerRegistry::AssociationOwner{}",
        "explicit_bzero(&saeCredential",
        "explicit_bzero(saeBssid")

pure_code = strip_comments(pure)
for token in (
        "kAuditedWpa3PskTransitionAuth",
        "kAuthWpa2Psk",
        "candidate_bssid",
        "context_bssid",
        "associateSSID(",
        "publishPendingAssocTarget",
        "installExternalPmk",
        "storeAssocRsnIeOverride",
        "ieee80211_ioctl_setwpaparms",
        "ieee80211_new_state(",
        "IEEE80211_C_MFP",
        "ic->ic_pae_mfp_requested",
):
    forbid(pure_code, token, "legacy/PMF-bypass route in pure-SAE branch")

# The owner is deliberately a public carrier only.  Its stale value is reset
# after begin, then identity/auth/meta appear only after staging and auth-type
# acceptance, before the raw scan method may synchronously select a BSS.
registry_fields = re.findall(r"getTahoeOwnerRegistry\(\)\.([A-Za-z_]\w*)",
                             pure_code)
if not registry_fields or set(registry_fields) != {"association"}:
    fail("pure-SAE branch must clear/fill only TahoeOwnerRegistry::association")
for token in ("getTahoeOwnerRegistry().reset",):
    forbid(pure_code, token, "broad Tahoe owner reset")
owner_record = record_body(owner_registry, "struct AssociationOwner",
                           "public AssociationOwner record")
for token in ("bool hasCarrier", "bool selectedFromCandidate",
              "uint16_t apMode", "uint32_t authLower", "uint32_t authUpper",
              "uint32_t authFlags", "uint32_t ssidLength",
              "uint8_t ssid[33]", "uint8_t selectedBssid[6]",
              "uint8_t candidateBssid[6]"):
    require(owner_record, token, "public association-owner field")
for token in ("password", "pmk", "psk", "kck", "pwe", "ieee80211_key"):
    forbid(owner_record.lower(), token, "secret association-owner field")

owner_at = pure.find("auto &saeAssociationOwner =")
if owner_at < 0:
    fail("missing public pure-SAE association owner publication")
owner_opening = pure.rfind("{", 0, owner_at)
if owner_opening < 0:
    fail("missing public pure-SAE association owner body")
owner_publish = block_after(pure, owner_opening, "public pure-SAE owner publication")
for token in (
        "saeAssociationOwner.hasCarrier = true",
        "saeAssociationOwner.selectedFromCandidate = true",
        "saeAssociationOwner.apMode = ap_mode",
        "saeAssociationOwner.authLower = auth_lower",
        "saeAssociationOwner.authUpper = auth_upper",
        "saeAssociationOwner.authFlags = auth_flags",
        "saeAssociationOwner.ssidLength = raw_ssid_len",
        "saeAssociationOwner.candidateCount = candidate_count",
        "memcpy(saeAssociationOwner.ssid, ssid, raw_ssid_len)",
        "memcpy(saeAssociationOwner.selectedBssid, saeBssid",
        "memcpy(saeAssociationOwner.candidateBssid, saeBssid",
):
    require(owner_publish, token, "public pure-SAE owner publication")
for token in ("saeCredential", "saePassword", "password", "Pmk", "pmk",
              "rsnIe", "boundedRsn"):
    forbid(owner_publish, token, "private/raw-RSN state in pure-SAE owner")

# The ordinary branch remains explicitly fail-closed because this source is
# shared with production artifacts where the preprocessor removes `pure`.
ordinary_tail = association[guard_end:]
ordered(ordinary_tail, "ordinary pure-SAE rejection remains after lab gate",
        "TahoeAssociationAuthContracts::requiresUnsupportedWpa3Auth(",
        "return kIOReturnUnsupported;")


class IngressModel:
    """Value-only acceptance model; it intentionally has no PMK/4-way state."""

    def begin(self, auth: str, state: str, password_kind: str,
              password_length: int, candidates: int, bssid_ok: bool) -> bool:
        return (auth == "pure-sae" and state == "scan" and
                password_kind == "pwd" and 8 <= password_length <= 63 and
                1 <= candidates <= 69 and bssid_ok)


model = IngressModel()
assert model.begin("pure-sae", "scan", "pwd", 8, 1, True)
assert model.begin("pure-sae", "scan", "pwd", 63, 69, True)
assert not model.begin("sae-psk-transition", "scan", "pwd", 12, 1, True)
assert not model.begin("pure-sae", "run", "pwd", 12, 1, True)
assert not model.begin("pure-sae", "scan", "psk", 12, 1, True)
assert not model.begin("pure-sae", "scan", "pwd", 7, 1, True)
assert not model.begin("pure-sae", "scan", "pwd", 12, 0, True)
assert not model.begin("pure-sae", "scan", "pwd", 12, 1, False)

print("PASS: lab-gated pure-SAE WCL ingress reaches bounded staging/scan handoff; this ingress test alone does not claim on-air WPA3")
PY
