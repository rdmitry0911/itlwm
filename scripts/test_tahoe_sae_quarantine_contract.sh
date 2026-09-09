#!/usr/bin/env bash
# One-pass contract gate for the ordinary pure-SAE quarantine, the narrow
# exact-SAE-password IWN lab ingress exception, and the audited PMF owner.
#
# This intentionally combines semantic mask tests, every association ingress,
# PLTI/Agent PMK boundaries, net80211's Open-System limitation, and the AX211
# PMF transaction owner.  It is a source-and-build admission gate, not a
# claim that a complete WPA3 association is implemented.
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

bash "$root/scripts/test_payload_builders.sh"
bash "$root/scripts/test_net80211_sha256_ptk_kdf_contract.sh"
bash "$root/scripts/test_tahoe_ax211_api68_pmf_transaction_owner_contract.sh"
bash "$root/scripts/test_tahoe_gtk_igtk_slot_guard_contract.sh"
bash "$root/scripts/test_net80211_bip_lifetime_contract.sh"
bash "$root/scripts/test_net80211_retained_igtk_rearm.sh"
bash "$root/scripts/test_apsta_retained_association_epoch.sh"
bash "$root/scripts/test_iwn_scan_dwell_budget.sh"
bash "$root/scripts/test_tahoe_sae_product_foundation_contract.sh"
bash "$root/scripts/test_net80211_pae_epoch_contract.sh"
bash "$root/scripts/test_net80211_public_initial_bssid_pin_contract.sh"
bash "$root/scripts/test_net80211_auth_status_contract.sh"
bash "$root/scripts/test_tahoe_wcl_plti_scan_resume_contract.sh"
bash "$root/scripts/test_tahoe_wcl_wpa_psk_password_pmk_contract.sh"
bash "$root/scripts/test_tahoe_wcl_plti_scan_resume_runtime_evidence_contract.sh"
bash "$root/scripts/test_tahoe_wcl_physical_scan_lifecycle_contract.sh"
bash "$root/scripts/test_tahoe_wcl_physical_scan_trace_contract.sh"
bash "$root/scripts/test_tahoe_wcl_physical_scan_runtime_contract.sh"
bash "$root/scripts/test_tahoe_post_plti_trace_contract.sh"
bash "$root/scripts/test_tahoe_post_plti_trace_runtime_contract.sh"
bash "$root/scripts/test_tahoe_post_plti_trace_runtime_evidence_contract.sh"
bash "$root/scripts/test_tahoe_iwn_direct_sae_runtime_contract.sh"
bash "$root/scripts/test_tahoe_iwn_direct_sae_runtime_evidence_contract.sh"
bash "$root/scripts/test_tahoe_iwn_direct_isae_runtime_artifact_receipt_contract.sh"
bash "$root/scripts/test_tahoe_iwn_direct_isae_runtime_contract.sh"
bash "$root/scripts/test_tahoe_iwn_direct_isae_runtime_evidence_contract.sh"
bash "$root/scripts/test_tahoe_iwn_software_pmf_runtime_contract.sh"
bash "$root/scripts/test_tahoe_iwn_software_pmf_lab_build_contract.sh"
bash "$root/scripts/test_tahoe_iwn_lab_candidate_receipt_contract.sh"
bash "$root/scripts/test_tahoe_iwn_lab_candidate_stage_contract.sh"
bash "$root/scripts/test_tahoe_iwn_lab_loaded_identity_contract.sh"
bash "$root/scripts/test_tahoe_lab_public_recovery_contract.sh"
bash "$root/scripts/test_tahoe_iwn_lab_public_recovery_receipt_contract.sh"
bash "$root/scripts/test_tahoe_iwn_public_recovery_stage_contract.sh"
bash "$root/scripts/test_tahoe_lab_credential_broker_contract.sh"
bash "$root/scripts/test_tahoe_iwn_public_recovery_contract.sh"
bash "$root/scripts/test_tahoe_iwn_software_pmf_contract.sh"
bash "$root/scripts/test_tahoe_iwn_software_pmf_reconnect_contract.sh"
bash "$root/scripts/test_tahoe_iwx_pmf_bip_runtime_contract.sh"
bash "$root/scripts/test_tahoe_prepare_disposable_overlay_contract.sh"
bash "$root/scripts/test_tahoe_launch_disposable_pair_contract.sh"

python3 - "$root" <<'PY'
import json
import re
from pathlib import Path
import sys


root = Path(sys.argv[1])
auth = (root / "AirportItlwm/TahoeAssociationAuthContracts.hpp").read_text()
sky = (root / "AirportItlwm/AirportItlwmSkywalkInterface.cpp").read_text()
direct_sae_gate = (root / "AirportItlwm/IwnDirectSaeLabGate.hpp").read_text()
v2 = (root / "AirportItlwm/AirportItlwmV2.cpp").read_text()
legacy = (root / "AirportItlwm/AirportItlwm.cpp").read_text()
legacy_ioctl = (root / "AirportItlwm/AirportSTAIOCTL.cpp").read_text()
agent_header = (root / "AirportItlwmAgent/src/assoc_target.h").read_text()
agent = (root / "AirportItlwmAgent/src/main.m").read_text()
output = (root / "itl80211/openbsd/net80211/ieee80211_output.c").read_text()
input_source = (root / "itl80211/openbsd/net80211/ieee80211_input.c").read_text()
crypto = (root / "itl80211/openbsd/net80211/ieee80211_crypto.h").read_text()
crypto_source = (root / "itl80211/openbsd/net80211/ieee80211_crypto.c").read_text()
raw_ioctl = (root / "itl80211/openbsd/net80211/ieee80211_ioctl.c").read_text()
regdiag_header = (root / "include/ClientKit/AirportItlwmRegDiag.h").read_text()
regdiag_client = (root / "AirportItlwmRegDiag/airport_itlwm_regdiag.c").read_text()
capture_script = (root / "scripts/capture_tahoe_sae_layer.sh").read_text()
capture_evaluator = (root / "scripts/evaluate_tahoe_sae_capture.py").read_text()
profile_runner = (root / "scripts/run_tahoe_sae_lab_profiles.sh").read_text()
layer_runner = (root / "scripts/run_tahoe_sae_quarantine_layer.sh").read_text()
copyout_record = (root / "analysis/TAHOE_SAE_COPYOUT_EVIDENCE_CORRELATION_2026-07-21.md").read_text()


def fail(message):
    raise SystemExit(f"SAE quarantine contract: {message}")


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


# Mask model: exact 0x1008 is the one deliberately permitted transition
# carrier.  The regular product, public ingress, legacy ingress, IWX, and
# every PLTI/Agent PMK carrier reject a pure SAE vector before legacy auth or
# PBKDF2.  One separately compiled IWN-only WCL branch below is deliberately
# outside those carriers and owns its PMK-to-RSN continuation wholly inside
# the lab-gated driver path.
for needle in (
    "kAuthWpa3Sae = 1U << 12",
    "kAuthWpa2Psk = 1U << 3",
    "kAuditedWpa3PskTransitionAuth =",
    "kAuthWpa3Sae | kAuthWpa2Psk",
    "inline bool requiresUnsupportedWpa3Auth",
    "inline bool isAuditedPskPmkAuth",
    "inline bool mayUseLocalPskPmk",
    "inline bool mayUseDirectSaeWclCredential",
    "inline uint32_t localPskAkmSelectionMaskForDirectWclPmk",
    "return authtypeUpper == kAuditedWpa3PskTransitionAuth;",
):
    require(auth, needle, "strict WPA3 mask model")

unsupported = body(auth, "inline bool requiresUnsupportedWpa3Auth",
                   "requiresUnsupportedWpa3Auth")
ordered(unsupported, "unsupported WPA3 predicate",
        "authtypeUpper & kWpa3OnlyAuthMask", "!isAuditedWpa3PskTransition")
pmk_policy = body(auth, "inline bool mayUseLocalPskPmk",
                  "mayUseLocalPskPmk")
ordered(pmk_policy, "PLTI PMK policy",
        "return isAuditedWpa3PskTransition(authtypeUpper)",
        "isAuditedPskPmkAuth(authtypeUpper)")
audited_psk = body(auth, "inline bool isAuditedPskPmkAuth",
                   "exact PLTI PSK allow-list")
ordered(audited_psk, "exact PLTI PSK allow-list",
        "authtypeUpper != 0", "~kPskAuthMask")
forbid(audited_psk, "usesLocalPskAkm(",
       "broad PSK authorization in exact PLTI allow-list")
direct_wcl_selection = body(
    auth, "inline uint32_t localPskAkmSelectionMaskForDirectWclPmk",
    "exact direct-WCL SHA256 PSK selection compatibility")
ordered(direct_wcl_selection, "bounded direct-WCL selector compatibility",
        "directWclPmkCarrier", "authtypeUpper == kAuthSha256Psk",
        "return kAuthWpa2Psk | kAuthSha256Psk;",
        "return authtypeUpper & kPskAuthMask;")

# The generic Skywalk/public and legacy Tahoe routes must reject before any
# association state or RSN mutation.  The WCL handler contains one separately
# preprocessor-gated IWN exception, which is checked as a self-contained
# direct carrier below; its ordinary tail must keep the same reject ordering.
sky_assoc = body(sky, "IOReturn AirportItlwmSkywalkInterface::associateSSID",
                 "Skywalk associateSSID")
ordered(sky_assoc, "Skywalk associate ingress",
        "requiresUnsupportedWpa3Auth", "return kIOReturnUnsupported;",
        "fHalService->get80211Controller()", "ieee80211_disable_rsn",
        "publishPendingAssocTarget")
ordered(sky_assoc, "Skywalk exact PSK AKM mapping",
        "localPskAkmSelectionMaskForDirectWclPmk", "usesLocalLegacyPskAkm",
        "IEEE80211_WPA_AKM_PSK", "usesLocalSha256PskAkm",
        "IEEE80211_WPA_AKM_SHA256_PSK")
forbid(sky_assoc, "IEEE80211_WPA_AKM_PSK | IEEE80211_WPA_AKM_SHA256_PSK",
       "implicit SHA256-PSK in Skywalk association")
for token in ("IEEE80211_AUTH_ALG_OPEN",):
    if token in sky_assoc:
        fail(f"Skywalk association directly contains unsafe token {token}")

public_assoc = body(sky, "IOReturn AirportItlwmSkywalkInterface::\nsetASSOCIATE",
                    "public setASSOCIATE")
ordered(public_assoc, "public association ingress",
        "requiresUnsupportedWpa3Auth", "kIOReturnUnsupported",
        "if (ic->ic_state < IEEE80211_S_SCAN)", "setAUTH_TYPE",
        "assocResult = associateSSID")
require(public_assoc, "return assocResult;", "public association error propagation")
require(public_assoc, "true, false, false, nullptr",
        "public association cannot enable direct-WCL compatibility")

hidden_assoc = body(sky,
                    "IOReturn AirportItlwmSkywalkInterface::\nsetWCL_ASSOCIATEImpl",
                    "hidden setWCL_ASSOCIATEImpl")
direct_marker = ("#if AIRPORT_ITLWM_IWN_SAE_WCL_INGRESS\n"
                 "    /*\n"
                 "     * The live ingress")
direct_lab = preprocessor_block(hidden_assoc, direct_marker,
                                "IWN lab exact-SAE WCL block")
direct_transaction = body(
    sky,
    "IOReturn AirportItlwmSkywalkInterface::\nstartIwnDirectSaeCredential",
    "common IWN direct-SAE transaction")
for token in (
    "ITL_SAE_DRIVER_CRYPTO_AVAILABLE",
    "#define AIRPORT_ITLWM_IWN_SAE_WCL_INGRESS 1",
    "defined(IWN_SOFTWARE_PMF_LAB_BUILD)",
    "#define AIRPORT_ITLWM_IWN_DIRECT_SAE_LAB_STIMULUS 1",
    "#define AIRPORT_ITLWM_IWN_DIRECT_SAE_LAB_STIMULUS 0",
    "AIRPORT_ITLWM_IWN_DIRECT_SAE_LAB_STIMULUS",
):
    require(direct_sae_gate, token, "IWN product/diagnostic compile gates")
require(sky, '#include "IwnDirectSaeLabGate.hpp"',
        "Skywalk direct-SAE lab gate include")
require(sky, "#if AIRPORT_ITLWM_IWN_SAE_WCL_INGRESS",
        "Skywalk direct-SAE lab gate use")
require_re(
    hidden_assoc,
    r"const bool directSaeWclPassword\s*=\s*"
    r"wcl_key_cipher\s*==\s*APPLE80211_CIPHER_PWD\s*&&\s*"
    r"TahoeAssociationAuthContracts::mayUseDirectSaeWclCredential\(\s*"
    r"auth_upper\s*\)\s*;",
    "exact CIPHER_PWD-and-auth IWN selector")
for token in (
    "if (directSaeWclPassword)",
    "AirportItlwmIwnDirectSaeCredentialProvenance::WclCandidate",
    "directRequest.wclOwner = &owner",
    "startIwnDirectSaeCredential(&directRequest, nullptr,",
):
    require(direct_lab, token, "exact-SAE direct IWN ingress")
ordered(direct_lab, "IWN direct exact-SAE delegation",
        "if (directSaeWclPassword)",
        "AirportItlwmIwnDirectSaeCredentialProvenance::WclCandidate",
        "startIwnDirectSaeCredential(&directRequest, nullptr,")
for token in (
    "OSDynamicCast(ItlIwn, fHalService)",
    "request->provenance",
    "clearExternalPmkEligibilityLocked(",
    "ieee80211_sae_wcl_request_begin",
    "stageSaeWclCredential",
    "setAUTH_TYPE(&authType)",
    "ieee80211_sae_wcl_request_resume_scan",
):
    require(direct_transaction, token, "common exact-SAE IWN transaction")
ordered(direct_transaction, "IWN direct exact-SAE transaction ordering",
        "clearExternalPmkEligibilityLocked(",
        "ieee80211_sae_wcl_request_begin", "stageSaeWclCredential",
        "setAUTH_TYPE(&authType)", "ieee80211_sae_wcl_request_resume_scan")
for token in (
    "kAuditedWpa3PskTransitionAuth",
    "TahoeAssociationAuthContracts::mayUseLocalPskPmk",
    "publishPendingAssocTarget(",
    "waitForExternalPmkReady",
    "assocResult = associateSSID",
    "installExternalPmkLocked",
):
    forbid(direct_lab, token, "PLTI/legacy association reuse in IWN ingress")
    forbid(direct_transaction, token,
           "PLTI/legacy association reuse in IWN transaction")
legacy_start = hidden_assoc.find(
    "if (TahoeAssociationAuthContracts::requiresUnsupportedWpa3Auth(")
if legacy_start < 0:
    fail("missing ordinary hidden pure-SAE rejection after lab gate")
legacy_hidden_assoc = hidden_assoc[legacy_start:]
ordered(legacy_hidden_assoc, "ordinary hidden association ingress",
        "requiresUnsupportedWpa3Auth", "kIOReturnUnsupported",
        "const bool directWclPmk =",
        "const bool directWclPmkSha256PskCompatibility =",
        "setAUTH_TYPE",
        "assocResult = associateSSID")
require(legacy_hidden_assoc, "return assocResult;",
        "ordinary hidden association error propagation")
require(legacy_hidden_assoc,
        "directWclPmkSha256PskCompatibility,\n"
        "                &externalPmkReadyObserved",
        "hidden association passes only its exact direct-PMK compatibility bit")


def direct_sae_password_route(auth, cipher):
    return cipher == "pwd" and auth in {"pure-sae", "sae-psk-transition"}


assert direct_sae_password_route("pure-sae", "pwd")
assert direct_sae_password_route("sae-psk-transition", "pwd")
assert not direct_sae_password_route("sae-psk-transition", "pmk")
assert not direct_sae_password_route("wpa2-psk", "pwd")

legacy_assoc = body(legacy, "IOReturn AirportItlwm::associateSSID",
                    "legacy associateSSID")
ordered(legacy_assoc, "legacy association ingress",
        "requiresUnsupportedWpa3Auth", "return kIOReturnUnsupported;",
        "fHalService->get80211Controller()", "ieee80211_disable_rsn")
ordered(legacy_assoc, "legacy exact PSK AKM mapping",
        "usesLocalPskAkm", "usesLocalLegacyPskAkm",
        "IEEE80211_WPA_AKM_PSK", "usesLocalSha256PskAkm",
        "IEEE80211_WPA_AKM_SHA256_PSK")
forbid(legacy_assoc, "IEEE80211_WPA_AKM_PSK | IEEE80211_WPA_AKM_SHA256_PSK",
       "implicit SHA256-PSK in legacy association")
legacy_public = body(legacy_ioctl, "IOReturn AirportItlwm::\nsetASSOCIATE",
                     "legacy setASSOCIATE")
ordered(legacy_public, "legacy public association ingress",
        "requiresUnsupportedWpa3Auth", "return kIOReturnUnsupported;",
        "if (ic->ic_state < IEEE80211_S_SCAN)", "return associateSSID")

# The project-owned PMK carrier cannot publish, consume, or derive a PMK for
# an unapproved WPA3 vector. This makes stale Keychain contents irrelevant to
# pure SAE: the keychain lookup is never reached.
publish_action = body(v2, "static IOReturn\nairportItlwmPublishAssocAction",
                      "PLTI publish action")
ordered(publish_action, "PLTI publish action",
        "mayUseLocalPskPmk", "a->out_generation = 0",
        "s->fAssocGenCounter += 1")
deliver_action = body(v2, "static IOReturn\nairportItlwmDeliverPmkAction",
                      "PLTI deliver action")
ordered(deliver_action, "PLTI deliver action",
        "mayUseLocalPskPmk", "a->rc = kIOReturnNotPermitted",
        "memcpy(ic->ic_psk")
publish_api = body(v2, "uint64_t AirportItlwm::\npublishPendingAssocTarget",
                   "PLTI publish API")
ordered(publish_api, "PLTI publish API",
        "mayUseLocalPskPmk", "return 0;", "IOCommandGate *gate")
for segment, label in ((publish_action, "publish action"),
                       (publish_api, "publish API")):
    forbid(segment, "usesLocalPskAkm(", f"broad PSK bypass in {label}")
ordered(deliver_action, "PLTI exact PSK AKM mapping",
        "localAuthMaskWithoutFallbackRewrite", "usesLocalPskAkm",
        "usesLocalLegacyPskAkm", "IEEE80211_WPA_AKM_PSK",
        "usesLocalSha256PskAkm", "IEEE80211_WPA_AKM_SHA256_PSK",
        "ieee80211_ioctl_setwpaparms")
forbid(deliver_action, "IEEE80211_WPA_AKM_PSK | IEEE80211_WPA_AKM_SHA256_PSK",
       "implicit SHA256-PSK in PLTI delivery")
pmk_ingress = body(sky, "IOReturn AirportItlwmSkywalkInterface::\ninstallExternalPmkLocked",
                   "CIPHER_KEY/CUR_PMK ingress")
ordered(pmk_ingress, "direct PMK exact PSK AKM mapping",
        "requiresUnsupportedWpa3Auth", "memcpy(ic->ic_psk",
        "localAuthMaskWithoutFallbackRewrite", "usesLocalLegacyPskAkm",
        "IEEE80211_WPA_AKM_PSK", "usesLocalSha256PskAkm",
        "IEEE80211_WPA_AKM_SHA256_PSK", "ieee80211_ioctl_setwpaparms")
forbid(pmk_ingress, "IEEE80211_WPA_AKM_PSK | IEEE80211_WPA_AKM_SHA256_PSK",
       "implicit SHA256-PSK in direct PMK ingress")
forbid(pmk_ingress, "localPskAkmSelectionMaskForDirectWclPmk",
       "shared PMK ingress must remain independent of the direct-WCL selector")
forbid(pmk_ingress, "directWclPmkSha256PskCompatibility",
       "shared PMK ingress must not inherit a WCL-only compatibility bit")
require(pmk_ingress, "CIPHER_KEY/CUR_PMK may arrive before WCL_ASSOCIATE",
        "PMK-before-WCL ordering boundary")
cipher_key = body(sky, "setCIPHER_KEY(struct apple80211_key *key)",
                  "CIPHER_KEY PMK caller")
require(cipher_key, "current_authtype_upper,\n                                            \"CIPHER_KEY\"",
        "CIPHER_KEY selector passed to PMK ingress")
require(cipher_key, "current_authtype_upper,\n                                                \"CIPHER_KEY_MSK\"",
        "CIPHER_KEY MSK selector passed to PMK ingress")
cur_pmk = body(sky, "setCUR_PMK(struct apple80211_pmk *pmk)",
               "CUR_PMK caller")
require(cur_pmk, "current_authtype_upper,\n                                    \"CUR_PMK\"",
        "CUR_PMK selector passed to PMK ingress")

# The OpenBSD raw ioctl backend is compiled for the device, but Tahoe's
# Skywalk BSD bridge must never leave an untyped mutable ESS/AKM/PMK carrier
# to its opaque superclass fallback.  Explicitly reject the state
# setters before the Apple80211 wrapper route is even considered.
bsd_dispatch = body(sky, "IOReturn AirportItlwmSkywalkInterface::\nprocessBSDCommand",
                    "Skywalk BSD dispatcher")
ordered(bsd_dispatch, "raw net80211 association quarantine",
        "case SIOCS80211NWID:", "case SIOCS80211JOIN:",
        "case SIOCS80211NWKEY:", "case SIOCS80211WPAPARMS:",
        "case SIOCS80211WPAPSK:", "case SIOCS80211KEYAVAIL:",
        "case SIOCS80211KEYRUN:", "case SIOCS80211BSSID:",
        "case SIOCS80211CHANNEL:",
        "REJECT_RAW_NET80211_ASSOC", "return kIOReturnUnsupported;",
        "if ((isApple80211GetIoctl(cmd) || isApple80211SetIoctl(cmd))")
for token in ("case SIOCS80211NWID:", "case SIOCS80211JOIN:",
              "case SIOCS80211NWKEY:", "case SIOCS80211WPAPARMS:",
              "case SIOCS80211WPAPSK:", "case SIOCS80211KEYAVAIL:",
              "case SIOCS80211KEYRUN:", "case SIOCS80211BSSID:",
              "case SIOCS80211CHANNEL:"):
    require(raw_ioctl, token, "raw net80211 setter inventory")
raw_backend_fence = body(raw_ioctl, "ieee80211_tahoe_raw_assoc_mutation",
                         "Tahoe raw net80211 backend fence")
for token in ("case SIOCS80211NWID:", "case SIOCS80211JOIN:",
              "case SIOCS80211NWKEY:", "case SIOCS80211WPAPARMS:",
              "case SIOCS80211WPAPSK:", "case SIOCS80211KEYAVAIL:",
              "case SIOCS80211KEYRUN:", "case SIOCS80211BSSID:",
              "case SIOCS80211CHANNEL:"):
    require(raw_backend_fence, token, "Tahoe raw backend fence setter")
require(raw_backend_fence, "return 1;", "Tahoe raw backend fence reject")
raw_backend_dispatch = body(raw_ioctl,
                            "ieee80211_ioctl(struct _ifnet *ifp, u_long cmd, caddr_t data)",
                            "raw net80211 dispatcher")
ordered(raw_backend_dispatch, "raw net80211 backend dispatch quarantine",
        "ieee80211_tahoe_raw_assoc_mutation(cmd)", "return EOPNOTSUPP;",
        "switch (cmd)")

for needle in (
    "kAirportItlwmAuthSha256Psk",
    "kAirportItlwmAuthWpa3Mask",
    "kAirportItlwmAuthAuditedWpa3PskTransition",
    "kAirportItlwmAuthWpa3Sae | kAirportItlwmAuthWpa2Psk",
    "AirportItlwmAgentTargetUsesPskPmk",
    "~kAirportItlwmAuthPskPmkMask",
):
    require(agent_header, needle, "Agent mirrored auth policy")
agent_policy = body(agent, "agent_target_uses_psk_pmk",
                    "Agent target policy")
ordered(agent_policy, "Agent target policy",
        "return AirportItlwmAgentTargetUsesPskPmk(tgt->authtype_upper)")
agent_handler = body(agent, "agent_handle_target", "Agent target handler")
ordered(agent_handler, "Agent credential boundary",
        "!agent_target_uses_psk_pmk(tgt)", "return -1;",
        "AgentLookupProjectPSK", "AgentDerivePMK_PBKDF2")

# A single runtime association capture must expose the input auth/PMF carrier,
# direct PMK order, PLTI generation handoff, lifecycle clears, and EAPOL-only
# traffic without recording key material or requiring route changes.
for needle in (
    "AIRPORT_ITLWM_REGDIAG_ABI_VERSION 2U",
    "kAirportItlwmRegDiagModePmk",
    "kAirportItlwmRegDiagTraceAuthPolicy",
    "kAirportItlwmRegDiagTracePmkIngress",
    "kAirportItlwmRegDiagTracePmkClear",
    "kAirportItlwmRegDiagTracePltiPublish",
    "kAirportItlwmRegDiagTracePltiDeliver",
    "kAirportItlwmRegDiagPathPmk",
    "kAirportItlwmRegDiagPathPlti",
    "kAirportItlwmRegDiagPathLifecycle",
    "lastAssocPmfCapability",
    "lastPmkGeneration",
):
    require(regdiag_header, needle, "SAE/PMK RegDiag ABI")
for needle in (
    "airportItlwmRegDiagRecordAssocPolicy",
    "airportItlwmRegDiagRecordPmkIngress",
    "airportItlwmRegDiagRecordPmkClear",
):
    require(sky, needle, "Skywalk SAE/PMK timeline hook")
for needle in (
    "airportItlwmRegDiagRecordPlti",
    "kAirportItlwmRegDiagTracePltiPublish",
    "kAirportItlwmRegDiagTracePltiDeliver",
):
    require(v2, needle, "PLTI SAE/PMK timeline hook")
packet_trace_policy = body(v2, "airportItlwmRegDiagShouldTracePacket",
                           "PMK diagnostic packet trace policy")
ordered(packet_trace_policy, "PMK diagnostic EAPOL-only data filter",
        "kAirportItlwmRegDiagModeData", "eapol",
        "kAirportItlwmRegDiagModePmk")
require(sky, "airportItlwmRegDiagShouldTracePacket(isEapol)",
        "Skywalk packet trace obeys PMK EAPOL-only filter")
for needle in (
    "sae-on",
    "get snapshot|trace|control|report",
    "pmk_source_name",
    "pmk_decision_name",
    " eapol=%d length=",
    " link_state=%d raw_code=",
):
    require(regdiag_client, needle, "RegDiag SAE/PMK report client")
for needle in (
    "routing_mutation=none",
    "ip_address_mutation=none",
    '"$@" >/dev/null 2>&1',
    '"$TOOL" sae-on',
    '"$TOOL" get report',
    '"$EVALUATOR" --expect',
    'netstat -rn -f inet',
    'mktemp -d "$OUT_ROOT/sae-layer-$STAMP.XXXXXX"',
):
    require(capture_script, needle, "one-run SAE capture safety contract")
forbid(capture_script, "networksetup", "hard-coded network mutation in capture")
forbid(capture_script, "route ", "route mutation in capture")
for needle in (
    "MODE_SAE_PMK = 0x35",
    "TRACE_CAPACITY = 128",
    "post-snapshot.txt",
    "trace header count does not match decoded records",
    "trace sequence is not a contiguous, unique ring window",
    "PMK-mode trace contains non-EAPOL",
    "snapshot packet counters do not reconcile",
    "event.auth_upper == PURE_SAE",
    "event.generation in published",
    "no successful link-up publication",
    "association_attempt_window",
    "correlated_psk_success_stages",
    "wpa2-prior-policy-progress",
    "wpa2-policy-result",
    "wpa2-mismatched-plti-auth",
):
    require(capture_evaluator, needle, "strict SAE/PMK capture evaluator")
forbid(capture_evaluator, 'read_optional(directory / "report.txt")',
       "report.txt fallback in canonical evaluator")
for needle in (
    "password_carrier=keychain-only",
    "diagnostic_epoch=sae-on-clear-per-attempt",
    "run_epoch wpa2-psk-baseline wpa2-psk",
    "run_epoch pure-sae-required-pmf-reject pure-sae-required-pmf-reject",
    "run_epoch sae-transition-psk sae-transition-psk",
    "run_epoch wpa2-psk-recovery wpa2-psk",
    "--strict",
    "-- \"$NETWORKSETUP_TOOL\" -setairportnetwork",
):
    require(profile_runner, needle, "four-epoch SAE/PMF lab runner")
forbid(profile_runner, "PASSWORD=", "password carrier in SAE lab runner")
forbid(profile_runner, "--password", "password command line in SAE lab runner")
for needle in (
    "./scripts/build_tahoe_lab_public_recovery.sh",
    "./scripts/build_regdiag.sh",
):
    require(layer_runner, needle,
            "layer gate builds the matching recovery/RegDiag client")
for needle in ("git -C \"$ROOT\" diff --cached --quiet",
               "git -C \"$ROOT\" diff --cached --binary",
               "git -C \"$ROOT\" ls-files --others --exclude-standard"):
    require(layer_runner, needle,
            "layer gate source identity includes staged changes")

# Successful synthetic scenarios are versioned with their source changes, so
# a later layer cannot silently reclassify test-only or quarantine evidence as
# a functional pure-SAE result.
for needle in (
    "exact current-epoch copyout", "wpa2-prior-policy-progress",
    "pure-sae-required-pmf-reject", "no production consumer",
    "not a functional pure-SAE association", "Raw captures",
):
    require(copyout_record, needle, "copyout/evaluator test record")
forbidden_record_claims = (
    "working pure-SAE connection passed",
    "functional pure-SAE association passed",
    "PMF key lifecycle passed",
)
for needle in forbidden_record_claims:
    forbid(copyout_record, needle, "overbroad copyout/evaluator test claim")

# The remote build runner is confined to the fixed QEMU laboratory guest.
# It creates a private known_hosts file from a source-controlled literal pin;
# neither the caller's SSH config nor mutable ambient known-host files can
# redirect the session or silently accept a replacement key.  The same strict
# transport settings must cover the separate rsync client as well as command
# execution over SSH.
for needle in (
    'PINNED_GUEST="devops@127.0.0.1"',
    'PINNED_PORT=3322',
    'PINNED_REMOTE_SDK="/Users/devops/Projects/itlwm/MacKernelSDK"',
    'PINNED_BOOTKC="/System/Library/KernelCollections/BootKernelExtensions.kc"',
    'EXPECTED_GUEST_BUILD="25C56"',
    'EXPECTED_BOOTKC_SHA256="eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d"',
    'EXPECTED_GUEST_HOSTKEY_LINE=',
    'EXPECTED_GUEST_HOSTKEY_SHA256=',
    'KNOWN_HOSTS="$(mktemp /tmp/aiam-tahoe-sae-gate-known-hosts.XXXXXX)"',
    'chmod 600 "$KNOWN_HOSTS"',
    'ssh-keygen -lf "$KNOWN_HOSTS" -E sha256',
    'ERROR: Tahoe SAE gate only accepts the pinned laboratory guest and paths',
    'trap cleanup_known_hosts EXIT',
    'RSYNC_RSH=',
):
    require(layer_runner, needle, "pinned layer-gate transport")
transport_prepare_body = body(layer_runner, "prepare_guest_transport()",
                              "pinned layer-gate transport preparation")
ordered(transport_prepare_body, "pinned destination and path rejection",
        '"$REMOTE" != "$PINNED_GUEST"',
        '"$PORT" != "$PINNED_PORT"',
        '"$REMOTE_SDK" != "$PINNED_REMOTE_SDK"',
        '"$BOOTKC" != "$PINNED_BOOTKC"',
        'ERROR: Tahoe SAE gate only accepts the pinned laboratory guest and paths',
        'exit 2',
        'KNOWN_HOSTS="$(mktemp /tmp/aiam-tahoe-sae-gate-known-hosts.XXXXXX)"',
        'ssh-keygen -lf "$KNOWN_HOSTS" -E sha256',
        'SSH=(')
for needle in (
    'ssh -F /dev/null -o BatchMode=yes -o ConnectTimeout=8',
    'StrictHostKeyChecking=yes',
    'UserKnownHostsFile="$KNOWN_HOSTS"',
    'GlobalKnownHostsFile=/dev/null',
    'UpdateHostKeys=no',
    'LogLevel=ERROR',
):
    require(transport_prepare_body, needle, "strict SSH transport")
provenance_body = body(layer_runner, "assert_pinned_guest_provenance()",
                       "pinned Tahoe guest provenance")
ordered(provenance_body, "pinned Tahoe guest provenance checks",
        'sw_vers -buildVersion',
        "shasum -a 256 '$BOOTKC'",
        '"$observed_build" != "$EXPECTED_GUEST_BUILD"',
        'ERROR: pinned Tahoe guest build mismatch',
        '"$observed_bootkc" != "$EXPECTED_BOOTKC_SHA256"',
        'ERROR: pinned Tahoe guest BootKC digest mismatch')
rsync_rsh_line = next((line for line in layer_runner.splitlines()
                       if line.startswith("RSYNC_RSH=")), "")
if not rsync_rsh_line:
    fail("missing strict rsync transport command")
for needle in (
    'ssh -F /dev/null -o BatchMode=yes -o ConnectTimeout=8',
    'StrictHostKeyChecking=yes',
    'UserKnownHostsFile=$KNOWN_HOSTS',
    'GlobalKnownHostsFile=/dev/null',
    'UpdateHostKeys=no',
    'LogLevel=ERROR',
    '-p $PORT',
):
    require(rsync_rsh_line, needle, "strict rsync transport")
require(layer_runner, 'rsync -a -e "$RSYNC_RSH"',
        "rsync uses its strict transport command")
for needle in (
    'StrictHostKeyChecking=no',
    'UserKnownHostsFile=/dev/null',
    'ssh-keyscan',
    'accept-new',
):
    forbid(layer_runner, needle, "weak layer-gate transport")
static_exit = layer_runner.find('if [ "$STATIC_ONLY" -eq 1 ]; then')
untracked_gate = layer_runner.find('UNTRACKED_FILES="$(git -C "$ROOT"')
if static_exit < 0 or untracked_gate < 0 or static_exit > untracked_gate:
    fail("static-only layer gate must complete before the remote cleanliness gate")
transport_cleanup = layer_runner.find('trap cleanup_known_hosts EXIT', static_exit)
transport_prepare = layer_runner.find('\nprepare_guest_transport\n', static_exit)
provenance_check = layer_runner.find('\nassert_pinned_guest_provenance\n', static_exit)
if (transport_cleanup < 0 or transport_prepare < 0 or provenance_check < 0 or
        transport_cleanup > transport_prepare or transport_prepare > provenance_check or
        provenance_check > untracked_gate):
    fail("layer gate must verify pinned transport and Tahoe provenance before remote staging")
transport_evidence = json.loads((root / "evidence/runtime/"
                                 "tahoe_sae_quarantine_transport_pin_4945db1.json").read_text())
transport_doc = (root / "analysis/"
                 "TAHOE_SAE_QUARANTINE_TRANSPORT_PIN_2026-07-20.md").read_text()
if transport_evidence.get("schema_version") != "itlwm-tahoe-sae-quarantine-transport-pin/v1":
    fail("transport evidence schema mismatch")
provenance = transport_evidence.get("provenance", {})
expected_provenance = {
    "source_commit": "4945db13844b46da68769ef0ff66d272e5db79fa",
    "guest_os_build": "25C56",
    "bootkc_sha256": "eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d",
    "guest_ssh_hostkey_sha256": "SHA256:4Q/9OkSwSE09YhXRdAbdbPl7WTqRNJHyn+vAM6p8QiY",
}
if provenance != expected_provenance:
    fail("transport evidence provenance mismatch")
for section, key in (
    ("build_admission", "static_contracts_passed"),
    ("build_admission", "kext_debug_build_passed"),
    ("build_admission", "agent_clean_build_passed"),
    ("build_admission", "regdiag_build_passed"),
    ("transport", "ambient_ssh_config_ignored"),
    ("transport", "ephemeral_private_known_hosts"),
    ("transport", "ssh_strict_hostkey_check"),
    ("transport", "rsync_strict_hostkey_check"),
    ("verdict", "transport_pinned"),
    ("verdict", "guest_provenance_pinned"),
    ("verdict", "build_admission_passed"),
):
    if transport_evidence.get(section, {}).get(key) is not True:
        fail(f"transport evidence missing success assertion: {section}.{key}")
if transport_evidence.get("build_admission", {}).get("all_undefined_symbols_resolved") != 958:
    fail("transport evidence unresolved-symbol count mismatch")
for key, value in transport_evidence.get("non_claims", {}).items():
    if value is not False:
        fail(f"transport evidence broadens execution scope: {key}")
for needle in (
    "Successful, bounded scenario",
    "all 958 undefined symbols",
    "It is consequently not a\nfunctional Wi-Fi, SAE, PMF, association, or data-path pass claim",
    "credential-free record",
):
    require(transport_doc, needle, "transport-pinned gate evidence document")

# The historic generic crypto carrier remains PSK-only.  Passive BSS discovery
# recognizes the RSN SAE suite and an exact selected-BSS/controller admission
# may carry one bounded peer Authentication value, but neither the controller
# relay nor IWX gets an active SAE/PMK/PMF path from the IWN-only lab ingress.
require(crypto, "IEEE80211_AKM_SAE", "passive net80211 SAE AKM taxonomy")
require(crypto_source, "ic->ic_rsnakms = IEEE80211_AKM_PSK;",
        "PSK-only active net80211 configuration")
forbid(crypto_source, "IEEE80211_AKM_SAE",
       "active net80211 SAE configuration")
peer_rx = body(input_source, "static int\nieee80211_recv_sae_peer_auth",
               "bounded net80211 SAE peer RX")
for token in ("ieee80211_pae_selected_bss_copyout_current",
              "ieee80211_sae_peer_rx_snapshot_admission",
              "kItlSaeAuthTransportPeerWireTransactionCommit",
              "kItlSaeAuthTransportPeerWireTransactionConfirm",
              "mbuf_copydata", "IEEE80211_EVT_SAE_AUTH_PEER"):
    require(peer_rx, token, "bounded SAE peer RX identity/copy fence")
for token in ("getCommandGate", "runAction", "commandSleep", "ic_psk",
              "IEEE80211_AKM_SAE", "ieee80211_new_state"):
    forbid(peer_rx, token, "peer RX association/key side effect")
if input_source.count("IEEE80211_AUTH_ALG_SAE") != 1:
    fail("only bounded peer RX may parse net80211 Algorithm 3")
auth_tx = body(output, "mbuf_t\nieee80211_get_auth", "net80211 auth TX")
require(auth_tx, "LE_WRITE_2(frm, IEEE80211_AUTH_ALG_OPEN)",
        "Open-System-only auth TX")
auth_rx = body(input_source, "void\nieee80211_recv_auth", "net80211 auth RX")
require(auth_rx, "if (algo != IEEE80211_AUTH_ALG_OPEN)",
        "generic Open-System auth RX fallback")

print("PASS: Tahoe ordinary pure-SAE quarantine, narrow IWN lab ingress, and audited PMF-owner contracts")
PY

python3 "$root/scripts/evaluate_tahoe_sae_capture.py" --self-test
bash -n "$root/scripts/run_tahoe_sae_lab_profiles.sh"
bash "$root/scripts/test_tahoe_sae_lab_scenario_contract.sh"
