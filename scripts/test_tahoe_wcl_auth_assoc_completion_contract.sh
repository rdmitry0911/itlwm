#!/usr/bin/env bash
# Static regression gate for Tahoe's two-stage association boundary:
# generic Core status (0x4e/0x08), then an exact selected-BSS JoinAdapter
# completion (0xd3/0x1c) owned by either WCL or public IOC_ASSOCIATE.
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

python3 - "$root" <<'PY'
from pathlib import Path
import sys

root = Path(sys.argv[1])
v2 = (root / "AirportItlwm/AirportItlwmV2.cpp").read_text()
v2_header = (root / "AirportItlwm/AirportItlwmV2.hpp").read_text()
sky = (root / "AirportItlwm/AirportItlwmSkywalkInterface.cpp").read_text()
owner = (root / "AirportItlwm/TahoeOwnerRegistry.hpp").read_text()
open_resume = (
    root / "AirportItlwm/TahoeWclOpenScanResumeContracts.hpp"
).read_text()
header = (root / "include/Airport/apple80211_var.h").read_text()
parity = (root / "AirportItlwm/TahoePayloadParity.hpp").read_text()
unit = (root / "tests/tahoe_payload_builders_test.cpp").read_text()
net_var = (
    root / "itl80211/openbsd/net80211/ieee80211_var.h"
).read_text()
net_input = (
    root / "itl80211/openbsd/net80211/ieee80211_input.c"
).read_text()
net_proto = (
    root / "itl80211/openbsd/net80211/ieee80211_proto.c"
).read_text()


def fail(message):
    raise SystemExit(f"Tahoe WCL auth/assoc completion: {message}")


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


def between(text, begin, end, label):
    start = text.find(begin)
    if start < 0:
        fail(f"missing start for {label}: {begin}")
    finish = text.find(end, start)
    if finish < 0:
        fail(f"missing end for {label}: {end}")
    return text[start:finish]


# ABI: generic status and JoinManager completion are intentionally distinct.
for token in (
        "APPLE80211_WCL_ASSOC_STATUS_LEN 8",
        "struct apple80211_wcl_assoc_status_event",
        "APPLE80211_WCL_AUTH_ASSOC_COMPLETE_LEN 0x1c",
        "struct apple80211_wcl_auth_assoc_complete_event",
        "APPLE80211_M_WCL_AUTH_ASSOC_EVENT    78",
        "APPLE80211_M_WCL_AUTH_ASSOC_COMPLETE 211",
        "authAssocCompleteEventHandler accepts this exact 0x1c",
        "independent 0x4e association-status carrier above must be sent first",
):
    require(header, token, "two-carrier header ABI")

for token in (
        "secondary_state) == 0x02",
        "auth_seen) == 0x04",
        "bssid) == 0x05",
        "auth_status) == 0x0c",
        "assoc_reason) == 0x18",
):
    require(header, token, "0xd3 packed layout assertion")

for token in (
        '"wcl-assoc-status"',
        '"wcl-auth-assoc-complete"',
        "0x08",
        "0x1c",
        "postTahoeWclAuthAssocCompleteGated",
):
    require(parity, token, "payload parity inventory")

for token in (
        "WCL association-status uses AppleBCMWLAN handleAssocEvent selector 0x4e",
        "WCLJoinManager completion uses its distinct selector 0xd3",
        "WCL join auth/assoc completion is exactly 0x1c bytes",
        "successful join completion has the reference auth and assoc result tuple",
):
    require(unit, token, "standalone ABI regression")

# The early association callback is status-plane only.  It must not construct
# 0xd3 before mandatory rates/IE parsing has validated the response.
assoc_case = between(v2,
                     "case IEEE80211_EVT_STA_ASSOC_DONE:",
                     "case IEEE80211_EVT_STA_AUTH_DONE:",
                     "STA_ASSOC_DONE case")
ordered(assoc_case, "early status-only publication",
        "buildTahoeWclAssocStatusPayload(0, 0, &assocStatus)",
        "APPLE80211_M_WCL_AUTH_ASSOC_EVENT",
        "return;")
for token in (
        "postTahoeWclAuthAssocCompleteGated",
        "APPLE80211_M_WCL_AUTH_ASSOC_COMPLETE",
        "captureTahoeWclSelectedBssRequest",
):
    forbid(assoc_case, token, "0xd3 work in early STA_ASSOC_DONE")

# A real successful authentication response records the exact selected-BSS
# epoch before the transition to ASSOC.
auth_case = between(v2,
                    "case IEEE80211_EVT_STA_AUTH_DONE:",
                    "case IEEE80211_EVT_STA_ASSOC_VALIDATED:",
                    "STA_AUTH_DONE case")
for token in (
        "captureTahoeWclSelectedBssRequest(",
        "IEEE80211_S_AUTH",
        "recordTahoeWclAuthSuccessGated",
        "SUCCESS_LEDGER",
):
    require(auth_case, token, "authentication success ledger")

auth_recorder = body(v2, "static IOReturn recordTahoeWclAuthSuccessGated(",
                     "gated authentication success recorder")
ordered(auth_recorder, "authentication identity before ledger publication",
        "ic->ic_state != IEEE80211_S_AUTH",
        "ieee80211_pae_assoc_epoch_current(ic)",
        "ieee80211_pae_selected_bss_copyout_current",
        "tahoeWclSelectedBssMatchesOwner",
        "owner->authSuccessRecorded = true",
        "owner->authSuccessEpoch = request->associationEpoch",
        "owner->authSuccessBssid")

# Only the later, fully parsed association callback may consume that ledger and
# publish the candidate-matched 0xd3.
validated_case = between(v2,
                         "case IEEE80211_EVT_STA_ASSOC_VALIDATED:",
                         "case IEEE80211_EVT_STA_OPEN_RUN_DONE:",
                         "STA_ASSOC_VALIDATED case")
ordered(validated_case, "validated association completion",
        "captureTahoeWclSelectedBssRequest(",
        "IEEE80211_S_ASSOC",
        "gate->runAction(postTahoeWclAuthAssocCompleteGated",
        "VALIDATED_COMPLETION",
        "return;")

# Capture is parameterized by the exact AUTH/ASSOC state and carries a value
# snapshot tied to the exact selected-BSS epoch.
capture = body(v2, "static bool captureTahoeWclSelectedBssRequest(",
               "completion snapshot capture")
for token in (
        "ic->ic_state != expectedState",
        "ieee80211_pae_assoc_epoch_current(ic)",
        "ieee80211_pae_selected_bss_copyout_current",
        "IEEE80211_ADDR_EQ(request->selected.bssid, ic->ic_bss->ni_bssid)",
):
    require(capture, token, "selected-BSS capture gate")

# The owner match prevents an ownerless association, alternate candidate,
# stale owner, or duplicate publication from entering the JoinManager path.
identity_match = body(v2, "static bool tahoeWclSelectedBssMatchesOwner(",
                      "candidate identity matcher")
for token in (
        "owner.hasCarrier",
        "owner.selectedFromCandidate",
        "owner.authAssocCompletionArmed",
        "owner.candidateCount == 0",
        "selected.ssid_len != owner.ssidLength",
        "owner.selectedBssid",
        "owner.candidateBssid",
        "memcmp(selected.ssid, owner.ssid, selected.ssid_len)",
):
    require(identity_match, token, "candidate identity fence")

matches = body(v2, "static bool tahoeWclAuthAssocCompletionMatchesOwner(",
               "authenticated candidate owner matcher")
for token in (
        "tahoeWclSelectedBssMatchesOwner",
        "owner.authSuccessRecorded",
        "owner.authSuccessEpoch == selected.epoch",
        "owner.authSuccessBssid",
        "!owner.authAssocCompletionPublished",
):
    require(matches, token, "authenticated completion fence")

publisher = body(v2, "static IOReturn postTahoeWclAuthAssocCompleteGated(",
                 "gated 0xd3 publisher")
ordered(publisher, "gated completion validation before publication",
        "ic->ic_state != IEEE80211_S_ASSOC",
        "ieee80211_pae_assoc_epoch_current(ic) != request->associationEpoch",
        "ieee80211_pae_selected_bss_copyout_current",
        "ieee80211_pae_selected_bss_identity_matches",
        "owner = &registry.association",
        "owner = &registry.publicAssociation",
        "buildTahoeWclAuthAssocCompletePayload",
        "owner->authAssocCompletionPublished = true",
        "APPLE80211_M_WCL_AUTH_ASSOC_COMPLETE")
for token in (
        "sizeof(payload)",
        "if (result != kIOReturnSuccess)",
        "owner->authAssocCompletionPublished = false",
):
    require(publisher, token, "one-shot completion publication")
forbid(publisher, "!owner.publicCarrier",
       "public completion exclusion from common publisher")

# Open networks have no key-done edge.  Their exact post-RUN owner publishes
# WCL link-up/connect-complete once, without fabricating RSN key completion.
open_case = between(v2,
                    "case IEEE80211_EVT_STA_OPEN_RUN_DONE:",
                    "case IEEE80211_EVT_STA_RSN_HANDSHAKE_DONE:",
                    "STA_OPEN_RUN_DONE case")
for token in (
        "postTahoeWclOpenJoinCompletionGated",
        "RUN_COMPLETION",
        "return;",
):
    require(open_case, token, "open RUN completion dispatch")

open_completion = body(
    v2, "static IOReturn postTahoeWclOpenJoinCompletionGated(",
    "gated open RUN completion")
ordered(open_completion, "open completion exact owner before publication",
        "ic->ic_state != IEEE80211_S_RUN",
        "IEEE80211_F_RSNON",
        "ieee80211_pae_selected_bss_copyout_current",
        "tahoeWclOpenJoinCompletionMatchesOwner",
        "owner->connectCompletionPublished = true",
        "postTahoeWclLinkUpInd",
        "postTahoeWclConnectCompleteEvent")
open_match = body(
    v2, "static bool tahoeWclOpenJoinCompletionMatchesOwner(",
    "open completion owner matcher")
for token in (
        "tahoeWclSelectedBssMatchesOwner",
        "owner.authLower == APPLE80211_AUTHTYPE_OPEN",
        "owner.authUpper == APPLE80211_AUTHTYPE_NONE",
        "owner.rsnIeLength == 0",
        "owner.authSuccessRecorded",
        "owner.authSuccessEpoch == selected.epoch",
        "owner.authSuccessBssid",
        "owner.authAssocCompletionPublished",
        "!owner.connectCompletionPublished",
):
    require(open_match, token, "open completion owner fence")
for token in (
        "postRsnHandshakeDoneGated",
        "APPLE80211_M_RSN_HANDSHAKE_DONE",
        "handleKeyDone",
):
    forbid(open_completion, token, "fabricated key completion on open RUN")

for token in (
        "IEEE80211_EVT_STA_AUTH_DONE               18",
        "IEEE80211_EVT_STA_ASSOC_VALIDATED         19",
        "IEEE80211_EVT_STA_OPEN_RUN_DONE           20",
):
    require(net_var, token, "split net80211 completion event")
ordered(net_input, "validated association after mandatory setup",
        "/* supported rates element is mandatory */",
        "ieee80211_setup_rates",
        "IEEE80211_EVT_STA_ASSOC_VALIDATED",
        "ieee80211_new_state(ic, IEEE80211_S_RUN")
ordered(net_proto, "Open-System auth ledger before ASSOC",
        "if (status != 0)",
        "IEEE80211_EVT_STA_AUTH_DONE",
        "ieee80211_new_state(ic, IEEE80211_S_ASSOC")
ordered(net_proto, "open RUN after real link state",
        "ieee80211_set_link_state(ic, LINK_STATE_UP)",
        "IEEE80211_EVT_STA_OPEN_RUN_DONE")

deauth_case = between(v2,
                      "case IEEE80211_EVT_STA_DEAUTH:",
                      "case IEEE80211_EVT_SCAN_DONE:",
                      "STA_DEAUTH case")
require(deauth_case, "clearTahoeWclAuthAssocCompletionLeaseGated",
        "deauthentication completion-lease clear")
deauth_clear = body(v2, "static IOReturn clearTahoeWclAuthAssocCompletionLeaseGated(",
                    "deauthentication lease clear action")
for token in ("registry.association =", "registry.publicAssociation ="):
    require(deauth_clear, token, "deauthentication clears both leases")

clear = body(sky, "clearExternalPmkEligibilityLocked(const char *reason_tag)",
             "shared association lifecycle clear")
require(clear, "getTahoeOwnerRegistry().association =",
        "PMK maintenance clears WCL candidate owner")
forbid(clear, "publicAssociation",
       "PMK maintenance cancellation of public completion lease")

public_assoc = body(sky, "setASSOCIATE(struct apple80211_assoc_data *ad)",
                    "public association setter")
require(public_assoc, "registry.association =",
        "public association clears old WCL owner")
ordered(public_assoc, "public completion lease precedes scan resume",
        "tahoePublicAssociationOwnerMatchesRequest(",
        "if (instance != nullptr && !preservePublicCompletionOwner)",
        "registry.publicAssociation =",
        "assocResult = associateSSID(",
        "tahoeBuildPublicAssociationOwner(ad, &publicOwner)",
        "getTahoeOwnerRegistry().publicAssociation =",
        "ieee80211_new_state(")

public_match = body(
    sky, "tahoePublicAssociationOwnerMatchesRequest(",
    "public duplicate-owner matcher")
for token in (
        "owner.publicCarrier",
        "owner.authAssocCompletionArmed",
        "owner.apMode != request->ad_mode",
        "owner.authLower != request->ad_auth_lower",
        "owner.authUpper != request->ad_auth_upper",
        "owner.ssidLength != request->ad_ssid_len",
        "owner.rsnIeLength != request->ad_rsn_ie_len",
        "owner.selectedBssid",
        "owner.candidateBssid",
        "memcmp(owner.ssid, request->ad_ssid",
):
    require(public_match, token, "exact public duplicate fence")

public_build = body(
    sky, "tahoeBuildPublicAssociationOwner(",
    "public completion-owner builder")
for token in (
        "request->ad_mode != APPLE80211_AP_MODE_INFRA",
        "TahoeScanContracts::hasRenderableBssid(request->ad_bssid.octet)",
        "owner->hasCarrier = true",
        "owner->publicCarrier = true",
        "owner->selectedFromCandidate = true",
        "owner->authAssocCompletionArmed = true",
        "owner->candidateCount = 1",
        "owner->selectedBssid",
        "owner->candidateBssid",
):
    require(public_build, token, "public JoinAdapter completion lease")

wcl_assoc = body(sky, "setWCL_ASSOCIATEImpl(apple80211AssocCandidates *candidates)",
                 "WCL association setter")
for token in (
        "A replacement WCL carrier starts a new WCL candidate ledger",
        "tahoePublicAssociationOwnerMatchesWclIdentity(",
        "getTahoeOwnerRegistry().publicAssociation =",
        "associationOwner.authAssocCompletionArmed = true",
        "associationOwner.authAssocCompletionPublished = false",
):
    require(wcl_assoc, token, "WCL lease lifecycle")
ordered(wcl_assoc, "same-identity public/WCL lease preservation",
        "getTahoeOwnerRegistry().association =",
        "tahoePublicAssociationOwnerMatchesWclIdentity(",
        "getTahoeOwnerRegistry().publicAssociation =")

# Tahoe 25C56 calls resetAutoCountry before touching the WCL candidate and
# propagates a non-zero firmware/config result.  Intel has no corresponding
# Broadcom iovar, so the controller preflight uses the already-owned PowerOn
# availability epoch.  Both the lifecycle fence and lower SCAN fence precede
# replacement of the completion owner or parsing of candidate fields.
require(v2_header, "prepareTahoeWclAssociationBackend() const;",
        "association backend preflight declaration")
backend_preflight = body(
    v2, "IOReturn AirportItlwm::prepareTahoeWclAssociationBackend() const",
    "association backend preflight")
for token in (
        "power_state == kWiFiPowerOn",
        "kAirportItlwmPmBootInProgressBit",
        "kAirportItlwmPmPermanentFailureBit",
        "kAirportItlwmPmDriverAvailabilityPendingBit",
        "(lifecycleState & unavailableMask) == 0",
        "NOT_READY: power_state=%u pm_flags=0x%x hal=%p",
        "return kIOReturnNotReady;",
        "return kIOReturnSuccess;",
):
    require(backend_preflight, token, "reference-aligned backend preflight")

ordered(wcl_assoc, "preflight before candidate mutation",
        "prepareTahoeWclAssociationBackend()",
        "if (backendResult != kIOReturnSuccess)",
        "return backendResult;",
        "fHalService->get80211Controller()",
        "ic->ic_state < IEEE80211_S_SCAN",
        "return kIOReturnNotReady;",
        "getTahoeOwnerRegistry().association =",
        "reinterpret_cast<const uint8_t *>(candidates)")

early_power_on = body(
    wcl_assoc, "if (backendResult != kIOReturnSuccess)",
    "early PowerOn WCL association")
forbid(early_power_on, "kIOReturnSuccess;",
       "false success for an unretained early WCL association")

ordered(wcl_assoc, "open WCL completion lease precedes direct join or scan fallback",
        "const TahoeWclOpenScanResumeContracts::Facts openScanResumeFacts",
        "shouldResumeScanAfterOpenAssociation(openScanResumeFacts)",
        "associationOwner.authAssocCompletionArmed = true",
        "getTahoeOwnerRegistry().association =",
        "tahoeJoinCachedWclCandidate(",
        "ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);")
for token in (
        "ap_mode == APPLE80211_AP_MODE_INFRA",
        "auth_lower == APPLE80211_AUTHTYPE_OPEN",
        "auth_upper == APPLE80211_AUTHTYPE_NONE",
        "wcl_key_len == 0",
        "rsn_ie_len == 0",
        "candidate_count > 0",
        "TahoeScanContracts::hasRenderableBssid(bssid->octet)",
        "associationOwner.selectedFromCandidate",
        "associationScanOwnersIdle()",
        "OPEN_READY_SCAN_RESUME",
):
    require(wcl_assoc, token, "fail-closed open WCL direct join/fallback")
require(sky, "CACHED_CANDIDATE_DIRECT_JOIN",
        "cached-candidate direct join marker")

open_predicate = body(
    open_resume, "constexpr bool shouldResumeScanAfterOpenAssociation",
    "pure open WCL scan-resume predicate")
for token in (
        "facts.associationAccepted",
        "facts.infrastructureMode",
        "facts.openAuthLower",
        "facts.noUpperAuth",
        "facts.noCredential",
        "facts.noRsnIe",
        "facts.stateIsScan",
        "facts.hasSelectedCandidate",
        "facts.selectedBssidRenderable",
):
    require(open_predicate, token, "complete open WCL admission fence")
for token in (
        "ieee80211_node_choose_bss",
        "IEEE80211_S_AUTH",
        "IEEE80211_SEND_MGMT",
):
    forbid(open_predicate, token, "lower-layer shortcut in pure open predicate")

wcl_public_match = body(
    sky, "tahoePublicAssociationOwnerMatchesWclIdentity(",
    "public/WCL duplicate identity matcher")
for token in (
        "owner.publicCarrier",
        "owner.authAssocCompletionArmed",
        "owner.apMode != apMode",
        "owner.authLower != authLower",
        "owner.authUpper != authUpper",
        "owner.ssidLength != ssidLength",
        "owner.selectedBssid",
        "owner.candidateBssid",
        "memcmp(owner.ssid, ssid, ssidLength)",
):
    require(wcl_public_match, token, "exact public/WCL duplicate fence")

reassoc = body(sky, "setWCL_REASSOC(apple80211_reassoc *data)",
               "WCL reassociation setter")
for token in (
        "getTahoeOwnerRegistry().association =",
        "getTahoeOwnerRegistry().publicAssociation =",
):
    require(reassoc, token, "reassociation retires both completion leases")

abort = body(sky, "setWCL_JOIN_ABORT(apple80211_wcl_abort_join *data)",
             "WCL join abort setter")
ordered(abort, "join abort clears both completion leases",
        "getTahoeOwnerRegistry().publicAssociation =",
        'clearExternalPmkEligibilityLocked("setWCL_JOIN_ABORT")')

for marker, label in (
        ("setDISASSOCIATE(void *ad)", "public disassociate"),
        ("setWCL_LEAVE_NETWORK(apple80211_leave_network *data)", "WCL leave"),
):
    current = body(sky, marker, label)
    require(current, "getTahoeOwnerRegistry().publicAssociation =",
            f"{label} clears public completion lease")

pmksa = body(sky, "setCLEAR_PMKSA_CACHE(void *req)", "PMKSA cache clear")
require(pmksa, 'clearExternalPmkEligibilityLocked("setCLEAR_PMKSA_CACHE")',
        "PMKSA key-state reset")
forbid(pmksa, "publicAssociation",
       "PMKSA cache clear cancellation of public completion")

for token in (
        "publicCarrier",
        "AssociationOwner publicAssociation",
        "authAssocCompletionArmed",
        "authAssocCompletionPublished",
        "authSuccessRecorded",
        "authSuccessEpoch",
        "authSuccessBssid",
        "connectCompletionPublished",
        "selectedBssid",
        "candidateBssid",
):
    require(owner, token, "association owner state")

print("Tahoe WCL auth/assoc completion contract: PASS")
PY
