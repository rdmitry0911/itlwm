#!/usr/bin/env bash
# Static contract for the lab-gated direct-SAE PMK bridge.  It proves the
# in-kext Confirm -> local PAE -> exact Association Request ownership path;
# it deliberately does not claim that a physical AP completed 4-way, DHCP,
# traffic, rekey, or roaming.
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

python3 - "$root" <<'PY'
from pathlib import Path
import re
import sys


root = Path(sys.argv[1])
abi = (root / "include/HAL/ItlSaePmkContinuationV1.h").read_text()
engine_h = (root / "itl80211/openbsd/net80211/ieee80211_sae_engine.h").read_text()
engine_c = (root / "itl80211/openbsd/net80211/ieee80211_sae_engine.c").read_text()
proto_h = (root / "itl80211/openbsd/net80211/ieee80211_proto.h").read_text()
proto_c = (root / "itl80211/openbsd/net80211/ieee80211_proto.c").read_text()
var_h = (root / "itl80211/openbsd/net80211/ieee80211_var.h").read_text()
ifattach_c = (root / "itl80211/openbsd/net80211/ieee80211.c").read_text()
pae_c = (root / "itl80211/openbsd/net80211/ieee80211_pae_input.c").read_text()
output_c = (root / "itl80211/openbsd/net80211/ieee80211_output.c").read_text()
iwn = (root / "itlwm/hal_iwn/ItlIwn.cpp").read_text()
iwn_var = (root / "itlwm/hal_iwn/if_iwnvar.h").read_text()
build = (root / "scripts/build_tahoe.sh").read_text()


def fail(message: str) -> None:
    raise SystemExit(f"IWN SAE PMK continuation contract: {message}")


def require(text: str, needle: str, label: str) -> None:
    if needle not in text:
        fail(f"missing {label}: {needle}")


def forbid(text: str, needle: str, label: str) -> None:
    if needle in text:
        fail(f"unexpected {label}: {needle}")


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


def body(source: str, name: str, label: str) -> str:
    pattern = re.compile(r"\b" + re.escape(name) +
                         r"\s*\([^;{}]*\)\s*\{", re.S)
    match = pattern.search(source)
    if match is None:
        fail(f"missing {label}")
    return block_after(source, source.rfind("{", match.start(), match.end()),
                       label)


def iwn_method(name: str) -> str:
    pattern = re.compile(r"\bItlIwn\s*::\s*" + re.escape(name) +
                         r"\s*\([^;{}]*\)\s*\{", re.S)
    match = pattern.search(iwn)
    if match is None:
        fail(f"missing ItlIwn::{name}()")
    return block_after(iwn, iwn.rfind("{", match.start(), match.end()),
                       f"ItlIwn::{name}")


def ordered(text: str, label: str, *needles: str) -> None:
    cursor = 0
    for needle in needles:
        index = text.find(needle, cursor)
        if index < 0:
            fail(f"{label} missing ordered token: {needle}")
        cursor = index + len(needle)


def strip_comments(source: str) -> str:
    return re.sub(r"/\*.*?\*/|//[^\n]*", "", source, flags=re.S)


# The continuation is a private in-kext secret handoff with a public-only
# cancellation identity.  It must remain outside all UserClient/controller
# and PLTI payloads.
for token in (
        "Private SAE PMK continuation ABI",
        "never a UserClient ABI, controller/mailbox payload",
        "struct ItlSaePmkContinuationIdentityV1",
        "struct ItlSaePmkContinuationV1",
        "itl_sae_pmk_continuation_is_well_formed",
        "itl_sae_pmk_continuation_identity_is_well_formed"):
    require(abi, token, "private continuation ABI")
for token in ("password", "pwe", "kck", "controller_nonce", "client_cookie"):
    forbid(abi, token, "continuation ABI secret/controller surface")
require(engine_h, "ieee80211_sae_engine_derive_rsn_pmkid",
        "engine PMK Name export")
derive = body(engine_c, "ieee80211_sae_engine_derive_rsn_pmkid",
              "engine PMK Name derivation")
ordered(derive, "PMK Name derivation", '"PMK Name"', "hmac_sha256_vector",
        "os_memcpy(pmkid", "ieee80211_sae_secure_zero")

# The local generic claim copies a verified PMK only into the pre-existing
# PAE store, preserves SAE rather than PSK policy, and records a public
# one-shot identity.  It is neither an external PMK installer nor a WCL/PLTI
# association route.
claim = body(proto_c, "ieee80211_sae_wcl_request_pmk_claim_locked",
             "local SAE PMK claim")
for token in (
        "itl_sae_pmk_continuation_is_well_formed",
        "timingsafe_bcmp(continuation->pmkid, canonical_pmkid",
        "IEEE80211_S_AUTH",
        "ic->ic_sae_peer_rx_admission",
        "IEEE80211_F_PSK",
        "memcpy(ic->ic_psk, continuation->pmk",
        "ic->ic_external_pmk_owner = 0",
        "memcpy(ni->ni_pmkid, canonical_pmkid",
        "IEEE80211_NODE_PMKID",
        "claim->event_sequence",
        "claim->active = 1"):
    require(claim, token, "local PMK claim fence")
for token in ("installExternalPmkLocked", "DeliverPMK", "setwpaparms",
              "associateSSID", "PLTI", "Agent"):
    forbid(strip_comments(claim), token, "controller/external PMK detour")
require(claim, "ic->ic_flags &= ~IEEE80211_F_PSK;",
        "SAE leaves legacy PSK policy clear")
base = body(proto_c, "ieee80211_sae_wcl_request_pmk_base_current_locked",
            "PMK claim current-BSS predicate")
for token in ("IEEE80211_AKM_SAE", "IEEE80211_CIPHER_CCMP",
              "IEEE80211_F_MFPR", "IEEE80211_C_MFP",
              "IEEE80211_NODE_MFP", "ic_pae_mfp_txn_submit",
              "ic_pae_mfp_txn_cancel", "ic_pae_mfp_txn_finish"):
    require(base, token, "full PMF continuation predicate")
peer_admit = body(proto_c, "ieee80211_sae_wcl_peer_rx_admit",
                  "raw SAE peer admission")
forbid(peer_admit, "IEEE80211_C_MFP",
       "raw Commit/Confirm MFP capability gate")

# Generic RSN output and PAE M1 retain their existing local-PMK mechanism.
# SAE reaches it with F_PSK clear, not through a special controller branch.
for token in ("ic_psk", "memcpy(ni->ni_pmk, ic->ic_psk",
              "IEEE80211_NODE_PMK", "ieee80211_derive_ptk",
              "ieee80211_send_4way_msg2"):
    require(pae_c, token, "local PAE first-M1 route")
require(output_c, "if (ni->ni_flags & IEEE80211_NODE_PMKID)",
        "RSN PMKID emission")

# Claim cleanup and attach initialization ensure no later association inherits
# local key material or a stale PMK Name.
policy_clear = body(proto_c, "ieee80211_sae_wcl_request_policy_clear_locked",
                    "direct SAE policy cleanup")
for token in ("explicit_bzero(ni->ni_pmk", "explicit_bzero(ni->ni_pmkid",
              "IEEE80211_NODE_PMK | IEEE80211_NODE_PMKID",
              "explicit_bzero(&ic->ic_sae_wcl_pmk_claim",
              "explicit_bzero(ic->ic_psk"):
    require(policy_clear, token, "PMK cleanup")
require(ifattach_c, "memset(&ic->ic_sae_wcl_pmk_claim, 0",
        "PMK claim attach initialization")
require(var_h, "struct ieee80211_sae_wcl_pmk_claim",
        "public generic claim storage")

# Only the private sentinel can enter S_ASSOC; it reconstructs only public
# identity under the selected-BSS leaf and fails back to SCAN before generic
# code can enqueue ASSOC_REQ.  Ordinary state transitions remain unchanged.
require(proto_h, "IEEE80211_SAE_WCL_MGMT_PMK_CONTINUE",
        "private PMK continuation sentinel")
sentinel = body(proto_c,
                "ieee80211_sae_wcl_request_pmk_claim_assoc_sentinel_current",
                "generic S_ASSOC sentinel check")
ordered(sentinel, "sentinel public identity reconstruction",
        "IOSimpleLockLockDisableInterrupt(lock)",
        "identity.request_generation = claim->generation",
        "identity.event_sequence = claim->event_sequence",
        "ieee80211_sae_wcl_request_pmk_claim_assoc_current_locked",
        "IOSimpleLockUnlockEnableInterrupt(lock, irq)",
        "explicit_bzero(&identity")
assoc_start = proto_c.find("/* Direct SAE reaches Association")
if assoc_start < 0:
    fail("missing generic S_ASSOC continuation sentinel")
assoc_case = proto_c[assoc_start:assoc_start + 1800]
ordered(assoc_case, "direct S_ASSOC sentinel before association TX",
        "mgt == IEEE80211_SAE_WCL_MGMT_PMK_CONTINUE",
        "ostate != IEEE80211_S_AUTH",
        "ieee80211_sae_wcl_request_pmk_claim_assoc_sentinel_current",
        "ieee80211_new_state(ic, IEEE80211_S_SCAN, -1)",
        "IEEE80211_SEND_MGMT(ic, ni,",
        "IEEE80211_FC0_SUBTYPE_ASSOC_REQ")
continue_assoc = body(proto_c, "ieee80211_sae_wcl_request_pmk_continue_assoc",
                      "AUTH-to-ASSOC continuation")
ordered(continue_assoc, "direct continuation order",
        "IEEE80211_S_AUTH", "ieee80211_pae_assoc_epoch_note_newstate",
        "IEEE80211_S_ASSOC", "IEEE80211_SAE_WCL_MGMT_PMK_CONTINUE")

# Confirm completion independently derives and compares PMKID, claims the
# local PAE while holding selected-BSS -> engine leaves, then retains an
# identity-only tombstone until the real IWN descriptor is firmware-owned.
for token in ("completion_claimed", "assoc_tx_pending", "assoc_tx_accepted",
              "struct ItlSaePmkContinuationIdentityV1 completion"):
    require(iwn_var, token, "IWN association tombstone")
task = iwn_method("iwn_sae_engine_task")
ordered(task, "Confirm to local continuation",
        "IEEE80211_SAE_ENGINE_PEER_COMPLETE",
        "ieee80211_sae_engine_derive_rsn_pmkid",
        "timingsafe_bcmp(canonical_pmkid, continuation.pmkid",
        "IOSimpleLockLockDisableInterrupt(bss_lock)",
        "IOSimpleLockLock(sc->sc_sae_engine_lock)",
        "ieee80211_sae_wcl_request_pmk_claim_locked",
        "owner->completion = continuation.identity",
        "owner->completion_claimed = true",
        "owner->assoc_tx_pending = true",
        "ieee80211_sae_wcl_request_pmk_continue_assoc")
for token in ("installExternalPmkLocked", "DeliverPMK", "setwpaparms",
              "associateSSID", "PLTI", "Agent"):
    forbid(strip_comments(task), token, "IWN Confirm PMK detour")

preflight = body(iwn, "iwn_sae_engine_assoc_tx_preflight",
                 "Association Request preflight")
for token in ("IWN_SAE_ASSOC_TX_REJECTED", "owner->completion_claimed",
              "ieee80211_sae_wcl_request_pmk_claim_assoc_current_locked",
              "iwn_sae_engine_mark_cancelled_locked(sc,",
              "owner->request_generation"):
    require(preflight, token, "pre-doorbell direct SAE fence")
commit = body(iwn, "iwn_sae_engine_assoc_tx_commit",
              "Association Request descriptor fence")
ordered(commit, "final descriptor linearization",
        "IOSimpleLockLockDisableInterrupt(bss_lock)",
        "IOSimpleLockLock(sc->sc_sae_engine_lock)",
        "ieee80211_sae_wcl_request_pmk_claim_assoc_current_locked",
        "sc->ops.update_sched", "ring->cur = next_cur",
        "IWN_WRITE(sc, IWN_HBUS_TARG_WRPTR",
        "owner->assoc_tx_pending = false",
        "owner->assoc_tx_accepted = true")
for token in ("task_add", "ieee80211_new_state", "cancelSaeWclCredential"):
    forbid(commit, token, "callback/task under descriptor leaves")
tx = iwn_method("iwn_tx")
ordered(tx, "preflight before trim and final fence",
        "iwn_sae_engine_assoc_tx_preflight", "mbuf_adj(m, hdrlen)",
        "iwn_sae_engine_assoc_tx_commit", "iwn_sae_engine_schedule_task(sc)")
retire = body(iwn, "iwn_sae_engine_worker_retire", "SAE owner retirement")
require(retire, "sc->sc_ic.ic_state == IEEE80211_S_ASSOC",
        "S_ASSOC failure scan recovery")
require(task, "if (assoc_tx_accepted)",
        "accepted descriptor owner retirement")

# The bridge is compiled solely in the explicit laboratory artifact.  A
# regular build still has a false lab predicate and a fail-closed HAL ingress.
require(build, "IWN_SOFTWARE_PMF_LAB_BUILD=1", "lab compiler switch")
lab_gate = body(iwn, "iwn_sae_auth_transport_lab_opted_in",
                "direct SAE lab gate")
ordered(lab_gate, "ordinary artifact SAE gate", "#if IWN_SOFTWARE_PMF_LAB_BUILD",
        "return true;", "#else", "return false;")

print("PASS: direct SAE PMK is locally claimed, fenced through ASSOC_REQ, and remains lab-gated; on-air WPA3 is not claimed")
PY
