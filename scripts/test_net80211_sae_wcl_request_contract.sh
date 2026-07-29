#!/usr/bin/env bash
# Source-level contract for the direct WCL SAE request policy.  The policy is
# intentionally public identity only; this test keeps its bounded generation,
# one-shot scan handoff, post-copy bind, and legacy fallback boundaries
# reviewable without needing a live radio.
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

python3 - "$root" <<'PY'
from pathlib import Path
import re
import sys


root = Path(sys.argv[1])
var_h = (root / "itl80211/openbsd/net80211/ieee80211_var.h").read_text()
proto_h = (root / "itl80211/openbsd/net80211/ieee80211_proto.h").read_text()
proto_c = (root / "itl80211/openbsd/net80211/ieee80211_proto.c").read_text()
node_h = (root / "itl80211/openbsd/net80211/ieee80211_node.h").read_text()
node_c = (root / "itl80211/openbsd/net80211/ieee80211_node.c").read_text()
ieee_c = (root / "itl80211/openbsd/net80211/ieee80211.c").read_text()
skywalk_cpp = (
    root / "AirportItlwm/AirportItlwmSkywalkInterface.cpp"
).read_text()


def fail(message):
    raise SystemExit(f"net80211 direct-WCL SAE request contract: {message}")


def require(text, needle, label):
    if needle not in text:
        fail(f"missing {label}: {needle}")


def ordered(text, label, *needles):
    position = 0
    for needle in needles:
        found = text.find(needle, position)
        if found < 0:
            fail(f"{label} missing ordered token: {needle}")
        position = found + len(needle)


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


def strip_comments(text):
    return re.sub(r"/\*.*?\*/|//[^\n]*", "", text, flags=re.S)


for token in (
    "IEEE80211_SAE_WCL_REQUEST_NONE = 0",
    "IEEE80211_SAE_WCL_REQUEST_PENDING",
    "IEEE80211_SAE_WCL_REQUEST_SCAN_STARTING",
    "IEEE80211_SAE_WCL_REQUEST_SCAN_ISSUED",
    "IEEE80211_SAE_WCL_REQUEST_BOUND",
    "IEEE80211_SAE_WCL_REQUEST_RESUME_FAILED = 0",
    "IEEE80211_SAE_WCL_REQUEST_RESUME_STARTED = 1",
    "IEEE80211_SAE_WCL_REQUEST_RESUME_RETRY = 2",
    "IEEE80211_SAE_WCL_REQUEST_BIND_NONE = 0",
    "IEEE80211_SAE_WCL_REQUEST_BIND_BOUND = 1",
    "IEEE80211_SAE_WCL_REQUEST_BIND_REJECTED = -1",
    "u_int64_t\t\tgeneration;",
    "u_int64_t\t\tassociation_epoch;",
    "u_int8_t\t\tbssid[IEEE80211_ADDR_LEN];",
    "u_int8_t\t\tssid_len;",
    "u_int8_t\t\tssid[IEEE80211_NWID_LEN];",
    "u_int8_t\t\tphase;",
    "u_int64_t\t\tic_sae_wcl_request_next_generation;",
    "u_int64_t\t\tic_sae_wcl_policy_generation;",
    "struct ieee80211_sae_wcl_request ic_sae_wcl_request;",
    "u_int8_t\t\tic_sae_wcl_fresh_carrier_required;",
    "u_int8_t\t\tic_sae_wcl_request_policy_starting;",
    "u_int8_t\t\tic_sae_wcl_request_join_active;",
    "ic_sae_wcl_request_revoke",
):
    require(var_h, token, "public request value state")

request_record = body(var_h, "struct ieee80211_sae_wcl_request", "request record")
for forbidden in ("password", "pmk", "pwe", "kck", "callback", "node", "ieee80211_key"):
    if forbidden in request_record.lower():
        fail(f"request record must not retain secret/owner material: {forbidden}")

bound_record = body(var_h, "struct ieee80211_sae_wcl_bound_request",
                    "bound request copy-out record")
for token in (
    "u_int64_t\t\tgeneration;",
    "u_int64_t\t\tassociation_epoch;",
    "u_int32_t\t\tsae_scan_flags;",
    "u_int8_t\t\tbssid[IEEE80211_ADDR_LEN];",
    "u_int8_t\t\tsta[IEEE80211_ADDR_LEN];",
    "u_int8_t\t\tssid_len;",
    "u_int8_t\t\tssid[IEEE80211_NWID_LEN];",
    "u_int8_t\t\tsae_profile;",
):
    require(bound_record, token, "bound value snapshot")
for forbidden in ("password", "pmk", "pwe", "kck", "callback", "node", "ieee80211_key", "engine"):
    if forbidden in bound_record.lower():
        fail(f"bound snapshot must not retain secret/owner material: {forbidden}")

for token in (
    "ieee80211_sae_wcl_request_publish",
    "ieee80211_sae_wcl_request_begin",
    "ieee80211_sae_wcl_request_clear_if_generation",
    "ieee80211_sae_wcl_request_resume_scan",
    "ieee80211_sae_wcl_request_scan_starting",
    "ieee80211_sae_wcl_request_scan_started",
    "ieee80211_sae_wcl_request_scan_selection_held",
    "ieee80211_sae_wcl_request_scan_selection_owned",
    "ieee80211_sae_wcl_request_join_begin",
    "ieee80211_sae_wcl_request_join_end",
    "ieee80211_sae_wcl_request_bind_selected_bss",
    "ieee80211_sae_wcl_request_bound_current",
    "ieee80211_sae_wcl_request_copyout_bound_current",
    "ieee80211_sae_wcl_peer_rx_admit",
    "ieee80211_sae_wcl_fresh_carrier_accepted",
):
    require(proto_h, token, "public request API declaration")

require(node_h, "ieee80211_node_cleanup_sae_wcl_scan_starting",
        "exact SCAN_STARTING node-cleanup declaration")
scan_cleanup = body(
    node_c, "ieee80211_node_cleanup_sae_wcl_scan_starting(",
    "exact SCAN_STARTING node cleanup")
for token in (
        "expected_generation == 0",
        "ic->ic_state != IEEE80211_S_SCAN",
        "ni != ic->ic_bss",
        "ieee80211_sae_wcl_request_scan_starting(ic,",
        "current_generation != expected_generation",
        "ieee80211_node_cleanup_internal(ic, ni, 1)",
        "ieee80211_node_cleanup_internal(ic, ni, 0)"):
    require(scan_cleanup, token, "exact SCAN_STARTING cleanup fence")
ordered(scan_cleanup, "SCAN_STARTING cleanup decision",
        "ieee80211_sae_wcl_request_scan_starting(ic,",
        "current_generation != expected_generation",
        "ieee80211_node_cleanup_internal(ic, ni, 1)",
        "ieee80211_node_cleanup_internal(ic, ni, 0)")

publish = body(proto_c, "u_int64_t\nieee80211_sae_wcl_request_publish", "publish")
for token in (
    "ic_pae_selected_bss_lock",
    "ieee80211_sae_wcl_request_join_active_locked(ic)",
    "ieee80211_sae_wcl_request_run_is_stable_locked(ic)",
    "IEEE80211_SAE_WCL_REQUEST_NONE",
    "IEEE80211_SAE_WCL_REQUEST_PENDING",
    "IEEE80211_SAE_WCL_REQUEST_SCAN_ISSUED",
    "ieee80211_sae_wcl_request_clear_locked(ic, &revocation)",
    "ic_sae_wcl_request_next_generation == (u_int64_t)-1",
    "generation = ++ic->ic_sae_wcl_request_next_generation;",
    "IEEE80211_SAE_WCL_REQUEST_PENDING",
    "IEEE80211_ADDR_COPY(request->bssid, bssid)",
    "memcpy(request->ssid, ssid, ssid_len)",
    "IOSimpleLockLockDisableInterrupt",
    "IOSimpleLockUnlockEnableInterrupt",
):
    require(publish, token, "publish fence")
for forbidden in ("ic_psk", "password", "PMK", "PWE", "ic_newstate("):
    if forbidden in publish:
        fail(f"publish must remain public identity only: {forbidden}")
if "IEEE80211_SAE_WCL_REQUEST_SCAN_STARTING" in strip_comments(publish):
    fail("publish must not replace a raw scan-starting request")
require(publish, "ic->ic_sae_wcl_request_policy_starting != 0",
        "publish rejects an in-progress pure-SAE policy reservation")
ordered(publish, "publish supersede revokes after the leaf lock",
        "ieee80211_sae_wcl_request_clear_locked(ic, &revocation)",
        "IOSimpleLockUnlockEnableInterrupt",
        "ieee80211_sae_wcl_request_revocation_deliver(ic, &revocation)")

join_fence = body(proto_c,
                  "static int\nieee80211_sae_wcl_request_join_active_locked",
                  "selected-BSS join fence")
for token in (
        "ic->ic_sae_wcl_request_join_active",
        "ic->ic_state == IEEE80211_S_SCAN",
        "ic->ic_pae_selected_bss.epoch",
):
    require(join_fence, token, "selected-BSS join fence")

join_begin = body(proto_c,
                  "int\nieee80211_sae_wcl_request_join_begin",
                  "join publication fence begin")
join_end = body(proto_c,
                "void\nieee80211_sae_wcl_request_join_end",
                "join publication fence end")
for token in ("int begun = 0", "ic->ic_sae_wcl_request_policy_starting == 0",
              "ic->ic_sae_wcl_request_join_active = 1", "return begun"):
    require(join_begin, token, "join publication fence begin")
require(join_end, "ic->ic_sae_wcl_request_join_active = 0",
        "join publication fence end")

policy_begin = body(proto_c,
                    "u_int64_t\nieee80211_sae_wcl_request_begin",
                    "pure-SAE policy begin")
for token in (
        "ic->ic_state != IEEE80211_S_SCAN",
        "(ic->ic_caps & IEEE80211_C_RSN) == 0",
        "ic->ic_sae_wcl_request_policy_starting != 0",
        "ieee80211_sae_wcl_request_join_active_locked(ic)",
        "ic->ic_sae_wcl_request_policy_starting = 1",
        "ieee80211_disable_wep(ic)",
        "ic->ic_sae_wcl_request_policy_starting == 0",
        "ic->ic_rsnprotos = IEEE80211_PROTO_RSN",
        "ic->ic_rsnakms = IEEE80211_AKM_SAE",
        "ic->ic_rsnciphers = IEEE80211_CIPHER_CCMP",
        "ic->ic_rsngroupcipher = IEEE80211_CIPHER_CCMP",
        "ic->ic_rsngroupmgmtcipher = IEEE80211_CIPHER_BIP",
        "IEEE80211_F_RSNON | IEEE80211_F_MFPR",
        "ic->ic_pae_mfp_requested = 1",
        "ic->ic_external_pmk_owner = 0",
        "explicit_bzero(ic->ic_psk",
        "ic->ic_sae_wcl_policy_generation = generation",
        "request->phase = IEEE80211_SAE_WCL_REQUEST_PENDING",
        "ic->ic_sae_wcl_fresh_carrier_required = 0",
        "ic->ic_sae_wcl_request_policy_starting = 0",
):
    require(policy_begin, token, "pure-SAE policy begin fence")
ordered(policy_begin, "reservation protects out-of-lock WEP teardown",
        "ic->ic_sae_wcl_request_policy_starting = 1",
        "ieee80211_disable_wep(ic)",
        "ic->ic_sae_wcl_request_policy_starting == 0",
        "ic->ic_sae_wcl_policy_generation = generation",
        "request->phase = IEEE80211_SAE_WCL_REQUEST_PENDING",
        "ic->ic_sae_wcl_fresh_carrier_required = 0",
        "ic->ic_sae_wcl_request_policy_starting = 0",
        "IOSimpleLockUnlockEnableInterrupt")
policy_begin_code = strip_comments(policy_begin)
if "IEEE80211_C_MFP" in policy_begin_code:
    fail("pure-SAE policy begin must not require a hardware-MFP capability bit")
if "ieee80211_disable_rsn(" in policy_begin_code:
    fail("pure-SAE policy begin must not destroy its own upcoming generation")
if "IEEE80211_SAE_WCL_REQUEST_SCAN_STARTING" in policy_begin_code:
    fail("pure-SAE policy begin must not replace a raw scan-starting request")

policy_clear = body(proto_c,
                    "static void\nieee80211_sae_wcl_request_policy_clear_locked",
                    "generation-owned pure-SAE policy clear")
for token in (
        "ic->ic_sae_wcl_policy_generation = 0",
        "ic->ic_pae_mfp_requested = 0",
        "IEEE80211_F_PSK | IEEE80211_F_RSNON |",
        "IEEE80211_F_MFPR | IEEE80211_F_DESBSSID",
        "explicit_bzero(ic->ic_psk",
        "ic->ic_external_pmk_owner = 0",
        "ic->ic_rsnprotos = 0",
        "ic->ic_rsnakms = 0",
        "ic->ic_rsngroupmgmtcipher = (enum ieee80211_cipher)0",
        "explicit_bzero(ic->ic_des_bssid",
):
    require(policy_clear, token, "generation-owned pure-SAE policy cleanup")
clear_locked = body(proto_c,
                    "static void\nieee80211_sae_wcl_request_clear_locked",
                    "request/policy generation pairing")
ordered(clear_locked, "request clear only tears down its own policy",
        "generation = ic->ic_sae_wcl_request.generation",
        "ic->ic_sae_wcl_policy_generation == generation",
        "ieee80211_sae_wcl_request_policy_clear_locked(ic)",
        "explicit_bzero(&ic->ic_sae_wcl_request")

run_publish_fence = body(proto_c,
                         "static int\nieee80211_sae_wcl_request_run_is_stable_locked",
                         "stable-RUN publication fence")
for token in (
        "ic->ic_state != IEEE80211_S_RUN",
        "ic->ic_pae_assoc_epoch",
        "ic->ic_pae_assoc_replace_epoch",
        "ic->ic_pae_selected_bss.epoch",
):
    require(run_publish_fence, token, "stable-RUN publication fence")

clear = body(proto_c,
             "int\nieee80211_sae_wcl_request_clear_if_generation",
             "generation-specific clear")
for token in (
    "generation == 0",
    "ic->ic_sae_wcl_request.generation == generation",
    "ieee80211_sae_wcl_request_phase_is_active",
    "ieee80211_sae_wcl_request_clear_locked(ic, &revocation)",
    "ieee80211_sae_wcl_request_revocation_deliver(ic, &revocation)",
    "return cleared",
):
    require(clear, token, "generation-specific clear fence")
ordered(clear, "generation-specific clear revokes after the leaf lock",
        "ieee80211_sae_wcl_request_clear_locked(ic, &revocation)",
        "IOSimpleLockUnlockEnableInterrupt",
        "ieee80211_sae_wcl_request_revocation_deliver(ic, &revocation)")

resume = body(proto_c,
              "int\nieee80211_sae_wcl_request_resume_scan",
              "one-shot scan resume")
for token in (
    "IEEE80211_SAE_WCL_REQUEST_PENDING",
    "IEEE80211_SAE_WCL_REQUEST_SCAN_STARTING",
    "IEEE80211_SAE_WCL_REQUEST_RESUME_RETRY",
    "ieee80211_sae_wcl_request_owner_hooks_ready_locked(ic)",
    "ieee80211_sae_wcl_request_join_active_locked(ic)",
    "ieee80211_sae_wcl_request_fence_run_resume",
    "ieee80211_sae_wcl_request_scan_starting_locked",
    "ieee80211_sae_wcl_request_scan_issued_locked",
    "scan_error == EAGAIN",
    "(*ic->ic_newstate)(ic, IEEE80211_S_SCAN, -1)",
    "ieee80211_sae_wcl_request_clear_if_generation",
):
    require(resume, token, "one-shot scan resume fence")
if "ieee80211_new_state(" in resume:
    fail("one-shot scan resume must call ic_newstate directly, not the macro")
if resume.count("ieee80211_sae_wcl_request_scan_starting_locked") != 1:
    fail("resume must revalidate its STARTING generation before raw scan entry")
for token in (
        "IEEE80211_SAE_WCL_REQUEST_BOUND",
        "ic->ic_sae_wcl_request.generation == generation",
):
    require(resume, token, "synchronous scan bind acknowledgement")

run_fence = body(proto_c,
                 "static int\nieee80211_sae_wcl_request_fence_run_resume",
                 "RUN resume fence")
for token in (
    "ic->ic_state != IEEE80211_S_RUN",
    "ieee80211_pae_assoc_epoch_advance_locked(ic)",
    "ieee80211_pae_selected_bss_invalidate(ic)",
    "ieee80211_sae_peer_rx_admission_clear_locked(ic)",
    "ieee80211_sae_wcl_request_scan_starting_locked(ic, generation)",
    "ic->ic_sae_wcl_request.association_epoch = 0",
    "ieee80211_pae_mfp_txn_cancel_locked",
):
    require(run_fence, token, "RUN resume old-association fence")
if "ieee80211_sae_wcl_request_clear_locked" in run_fence:
    fail("RUN resume fence must preserve its one SCAN_ISSUED request")

begin = body(proto_c, "u_int64_t\nieee80211_pae_assoc_epoch_begin", "ordinary epoch begin")
require(begin, "ic->ic_sae_wcl_request_policy_starting = 0",
        "ordinary epoch cancels a policy reservation")
require(begin, "ieee80211_sae_wcl_request_clear_locked(ic, &revocation)",
        "ordinary epoch request cancellation")
ordered(begin, "direct-SAE RUN cancellation waits for a fresh carrier",
        "ic->ic_state == IEEE80211_S_RUN",
        "IEEE80211_SAE_WCL_REQUEST_BOUND",
        "ic->ic_sae_wcl_fresh_carrier_required = 1",
        "ieee80211_sae_wcl_request_clear_locked(ic, &revocation)")
ordered(begin, "ordinary epoch revokes after the leaf lock",
        "ieee80211_sae_wcl_request_clear_locked(ic, &revocation)",
        "IOSimpleLockUnlockEnableInterrupt",
        "ieee80211_sae_wcl_request_revocation_deliver(ic, &revocation)")
replacement = body(proto_c,
                   "u_int64_t\nieee80211_pae_assoc_epoch_begin_replacement",
                   "controlled replacement")
require(replacement, "ic->ic_sae_wcl_request_policy_starting = 0",
        "controlled replacement cancels a policy reservation")
ordered(replacement, "controlled replacement preserves its one bind handoff",
        "ieee80211_sae_wcl_request_scan_issued_locked",
        "IEEE80211_SAE_WCL_REQUEST_PENDING",
        "ic->ic_sae_wcl_request.association_epoch = 0",
        "else", "ieee80211_sae_wcl_request_clear_locked(ic, &revocation)")
destroy = body(proto_c, "void\nieee80211_pae_selected_bss_lock_destroy",
               "terminal lock destruction")
require(destroy, "ieee80211_sae_wcl_request_clear_locked(ic, &revocation)",
        "terminal request scrub")
require(destroy, "ic->ic_sae_wcl_request_join_active = 0",
        "terminal join-fence scrub")
require(destroy, "ic->ic_sae_wcl_request_policy_starting = 0",
        "terminal policy-reservation scrub")
require(destroy, "ic->ic_sae_wcl_fresh_carrier_required = 0",
        "terminal fresh-carrier wait scrub")
ordered(destroy, "terminal scrub revokes after the leaf lock",
        "ieee80211_sae_wcl_request_clear_locked(ic, &revocation)",
        "IOSimpleLockUnlockEnableInterrupt",
        "ieee80211_sae_wcl_request_revocation_deliver(ic, &revocation)")

attach = body(ieee_c, "void\nieee80211_ifattach", "net80211 attach")
for token in (
        "ic->ic_sae_wcl_request_next_generation = 0;",
        "ic->ic_sae_wcl_policy_generation = 0;",
        "memset(&ic->ic_sae_wcl_request, 0,",
        "ic->ic_sae_wcl_fresh_carrier_required = 0;",
        "ic->ic_sae_wcl_request_policy_starting = 0;",
        "ic->ic_sae_wcl_request_join_active = 0;",
):
    require(attach, token, "direct-WCL request attach initialization")

bind = body(proto_c,
            "int\nieee80211_sae_wcl_request_bind_selected_bss",
            "post-copy BSS bind")
for token in (
    "ic->ic_state == IEEE80211_S_SCAN",
    "ieee80211_sae_wcl_request_scan_issued_locked",
    "ieee80211_sae_wcl_request_matches_current_locked",
    "ieee80211_sae_wcl_request_owner_hooks_ready_locked(ic)",
    "IEEE80211_SAE_WCL_REQUEST_BOUND",
    "IEEE80211_SAE_WCL_REQUEST_BIND_BOUND",
    "IEEE80211_SAE_WCL_REQUEST_BIND_REJECTED",
    "ieee80211_sae_wcl_request_clear_locked(ic, &revocation)",
    "ieee80211_sae_wcl_request_revocation_deliver(ic, &revocation)",
):
    require(bind, token, "post-copy bind fence")
ordered(bind, "bind mismatch revokes after the leaf lock",
        "ieee80211_sae_wcl_request_clear_locked(ic, &revocation)",
        "IOSimpleLockUnlockEnableInterrupt",
        "ieee80211_sae_wcl_request_revocation_deliver(ic, &revocation)")
matches = body(proto_c,
               "static int\nieee80211_sae_wcl_request_matches_current_locked",
               "exact selected-BSS match")
for token in (
    "ic->ic_bss != ni",
    "ic->ic_pae_assoc_epoch",
    "ic->ic_pae_assoc_replace_epoch",
    "ieee80211_pae_selected_bss_identity_matches",
    "IEEE80211_SAE_SELECTED_BSS_PROFILE_PURE",
    "IEEE80211_SAE_SELECTED_BSS_PROFILE_TRANSITION",
):
    require(matches, token, "exact selected-BSS match fence")
bound = body(proto_c,
             "int\nieee80211_sae_wcl_request_bound_current",
             "bound-current predicate")
for token in (
    "IEEE80211_SAE_WCL_REQUEST_BOUND",
    "association_epoch == epoch",
    "ieee80211_sae_wcl_request_owner_hooks_ready_locked(ic)",
    "ieee80211_sae_wcl_request_matches_current_locked",
):
    require(bound, token, "bound-current fence")

revoke = body(proto_c,
              "static void\nieee80211_sae_wcl_request_revocation_deliver",
              "post-unlock driver revocation delivery")
for token in (
    "revocation->callback",
    "revocation->generation",
    "explicit_bzero(revocation",
    "(*callback)(ic, generation)",
):
    require(revoke, token, "generation-only revocation delivery")
if any(token in revoke.lower() for token in ("password", "pmk", "pwe", "node")):
    fail("revocation delivery must not carry private/request-owner material")

owner_ready = body(proto_c,
                   "static int\nieee80211_sae_wcl_request_owner_hooks_ready_locked",
                   "direct SAE owner readiness gate")
for token in (
    "ic->ic_sae_auth_hold != NULL",
    "ic->ic_sae_auth_owned != NULL",
    "ic->ic_sae_engine_peer_event != NULL",
    "ic->ic_sae_wcl_request_revoke != NULL",
):
    require(owner_ready, token, "complete direct SAE owner readiness")

copyout = body(proto_c,
               "int\nieee80211_sae_wcl_request_copyout_bound_current",
               "bound request value copy-out")
for token in (
    "expected_generation == 0",
    "request->generation != 0",
    "IEEE80211_SAE_WCL_REQUEST_BOUND",
    "ieee80211_sae_wcl_request_owner_hooks_ready_locked(ic)",
    "ieee80211_sae_wcl_request_matches_current_locked",
    "out->generation = request->generation",
    "out->association_epoch = epoch",
    "out->sae_scan_flags = selected->sae_scan_flags",
    "IEEE80211_ADDR_COPY(out->bssid, request->bssid)",
    "IEEE80211_ADDR_COPY(out->sta, ic->ic_myaddr)",
    "memcpy(out->ssid, request->ssid, request->ssid_len)",
    "out->sae_profile = selected->strict_pure_sae_profile",
):
    require(copyout, token, "bound request value copy-out fence")
for forbidden in ("password", "pmk", "pwe", "struct ieee80211_node *",
                  "ic_sae_auth_hold(", "ic_sae_engine_peer_event("):
    if forbidden in copyout.lower():
        fail(f"bound request copy-out must remain value-only: {forbidden}")

wcl_peer_profile = body(
    proto_c, "static int\nieee80211_sae_wcl_peer_rx_admission_group19_hnp_locked",
    "direct-WCL group-19/HnP profile gate")
for token in (
    "IEEE80211_SAE_SELECTED_BSS_PROFILE_PURE",
    "ieee80211_sae_admission_group19_hnp(selected, out)",
    "IEEE80211_SAE_SELECTED_BSS_PROFILE_TRANSITION",
    "IEEE80211_SAE_SCAN_CENSUS_COMPLETE",
    "ieee80211_sae_scan_transition_akm_census_is_supported",
    "IEEE80211_SAE_SCAN_TRANSITION_AKM_MASK",
    "IEEE80211_SAE_ADMISSION_GROUP19_HNP_ALLOWED_FLAGS",
    "IEEE80211_SAE_ADMISSION_GROUP_19",
    "IEEE80211_SAE_ADMISSION_METHOD_HNP",
):
    require(wcl_peer_profile, token, "direct-WCL group-19/HnP profile gate")
for forbidden in ("H2E_ONLY_SELECTOR", "password", "pmk", "pwe",
                  "struct ieee80211_node *", "ieee80211_new_state"):
    if forbidden.lower() in wcl_peer_profile.lower():
        fail(f"direct-WCL profile gate must remain bounded: {forbidden}")

wcl_peer_admit = body(proto_c, "int\nieee80211_sae_wcl_peer_rx_admit",
                      "direct-WCL peer-RX admission")
for token in (
    "bound->generation",
    "bound->association_epoch",
    "bound->bssid",
    "bound->ssid_len",
    "bound->ssid",
    "bound->sta",
    "bound->sae_scan_flags",
    "bound->sae_profile",
    "ic->ic_state == IEEE80211_S_AUTH",
    "IEEE80211_SAE_WCL_REQUEST_BOUND",
    "ieee80211_sae_wcl_request_owner_hooks_ready_locked(ic)",
    "ieee80211_sae_wcl_request_matches_current_locked",
    "ieee80211_sae_wcl_peer_rx_admission_group19_hnp_locked",
    "ieee80211_sae_peer_rx_admission_clear_locked(ic)",
    "ic_sae_peer_rx_admission.active = 1",
    "IOSimpleLockLockDisableInterrupt",
    "IOSimpleLockUnlockEnableInterrupt",
):
    require(wcl_peer_admit, token, "direct-WCL peer-RX exact-bound gate")
for forbidden in ("ic_event_handler", "ieee80211_new_state", "ic_psk",
                  "password", "pmk", "pwe", "struct ieee80211_node *",
                  "IOCommandGate"):
    if forbidden.lower() in wcl_peer_admit.lower():
        fail(f"direct-WCL peer-RX admission must remain value-only: {forbidden}")

controller_peer_admit = body(proto_c, "int\nieee80211_sae_peer_rx_admit",
                             "controller peer-RX admission")
for token in (
    "ieee80211_sae_admission_group19_hnp(&ic->ic_pae_selected_bss",
    "ic_sae_peer_rx_admission.active = 1",
):
    require(controller_peer_admit, token,
            "controller peer-RX admission stays pure-SAE-only")
if "IEEE80211_SAE_SELECTED_BSS_PROFILE_TRANSITION" in controller_peer_admit:
    fail("controller peer-RX admission must not admit transition SAE")

auth_owner = body(proto_c,
                  "static int\nieee80211_sae_wcl_request_auth_owner_state",
                  "S_AUTH direct owner gate")
for token in (
    "IEEE80211_SAE_WCL_AUTH_OWNER_READY",
    "IEEE80211_SAE_WCL_AUTH_OWNER_REJECTED",
    "IEEE80211_SAE_WCL_REQUEST_BOUND",
    "ic->ic_sae_wcl_fresh_carrier_required != 0",
    "ieee80211_sae_wcl_request_owner_hooks_ready_locked(ic)",
    "ieee80211_sae_wcl_request_clear_locked(ic, &revocation)",
    "ieee80211_sae_wcl_request_revocation_deliver(ic, &revocation)",
):
    require(auth_owner, token, "S_AUTH direct owner fail-closed gate")
ordered(auth_owner, "S_AUTH rejection revokes after the leaf lock",
        "ieee80211_sae_wcl_request_clear_locked(ic, &revocation)",
        "IOSimpleLockUnlockEnableInterrupt",
        "ieee80211_sae_wcl_request_revocation_deliver(ic, &revocation)")

newstate_note = body(
    proto_c, "void\nieee80211_pae_assoc_epoch_note_newstate",
    "pre-driver state request fence")
require(newstate_note, "(void)ieee80211_pae_assoc_epoch_begin(ic);",
        "every non-forward state edge keeps ordinary epoch teardown")

fresh_carrier = body(
    proto_c, "void\nieee80211_sae_wcl_fresh_carrier_accepted",
    "fresh association carrier acceptance")
ordered(fresh_carrier, "fresh carrier clears under the selected-BSS leaf",
        "ic->ic_pae_selected_bss_lock",
        "IOSimpleLockLockDisableInterrupt(lock)",
        "ieee80211_sae_wcl_request_clear_locked(ic, &revocation)",
        "ic->ic_sae_wcl_fresh_carrier_required = 0",
        "IOSimpleLockUnlockEnableInterrupt(lock, irq)",
        "ieee80211_sae_wcl_request_revocation_deliver(ic, &revocation)")

associate = body(
    skywalk_cpp, "AirportItlwmSkywalkInterface::associateSSID(",
    "shared public/WCL association carrier")
ordered(associate, "new non-SAE carrier supersedes retired direct SAE",
        "requiresUnsupportedWpa3Auth(",
        "struct ieee80211com *ic",
        "ieee80211_sae_wcl_fresh_carrier_accepted(ic)",
        "ieee80211_public_initial_bssid_pin_disarm(ic)")

newstate_start = proto_c.find("int\nieee80211_newstate")
newstate_end = proto_c.find("void\nieee80211_set_link_state",
                            newstate_start)
if newstate_start < 0 or newstate_end < 0:
    fail("missing generic state-transition S_AUTH region")
newstate = proto_c[newstate_start:newstate_end]
auth_case = newstate.find("case IEEE80211_S_AUTH:")
if auth_case < 0:
    fail("missing generic S_AUTH state")
auth_tail = newstate[auth_case:]
ordered(auth_tail, "direct-WCL S_AUTH gate precedes historical Open auth",
        "ieee80211_sae_wcl_request_auth_owner_state(",
        "IEEE80211_SAE_WCL_AUTH_OWNER_REJECTED",
        "ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);",
        "IEEE80211_SAE_WCL_AUTH_OWNER_READY",
        "sae_auth_hold == 0",
        "ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);",
        "IEEE80211_SEND_MGMT(ic, ni,",
        "IEEE80211_FC0_SUBTYPE_AUTH, 1)")

join = body(node_c, "void\nieee80211_node_join_bss", "BSS join")
ordered(join, "bind after post-copy capture before RSN selection",
        "if (!ieee80211_sae_wcl_request_join_begin(ic))",
        "return;",
        "(*ic->ic_node_copy)(ic, ic->ic_bss, selbs);",
        "ieee80211_pae_selected_bss_capture(ic, ni, sae_profile,",
        "ieee80211_sae_wcl_request_bind_selected_bss(ic, ni,",
        "IEEE80211_SAE_WCL_REQUEST_BIND_REJECTED",
        "ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);",
        "ieee80211_sae_wcl_request_join_end(ic);",
        "ieee80211_choose_rsnparams(ic);")
if join.count("ieee80211_sae_wcl_request_join_end(ic);") < 3:
    fail("every BSS-join exit must release the direct-WCL publication fence")
choose = body(node_c, "void\nieee80211_choose_rsnparams", "RSN chooser")
ordered(choose, "SAE preservation requires exact bound request",
        "IEEE80211_AKM_SAE",
        "ieee80211_sae_wcl_request_bound_current(ic, ni)",
        "ni->ni_rsnakms = IEEE80211_AKM_SAE;")

scan_policy = body(proto_c,
                   "static int\nieee80211_sae_wcl_request_scan_policy_matches_locked",
                   "exact pure-SAE scan policy predicate")
for token in (
        "request->generation == ic->ic_sae_wcl_policy_generation",
        "ieee80211_sae_wcl_request_identity_is_valid_locked(request)",
        "IEEE80211_F_RSNON | IEEE80211_F_MFPR",
        "(ic->ic_flags & IEEE80211_F_PSK) == 0",
        "ic->ic_pae_mfp_requested != 0",
        "ic->ic_rsnakms == IEEE80211_AKM_SAE",
        "ic->ic_rsngroupmgmtcipher == IEEE80211_CIPHER_BIP",
):
    require(scan_policy, token, "exact pure-SAE scan policy fence")

scan_starting = body(proto_c,
                     "int\nieee80211_sae_wcl_request_scan_starting(struct",
                     "direct scan-starting query")
for token in (
        "*generation = 0",
        "ieee80211_sae_wcl_request_owner_hooks_ready_locked(ic)",
        "ieee80211_sae_wcl_request_scan_starting_locked(ic,",
        "ieee80211_sae_wcl_request_scan_policy_matches_locked(ic,",
        "*generation = ic->ic_sae_wcl_request.generation",
):
    require(scan_starting, token, "direct scan-starting query fence")
for forbidden in ("ieee80211_new_state", "ieee80211_disable_rsn",
                  "ieee80211_disable_wep", "ic_newstate"):
    if forbidden in scan_starting:
        fail(f"direct scan-starting query must be side-effect free: {forbidden}")

scan_started = body(proto_c,
                    "int\nieee80211_sae_wcl_request_scan_started",
                    "direct fresh-scan promotion")
for token in (
        "ic->ic_state != IEEE80211_S_SCAN",
        "ieee80211_sae_wcl_request_scan_starting_locked(ic, generation)",
        "ieee80211_sae_wcl_request_scan_policy_matches_locked(ic,",
        "IEEE80211_SAE_WCL_REQUEST_SCAN_ISSUED",
):
    require(scan_started, token, "direct fresh-scan promotion fence")
for forbidden in ("ieee80211_new_state", "ieee80211_disable_rsn",
                  "ieee80211_disable_wep", "ic_newstate"):
    if forbidden in scan_started:
        fail(f"direct fresh-scan promotion must be leaf-only: {forbidden}")

# HOLD and SELECT are intentionally separate.  A policy reservation, PENDING,
# or STARTING request can only stop an old scan result from being consumed;
# only a later SCAN_ISSUED request may suppress switch_ess() and proceed to
# BSS bind.
scan_held = body(proto_c,
                 "int\nieee80211_sae_wcl_request_scan_selection_held",
                 "pure-SAE scan HOLD predicate")
for token in (
        "ic->ic_state != IEEE80211_S_SCAN",
        "ic->ic_sae_wcl_request_policy_starting != 0",
        "request->phase == IEEE80211_SAE_WCL_REQUEST_PENDING",
        "request->phase == IEEE80211_SAE_WCL_REQUEST_SCAN_STARTING",
        "ieee80211_sae_wcl_request_scan_policy_matches_locked(ic, request)",
):
    require(scan_held, token, "pure-SAE scan HOLD fence")
scan_held_code = strip_comments(scan_held)
if "IEEE80211_SAE_WCL_REQUEST_SCAN_ISSUED" in scan_held_code:
    fail("scan HOLD must not consume or select an issued replacement scan")

scan_owned = body(proto_c,
                  "int\nieee80211_sae_wcl_request_scan_selection_owned",
                  "pure-SAE scan SELECT predicate")
for token in (
        "ic->ic_state != IEEE80211_S_SCAN",
        "request->phase == IEEE80211_SAE_WCL_REQUEST_SCAN_ISSUED",
        "ieee80211_sae_wcl_request_scan_policy_matches_locked(ic, request)",
):
    require(scan_owned, token, "pure-SAE scan SELECT fence")
scan_owned_code = strip_comments(scan_owned)
for forbidden_phase in ("IEEE80211_SAE_WCL_REQUEST_PENDING",
                        "IEEE80211_SAE_WCL_REQUEST_SCAN_STARTING"):
    if forbidden_phase in scan_owned_code:
        fail("scan SELECT must not treat a pending/starting request as selectable")
for predicate, label in ((scan_policy, "scan policy"),
                         (scan_held, "scan HOLD"),
                         (scan_owned, "scan SELECT")):
    for forbidden in ("ieee80211_new_state", "ieee80211_disable_rsn",
                      "ieee80211_disable_wep", "ic_newstate"):
        if forbidden in predicate:
            fail(f"{label} predicate must be side-effect free: {forbidden}")

end_scan = body(node_c, "void\nieee80211_end_scan", "scan completion")
ordered(end_scan, "scan HOLD returns before consuming a historical result",
        "ieee80211_sae_wcl_request_scan_selection_held(ic)",
        "return;",
        "ni = RB_MIN(ieee80211_tree, &ic->ic_tree);")
ordered(end_scan, "issued scan alone suppresses legacy ESS overwrite",
        "!ieee80211_sae_wcl_request_scan_selection_owned(ic)",
        "ieee80211_switch_ess(ic)")


class RequestModel:
    NONE, PENDING, SCAN_STARTING, SCAN_ISSUED, BOUND = range(5)
    OWNER_NONE = 0
    OWNER_READY = 1
    OWNER_REJECTED = -1
    RESUME_FAILED = 0
    RESUME_STARTED = 1
    RESUME_RETRY = 2

    def __init__(self):
        self.next_generation = 0
        self.phase = self.NONE
        self.generation = 0
        self.epoch = 0
        self.identity = None
        self.join_active = False
        self.policy_starting = False
        self.policy_generation = 0
        self.fresh_carrier_required = False
        self.run_stable = True
        self.owner_ready = True
        self.revoked = []

    def clear(self):
        if self.generation:
            # Model the copied generation-only callback after the leaf lock.
            self.revoked.append(self.generation)
        if self.policy_generation == self.generation:
            self.policy_generation = 0
        self.phase = self.NONE
        self.generation = 0
        self.epoch = 0
        self.identity = None

    def publish(self, identity):
        if not self.run_stable or self.join_active or self.policy_starting or self.phase == self.BOUND or self.phase not in {
                self.NONE, self.PENDING, self.SCAN_ISSUED} or \
                self.next_generation == (1 << 64) - 1:
            return 0
        if self.phase != self.NONE:
            self.clear()
        self.next_generation += 1
        self.generation = self.next_generation
        self.phase = self.PENDING
        self.epoch = 0
        self.identity = identity
        return self.generation

    def clear_if_generation(self, generation):
        if generation == 0 or generation != self.generation or self.phase == self.NONE:
            return False
        self.clear()
        return True

    def resume(self):
        if not self.resume_begin():
            return False
        return self.driver_fresh_started() == self.RESUME_STARTED

    def resume_begin(self):
        if not self.owner_ready or self.join_active or self.policy_starting or self.phase != self.PENDING:
            self.clear()
            return False
        self.phase = self.SCAN_STARTING
        return True

    def driver_fresh_started(self):
        if not self.owner_ready or self.phase != self.SCAN_STARTING:
            return self.RESUME_FAILED
        self.phase = self.SCAN_ISSUED
        return self.RESUME_STARTED

    def driver_coalesced_retry(self):
        if self.phase != self.SCAN_STARTING:
            return self.RESUME_FAILED
        self.clear()
        return self.RESUME_RETRY

    def ordinary_cancel(self):
        self.policy_starting = False
        self.policy_generation = 0
        self.clear()

    def policy_begin(self, identity):
        if self.policy_starting or self.join_active or self.phase in {self.SCAN_STARTING, self.BOUND}:
            return 0
        self.policy_starting = True
        # The out-of-lock WEP teardown has no authority to publish; a real
        # cancellation clears this reservation before the second leaf check.
        if not self.run_stable:
            self.policy_starting = False
            return 0
        if self.phase != self.NONE:
            self.clear()
        self.next_generation += 1
        self.generation = self.next_generation
        self.phase = self.PENDING
        self.identity = identity
        self.policy_generation = self.generation
        self.fresh_carrier_required = False
        self.policy_starting = False
        return self.generation

    def retire_direct_run_owner(self):
        if self.phase == self.BOUND and self.generation and \
                self.policy_generation == self.generation:
            self.fresh_carrier_required = True
        self.policy_generation = 0
        self.clear()

    def fresh_carrier_accepted(self):
        self.clear()
        self.fresh_carrier_required = False

    def join_begin(self):
        if self.policy_starting:
            return False
        self.join_active = True
        return True

    def policy_held_for_scan(self):
        return self.policy_starting or (
            self.phase in {self.PENDING, self.SCAN_STARTING} and self.generation != 0 and
            self.policy_generation == self.generation)

    def policy_selects_scan(self):
        return (not self.policy_starting and self.phase == self.SCAN_ISSUED and
                self.generation != 0 and self.policy_generation == self.generation)

    def controlled_replacement(self):
        if self.phase in {self.PENDING, self.SCAN_ISSUED}:
            self.epoch = 0
            return self.phase == self.SCAN_ISSUED
        self.ordinary_cancel()
        return False

    def bind(self, epoch, identity, profile):
        if not self.owner_ready or self.phase != self.SCAN_ISSUED or identity != self.identity or \
                profile not in {"pure", "transition"}:
            self.ordinary_cancel()
            return False
        self.phase = self.BOUND
        self.epoch = epoch
        return True

    def bound_current(self, epoch, identity):
        return self.owner_ready and self.phase == self.BOUND and self.epoch == epoch and \
            self.identity == identity

    def copyout_bound(self, expected_generation=0):
        if not self.owner_ready or self.phase != self.BOUND or \
                (expected_generation and expected_generation != self.generation):
            return None
        bssid, ssid = self.identity
        return {
            "generation": self.generation,
            "epoch": self.epoch,
            "bssid": bssid,
            "ssid": ssid,
            "profile": "transition",
        }

    def auth_owner_state(self, epoch, identity):
        if self.phase == self.NONE:
            return self.OWNER_REJECTED if self.fresh_carrier_required \
                else self.OWNER_NONE
        if self.owner_ready and self.phase == self.BOUND and \
                self.epoch == epoch and self.identity == identity:
            return self.OWNER_READY
        self.clear()
        return self.OWNER_REJECTED


model = RequestModel()
target_a = (bytes.fromhex("021122334455"), b"alpha")
target_b = (bytes.fromhex("021122334466"), b"beta")

coalesced = RequestModel()
coalesced_generation = coalesced.policy_begin(target_a)
assert coalesced_generation == 1
assert coalesced.resume_begin()
assert coalesced.phase == coalesced.SCAN_STARTING
assert coalesced.policy_held_for_scan() and not coalesced.policy_selects_scan()
assert coalesced.publish(target_b) == 0
assert coalesced.policy_begin(target_b) == 0
assert coalesced.driver_coalesced_retry() == coalesced.RESUME_RETRY
assert coalesced.phase == coalesced.NONE and coalesced.revoked == [coalesced_generation]

fresh = RequestModel()
fresh_generation = fresh.policy_begin(target_a)
assert fresh_generation == 1 and fresh.resume_begin()
assert fresh.policy_held_for_scan() and not fresh.policy_selects_scan()
assert fresh.driver_fresh_started() == fresh.RESUME_STARTED
assert not fresh.policy_held_for_scan() and fresh.policy_selects_scan()

generation_a = model.publish(target_a)
assert generation_a == 1
generation_b = model.publish(target_b)
assert generation_b == 2
assert model.revoked == [generation_a]
assert not model.clear_if_generation(generation_a)
assert model.generation == generation_b and model.phase == model.PENDING
assert model.resume()
generation_a2 = model.publish(target_a)
assert generation_a2 == 3
assert model.revoked == [generation_a, generation_b]
assert model.generation == generation_a2 and model.phase == model.PENDING
assert not model.clear_if_generation(generation_b)
assert model.resume()
assert model.controlled_replacement()
assert model.bind(7, target_a, "transition")
assert model.bound_current(7, target_a)
first_owner = model.copyout_bound(0)
assert first_owner and first_owner["generation"] == generation_a2
assert first_owner["epoch"] == 7 and first_owner["bssid"] == target_a[0]
assert model.copyout_bound(generation_a2)
assert model.copyout_bound(generation_a2 + 1) is None
assert model.publish(target_b) == 0
assert model.generation == generation_a2 and model.phase == model.BOUND
assert model.clear_if_generation(generation_a2)
assert model.revoked[-1] == generation_a2
generation_b2 = model.publish(target_b)
assert generation_b2 == 4
assert not model.controlled_replacement()
assert not model.bind(8, target_b, "transition")
assert model.phase == model.NONE
assert model.revoked[-1] == generation_b2
generation_b2 = model.publish(target_b)
assert generation_b2 == 5
assert model.resume()
assert not model.bind(8, target_a, "pure")
assert model.phase == model.NONE
assert model.revoked[-1] == generation_b2
model.join_active = True
assert model.publish(target_a) == 0
model.join_active = False
model.policy_starting = True
assert model.publish(target_a) == 0
assert not model.join_begin()
assert not model.resume()
model.policy_starting = False
generation_c = model.publish(target_a)
assert generation_c == 6
model.join_active = True
assert not model.resume()
assert model.phase == model.NONE
assert model.revoked[-1] == generation_c
model.join_active = False
generation_d = model.publish(target_b)
assert generation_d == 7
model.owner_ready = False
assert not model.resume()
assert model.revoked[-1] == generation_d
model.owner_ready = True
generation_e = model.publish(target_a)
assert generation_e == 8
assert model.resume() and model.controlled_replacement()
assert model.bind(9, target_a, "transition")
model.owner_ready = False
assert not model.bound_current(9, target_a)
assert model.auth_owner_state(9, target_a) == model.OWNER_REJECTED
assert model.phase == model.NONE and model.revoked[-1] == generation_e
model.owner_ready = True
model.run_stable = False
assert model.publish(target_a) == 0
model.run_stable = True
model.next_generation = (1 << 64) - 1
assert model.publish(target_b) == 0

reservation = RequestModel()
reservation.join_active = True
assert reservation.policy_begin(target_a) == 0
reservation.join_active = False
policy_generation = reservation.policy_begin(target_a)
assert policy_generation == 1 and reservation.policy_held_for_scan()
assert not reservation.policy_selects_scan()
assert reservation.resume()
assert not reservation.policy_held_for_scan() and reservation.policy_selects_scan()
reservation.ordinary_cancel()
assert not reservation.policy_held_for_scan() and not reservation.policy_selects_scan()

restart = RequestModel()
restart_generation = restart.policy_begin(target_a)
assert restart_generation == 1
assert restart.resume() and restart.controlled_replacement()
assert restart.bind(11, target_a, "pure")
restart.retire_direct_run_owner()
assert restart.phase == restart.NONE and restart.fresh_carrier_required
assert restart.auth_owner_state(12, target_a) == restart.OWNER_REJECTED
restart.fresh_carrier_accepted()
assert restart.auth_owner_state(12, target_a) == restart.OWNER_NONE
replacement_generation = restart.policy_begin(target_a)
assert replacement_generation == 2 and not restart.fresh_carrier_required

superseded = RequestModel()
superseded_generation = superseded.policy_begin(target_a)
assert superseded_generation == 1
assert superseded.resume() and superseded.controlled_replacement()
assert superseded.bind(13, target_a, "pure")
superseded.fresh_carrier_accepted()
assert superseded.phase == superseded.NONE
assert superseded.policy_generation == 0
assert superseded.revoked == [superseded_generation]
assert not superseded.fresh_carrier_required

print("net80211 direct-WCL SAE request contract: passed")
PY
