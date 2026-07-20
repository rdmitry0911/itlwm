#!/usr/bin/env bash
# One-pass contract gate for the complete current SAE/PMF quarantine layer.
#
# This intentionally combines semantic mask tests, every association ingress,
# PLTI/Agent PMK boundaries, net80211's Open-System limitation, and the AX211
# MFP runtime quarantine.  It is a source-and-build admission gate, not a
# claim that SAE itself is implemented.
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

bash "$root/scripts/test_payload_builders.sh"
bash "$root/scripts/test_net80211_mfp_lifecycle_contract.sh"
bash "$root/scripts/test_tahoe_sae_product_foundation_contract.sh"
bash "$root/scripts/test_net80211_pae_epoch_contract.sh"
bash "$root/scripts/test_net80211_auth_status_contract.sh"

python3 - "$root" <<'PY'
from pathlib import Path
import sys


root = Path(sys.argv[1])
auth = (root / "AirportItlwm/TahoeAssociationAuthContracts.hpp").read_text()
sky = (root / "AirportItlwm/AirportItlwmSkywalkInterface.cpp").read_text()
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


def require(text, needle, label):
    if needle not in text:
        fail(f"missing {label}: {needle}")


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
# carrier. A pure SAE carrier and every other WPA3-containing vector fail
# closed before reaching legacy auth or the PBKDF2 PMK carrier.
for needle in (
    "kAuthWpa3Sae = 1U << 12",
    "kAuthWpa2Psk = 1U << 3",
    "kAuditedWpa3PskTransitionAuth =",
    "kAuthWpa3Sae | kAuthWpa2Psk",
    "inline bool requiresUnsupportedWpa3Auth",
    "inline bool isAuditedPskPmkAuth",
    "inline bool mayUseLocalPskPmk",
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

# Both Tahoe ingress routes must reject before any association state or RSN
# mutation. The legacy route is kept in the same gate so a future target
# switch cannot re-open the unsafe path unnoticed.
sky_assoc = body(sky, "IOReturn AirportItlwmSkywalkInterface::associateSSID",
                 "Skywalk associateSSID")
ordered(sky_assoc, "Skywalk associate ingress",
        "requiresUnsupportedWpa3Auth", "return kIOReturnUnsupported;",
        "fHalService->get80211Controller()", "ieee80211_disable_rsn",
        "publishPendingAssocTarget")
ordered(sky_assoc, "Skywalk exact PSK AKM mapping",
        "usesLocalPskAkm", "usesLocalLegacyPskAkm",
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

hidden_assoc = body(sky,
                    "IOReturn AirportItlwmSkywalkInterface::\nsetWCL_ASSOCIATEImpl",
                    "hidden setWCL_ASSOCIATEImpl")
ordered(hidden_assoc, "hidden association ingress",
        "requiresUnsupportedWpa3Auth", "kIOReturnUnsupported",
        "auto &associationOwner", "setAUTH_TYPE",
        "assocResult = associateSSID")
require(hidden_assoc, "return assocResult;", "hidden association error propagation")

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
require(layer_runner, "./scripts/build_regdiag.sh",
        "layer gate builds the matching RegDiag client")
for needle in ("git -C \"$ROOT\" diff --cached --quiet",
               "git -C \"$ROOT\" diff --cached --binary",
               "git -C \"$ROOT\" ls-files --others --exclude-standard"):
    require(layer_runner, needle,
            "layer gate source identity includes staged changes")
static_exit = layer_runner.find('if [ "$STATIC_ONLY" -eq 1 ]; then')
untracked_gate = layer_runner.find('UNTRACKED_FILES="$(git -C "$ROOT"')
if static_exit < 0 or untracked_gate < 0 or static_exit > untracked_gate:
    fail("static-only layer gate must complete before the remote cleanliness gate")

# Passive BSS discovery recognizes the RSN SAE suite so a later state machine
# can consume an exact selected-BSS snapshot. It remains strictly inactive:
# active configuration stays PSK-only and net80211 still emits and accepts
# only Open-System authentication. Letting Algorithm 3 cross this boundary
# now would be a false implementation claim rather than a working exchange.
require(crypto, "IEEE80211_AKM_SAE", "passive net80211 SAE AKM taxonomy")
require(crypto_source, "ic->ic_rsnakms = IEEE80211_AKM_PSK;",
        "PSK-only active net80211 configuration")
forbid(crypto_source, "IEEE80211_AKM_SAE",
       "active net80211 SAE configuration")
forbid(input_source, "IEEE80211_AUTH_ALG_SAE", "net80211 SAE auth algorithm")
auth_tx = body(output, "mbuf_t\nieee80211_get_auth", "net80211 auth TX")
require(auth_tx, "LE_WRITE_2(frm, IEEE80211_AUTH_ALG_OPEN)",
        "Open-System-only auth TX")
auth_rx = body(input_source, "void\nieee80211_recv_auth", "net80211 auth RX")
require(auth_rx, "if (algo != IEEE80211_AUTH_ALG_OPEN)",
        "Open-System-only auth RX")

print("PASS: Tahoe SAE/PMF quarantine layer contracts")
PY

python3 "$root/scripts/evaluate_tahoe_sae_capture.py" --self-test
bash -n "$root/scripts/run_tahoe_sae_lab_profiles.sh"
bash "$root/scripts/test_tahoe_sae_lab_scenario_contract.sh"
