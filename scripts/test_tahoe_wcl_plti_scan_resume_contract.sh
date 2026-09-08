#!/usr/bin/env bash
# Admission contract for the Tahoe WCL external-PMK -> SCAN resume edge.
#
# This is intentionally a source and pure-unit test only.  It proves that the
# repair resumes the ordinary net80211 scan pipeline after a validated WCL
# PMK handoff is ready.  That handoff is either the paired PLTI delivery, the
# exact CIPHER_PMK value already embedded in the final WCL carrier, or the
# bounded WPA/WPA2-PSK CIPHER_PWD carrier from which the driver derives the
# standard PMK locally.  The separately compiled IWN exact-SAE-password
# ingress is prohibited from borrowing either PSK route.
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

bash "$root/scripts/test_payload_builders.sh"

python3 - "$root" <<'PY'
from pathlib import Path
import re
import sys


root = Path(sys.argv[1])
contracts = (root / "AirportItlwm/TahoeExternalPmkScanResumeContracts.hpp").read_text()
auth = (root / "AirportItlwm/TahoeAssociationAuthContracts.hpp").read_text()
sky = (root / "AirportItlwm/AirportItlwmSkywalkInterface.cpp").read_text()
sky_header = (root / "AirportItlwm/AirportItlwmSkywalkInterface.hpp").read_text()
iwn_gate = (root / "AirportItlwm/IwnDirectSaeLabGate.hpp").read_text()
iwx = (root / "itlwm/hal_iwx/ItlIwx.cpp").read_text()
iwn = (root / "itlwm/hal_iwn/ItlIwn.cpp").read_text()
node = (root / "itl80211/openbsd/net80211/ieee80211_node.c").read_text()
layer_runner = (root / "scripts/run_tahoe_sae_quarantine_layer.sh").read_text()
sae_gate = (root / "scripts/test_tahoe_sae_quarantine_contract.sh").read_text()


def fail(message):
    raise SystemExit(f"WCL PLTI scan-resume contract: {message}")


def require(text, needle, label):
    if needle not in text:
        fail(f"missing {label}: {needle}")


def require_re(text, pattern, label):
    if re.search(pattern, text, re.S) is None:
        fail(f"missing {label}: /{pattern}/")


def forbid(text, needle, label):
    if needle in text:
        fail(f"unexpected {label}: {needle}")


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


def preprocessor_block(text, marker, label):
    start = text.find(marker)
    if start < 0:
        fail(f"missing {label}")
    depth = 0
    offset = start
    for line in text[start:].splitlines(keepends=True):
        directive = line.lstrip()
        if directive.startswith("#if"):
            depth += 1
        elif directive.startswith("#endif"):
            depth -= 1
            if depth == 0:
                return text[start:offset + len(line)]
        offset += len(line)
    fail(f"unterminated {label}")


predicate = body(
    contracts, "constexpr bool shouldResumeScanAfterExternalPmk",
    "pure scan-resume predicate")
for token in (
        "facts.pskPmkPolicyAllowed",
        "facts.associationAccepted",
        "facts.observedExternalPmkReady",
        "facts.stateIsScan",
        "facts.pskFlagSet",
        "!facts.externalPmkOwner",
):
    require(predicate, token, "complete fail-closed predicate")
forbid(predicate, "IEEE80211_S_AUTH", "direct authentication transition")
forbid(predicate, "ieee80211_node_join_bss", "direct BSS selection")
forbid(predicate, "IEEE80211_SEND_MGMT", "direct management-frame send")

require(sky_header, "bool *externalPmkReadyObserved",
        "optional caller-local PMK-ready output")
associate = body(
    sky, "IOReturn AirportItlwmSkywalkInterface::associateSSID",
    "Skywalk associateSSID")
ordered(associate, "current PMK readiness handoff",
        "*externalPmkReadyObserved = false;", "waitForExternalPmkReady",
        "*externalPmkReadyObserved = pmkReady;")
require(associate, "Control-flow result only: never expose PMK material",
        "non-secret PMK-ready output boundary")

hidden_assoc = body(
    sky, "IOReturn AirportItlwmSkywalkInterface::\nsetWCL_ASSOCIATEImpl",
    "WCL association ingress")
direct_marker = ("#if AIRPORT_ITLWM_IWN_SAE_WCL_INGRESS\n"
                 "    /*\n"
                 "     * The live ingress")
direct_lab = preprocessor_block(hidden_assoc, direct_marker,
                                "lab-gated exact-SAE WCL ingress")
shared_direct = body(
    sky, "IOReturn AirportItlwmSkywalkInterface::\nstartIwnDirectSaeCredential",
    "shared IWN direct-SAE transaction")
require(iwn_gate, "defined(IWN_SOFTWARE_PMF_LAB_BUILD)",
        "compile-time IWN laboratory gate")
require(iwn_gate, "ITL_SAE_DRIVER_CRYPTO_AVAILABLE",
        "driver-crypto laboratory gate")
require(sky, '#include "IwnDirectSaeLabGate.hpp"',
        "IWN laboratory gate inclusion")
require_re(
    hidden_assoc,
    r"const bool directSaeWclPassword\s*=\s*"
    r"wcl_key_cipher\s*==\s*APPLE80211_CIPHER_PWD\s*&&\s*"
    r"TahoeAssociationAuthContracts::mayUseDirectSaeWclCredential\(\s*"
    r"auth_upper\s*\)\s*;",
    "exact CIPHER_PWD-and-auth SAE selector")
for token in (
        "if (directSaeWclPassword)",
        "startIwnDirectSaeCredential",
):
    require(direct_lab, token, "narrow IWN exact-SAE ingress")
for token in (
        "clearExternalPmkEligibilityLocked(",
        "ieee80211_sae_wcl_request_begin",
        "stageSaeWclCredential",
        "ieee80211_sae_wcl_request_resume_scan",
):
    require(shared_direct, token, "shared direct IWN SAE transaction")
ordered(shared_direct, "IWN exact-SAE avoids PLTI PMK handoff",
        "clearExternalPmkEligibilityLocked(",
        "ieee80211_sae_wcl_request_begin", "stageSaeWclCredential",
        "ieee80211_sae_wcl_request_resume_scan")
for token in (
        "TahoeAssociationAuthContracts::mayUseLocalPskPmk",
        "publishPendingAssocTarget(",
        "waitForExternalPmkReady",
        "assocResult = associateSSID",
        "installExternalPmkLocked",
):
    forbid(direct_lab, token, "PLTI/PMK reuse in IWN pure-SAE ingress")

# The ordinary source path follows the lab-only block.  It must retain the
# historical pure-SAE reject before it evaluates the PSK/PLTI resume edge.
legacy_start = hidden_assoc.find(
    "if (TahoeAssociationAuthContracts::requiresUnsupportedWpa3Auth(")
if legacy_start < 0:
    fail("missing ordinary WCL pure-SAE rejection after lab gate")
legacy_hidden_assoc = hidden_assoc[legacy_start:]
ordered(legacy_hidden_assoc, "WCL PMK scan-resume ordering",
        "requiresUnsupportedWpa3Auth", "return kIOReturnUnsupported;",
        "const bool directWclPmk =",
        "wcl_key_cipher == APPLE80211_CIPHER_PMK",
        "wcl_key_len == IEEE80211_PMK_LEN",
        "TahoeAssociationAuthContracts::mayUseLocalPskPmk(auth_upper)",
        "const bool wclPskPasswordCarrier =",
        "wcl_key_cipher == APPLE80211_CIPHER_PWD",
        "mayDeriveWpaPskPmkFromWclPassword(auth_upper)",
        "raw + TahoeAssociationContracts::kWclKeyPasswordOffset",
        "char wclPskPassphrase[",
        "bool externalPmkReadyObserved = false;",
        "pbkdf2_sha1(",
        "explicit_bzero(wclPskPassphrase",
        "const bool directWclLocalPmk =",
        "&externalPmkReadyObserved);",
        "explicit_bzero(wclPskDerivedPmk",
        "if (directWclLocalPmk && assocResult == kIOReturnSuccess)",
        "externalPmkReadyObserved = true;",
        "TahoeExternalPmkScanResumeContracts::Facts scanResumeFacts",
        "shouldResumeScanAfterExternalPmk(scanResumeFacts)",
        "ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);")
for token in (
        "directWclLocalPmk ? IEEE80211_PMK_LEN : 0",
        "0, directWclLocalPmk, !directWclLocalPmk,",
        "directWclPmkSha256PskCompatibility,",
):
    require(legacy_hidden_assoc, token,
            "exact direct-WCL local PMK ownership handoff")
require(hidden_assoc, "TahoeAssociationAuthContracts::mayUseLocalPskPmk(auth_upper)",
        "exact existing PLTI PSK policy at scan-resume edge")


def direct_sae_password_route(auth, cipher):
    return cipher == "pwd" and auth in {"pure-sae", "sae-psk-transition"}


assert direct_sae_password_route("pure-sae", "pwd")
assert direct_sae_password_route("sae-psk-transition", "pwd")
assert not direct_sae_password_route("sae-psk-transition", "pmk")
assert not direct_sae_password_route("wpa2-psk", "pwd")


def wpa_psk_password_route(auth, cipher):
    return cipher == "pwd" and auth in {
        "wpa-psk", "wpa2-psk", "sha256-psk"
    }


assert wpa_psk_password_route("wpa-psk", "pwd")
assert wpa_psk_password_route("wpa2-psk", "pwd")
assert wpa_psk_password_route("sha256-psk", "pwd")
assert not wpa_psk_password_route("wpa2-psk", "pmk")
assert not wpa_psk_password_route("pure-sae", "pwd")
assert not wpa_psk_password_route("sae-psk-transition", "pwd")
resume_start = legacy_hidden_assoc.find(
    "const TahoeExternalPmkScanResumeContracts::Facts scanResumeFacts")
resume_end = legacy_hidden_assoc.find("airportItlwmRegDiagRecordAssoc", resume_start)
if resume_start < 0 or resume_end < 0:
    fail("missing bounded WCL scan-resume block")
resume = legacy_hidden_assoc[resume_start:resume_end]
for token in (
        "ieee80211_node_choose_bss",
        "ieee80211_node_join_bss",
        "IEEE80211_S_AUTH",
        "IEEE80211_SEND_MGMT",
        "iwx_auth(",
):
    forbid(resume, token, "unsafe shortcut in WCL scan-resume block")
if resume.count("ieee80211_new_state(ic,") != 1:
    fail("WCL fallback block must contain exactly one normal state request")
require(resume, "tahoeJoinCachedWclCandidate(",
        "reference-aligned cached candidate join")
require(resume, "associationScanOwnersIdle()",
        "physical scan-owner idle fence")
require(resume, "PMK_READY_SCAN_RESUME", "credential-safe local progress marker")

# Preserve the lower-layer semantics that make SCAN->SCAN safe. IWX and the
# actual Tahoe-QEMU legacy IWN backend both coalesce an active scan and restart
# an inactive one; net80211 still holds an empty AUTO_JOIN scan for airportd
# instead of selecting a random BSS. The one exception is an already-issued,
# exact direct-SAE policy: it is the JoinAdapter owner and must select its
# matching refreshed BSS rather than be stranded by the generic hold.
iwx_newstate = body(iwx, "void ItlIwx::\niwx_newstate_task(void *psc)", "IWX newstate task")
ordered(iwx_newstate, "IWX SCAN->SCAN preservation",
        "if (ostate == IEEE80211_S_SCAN)",
        "if (nstate == ostate)",
        "IWX_FLAG_SCANNING", "goto next_scan", "iwx_scan(sc)")
iwn_newstate = body(iwn, "int ItlIwn::\niwn_newstate(", "IWN newstate")
ordered(iwn_newstate, "IWN SCAN->SCAN preservation",
        "if (ic->ic_state == IEEE80211_S_SCAN)",
        "if (nstate == IEEE80211_S_SCAN)",
        "IWN_FLAG_SCANNING", "return 0;",
        "case IEEE80211_S_SCAN:", "iwn_scan(sc,")
end_scan = body(node, "void\nieee80211_end_scan", "net80211 end_scan")
ordered(end_scan, "Apple AUTO_JOIN empty-ESS hold",
        "IEEE80211_F_AUTO_JOIN", "ic->ic_des_esslen == 0", "return;",
        "ieee80211_node_choose_bss")
require(end_scan,
        "ic->ic_des_esslen == 0 &&\n"
        "        !ieee80211_sae_wcl_request_scan_selection_owned(ic)",
        "direct SAE JoinAdapter exception to empty-ESS AUTO_JOIN hold")

# Ordinary builds and the audited PLTI path retain pure-SAE rejection.  The
# resume predicate only consumes the pre-existing exact PSK policy, so the
# lab-gated IWN direct route cannot reopen a PLTI carrier; its PMK-to-RSN
# continuation is local to the driver and cannot enter this resume path.
require(auth, "kAuthWpa3Sae | kAuthWpa2Psk",
        "sole audited WPA3 transition selector")
require(auth, "return (authtypeUpper & kWpa3OnlyAuthMask) != 0 &&",
        "pure WPA3 rejection predicate")
require(auth, "return isAuditedWpa3PskTransition(authtypeUpper) ||",
        "exact existing PMK allow-list")
require(sae_gate, "test_tahoe_wcl_plti_scan_resume_contract.sh",
        "SAE/quarantine aggregate includes WCL resume contract")
require(layer_runner, "test_tahoe_sae_quarantine_contract.sh",
        "isolated Tahoe layer runner includes aggregate contract")

print("WCL PLTI scan-resume contract ok")
PY
