#ifndef AirportItlwmIwnDirectSaeTraceContracts_h
#define AirportItlwmIwnDirectSaeTraceContracts_h

/*
 * Safe, categorical evaluator for one direct IWN SAE laboratory attempt.
 *
 * This consumes the existing preallocated trace ring rather than a second
 * driver ledger.  A positive result establishes only the stated in-kext
 * path: a WCL request reached the direct SAE worker, the worker completed
 * Commit/Confirm, the verified PMK crossed its local net80211 claim, the
 * real Association descriptor reached IWN firmware ownership, the local
 * four-way exchange installed its ordered software PMF keyset, and the port
 * became valid.  It contains no identity, secret, packet, descriptor,
 * status, channel, signal, or pointer field.
 */

#include <stdint.h>

#include <ClientKit/AirportItlwmPostPltiTrace.h>

enum AirportItlwmIwnDirectSaeTraceVerdict {
    kAirportItlwmIwnDirectSaeTraceVerdictIntegrityInconclusive = 0,
    kAirportItlwmIwnDirectSaeTraceVerdictBackendUnsupported,
    kAirportItlwmIwnDirectSaeTraceVerdictBranchNotObserved,
    kAirportItlwmIwnDirectSaeTraceVerdictFreshScanNotObserved,
    kAirportItlwmIwnDirectSaeTraceVerdictRequestNoBssSelection,
    kAirportItlwmIwnDirectSaeTraceVerdictJoinBssNotObserved,
    kAirportItlwmIwnDirectSaeTraceVerdictNodeMfpNotNegotiated,
    kAirportItlwmIwnDirectSaeTraceVerdictAuthStateNotObserved,
    kAirportItlwmIwnDirectSaeTraceVerdictCommitTxNotComplete,
    kAirportItlwmIwnDirectSaeTraceVerdictPeerCommitNotAccepted,
    kAirportItlwmIwnDirectSaeTraceVerdictConfirmTxNotComplete,
    kAirportItlwmIwnDirectSaeTraceVerdictPeerConfirmNotValidated,
    kAirportItlwmIwnDirectSaeTraceVerdictPmkNotClaimed,
    kAirportItlwmIwnDirectSaeTraceVerdictAssocDescriptorNotAccepted,
    kAirportItlwmIwnDirectSaeTraceVerdictAssocExchangeNotComplete,
    kAirportItlwmIwnDirectSaeTraceVerdictFourWayNotComplete,
    kAirportItlwmIwnDirectSaeTraceVerdictPmfPtkSoftwareCcmpNotObserved,
    kAirportItlwmIwnDirectSaeTraceVerdictPmfGtkSoftwareCcmpNotObserved,
    kAirportItlwmIwnDirectSaeTraceVerdictPmfIgtkStageNotObserved,
    kAirportItlwmIwnDirectSaeTraceVerdictPmfIgtkPublicationNotObserved,
    kAirportItlwmIwnDirectSaeTraceVerdictPmfKeysetPublicationNotObserved,
    kAirportItlwmIwnDirectSaeTraceVerdictDirectSae4WayPortValid,
};

enum AirportItlwmIwnDirectSaeTraceMissingStage {
    kAirportItlwmIwnDirectSaeTraceMissingStageNone = 0,
    kAirportItlwmIwnDirectSaeTraceMissingStageCaptureSeal,
    kAirportItlwmIwnDirectSaeTraceMissingStageFreshScan,
    kAirportItlwmIwnDirectSaeTraceMissingStageBssSelection,
    kAirportItlwmIwnDirectSaeTraceMissingStageJoinBss,
    kAirportItlwmIwnDirectSaeTraceMissingStageNodeMfp,
    kAirportItlwmIwnDirectSaeTraceMissingStageAuthState,
    kAirportItlwmIwnDirectSaeTraceMissingStageCommitTx,
    kAirportItlwmIwnDirectSaeTraceMissingStagePeerCommit,
    kAirportItlwmIwnDirectSaeTraceMissingStageConfirmTx,
    kAirportItlwmIwnDirectSaeTraceMissingStagePeerConfirm,
    kAirportItlwmIwnDirectSaeTraceMissingStagePmkClaim,
    kAirportItlwmIwnDirectSaeTraceMissingStageAssocDescriptor,
    kAirportItlwmIwnDirectSaeTraceMissingStageAssocExchange,
    kAirportItlwmIwnDirectSaeTraceMissingStageFourWay,
    kAirportItlwmIwnDirectSaeTraceMissingStagePmfPtkSoftwareCcmp,
    kAirportItlwmIwnDirectSaeTraceMissingStagePmfGtkSoftwareCcmp,
    kAirportItlwmIwnDirectSaeTraceMissingStagePmfIgtkStage,
    kAirportItlwmIwnDirectSaeTraceMissingStagePmfIgtkPublication,
    kAirportItlwmIwnDirectSaeTraceMissingStagePmfKeysetPublication,
    kAirportItlwmIwnDirectSaeTraceMissingStagePortValid,
    kAirportItlwmIwnDirectSaeTraceMissingStageUnknown,
};

enum airport_itlwm_iwn_direct_sae_trace_phase {
    airport_itlwm_iwn_direct_sae_trace_phase_need_commit = 0,
    airport_itlwm_iwn_direct_sae_trace_phase_need_peer_commit,
    airport_itlwm_iwn_direct_sae_trace_phase_need_confirm,
    airport_itlwm_iwn_direct_sae_trace_phase_need_peer_confirm,
    airport_itlwm_iwn_direct_sae_trace_phase_need_pmk_claim,
    airport_itlwm_iwn_direct_sae_trace_phase_need_assoc_descriptor,
    airport_itlwm_iwn_direct_sae_trace_phase_after_assoc_descriptor,
};

static inline void
airport_itlwm_iwn_direct_sae_trace_set_stage(
    enum AirportItlwmIwnDirectSaeTraceMissingStage *out_stage,
    enum AirportItlwmIwnDirectSaeTraceMissingStage stage)
{
    if (out_stage != 0)
        *out_stage = stage;
}

/* Generic scan, AUTH, and EAPOL facts corroborate the direct chain.  They
 * have no identity-bearing payload and may repeat during normal retries.
 * IWN software-PMF facts are intentionally not neutral here: a direct SAE
 * success is useful only if the required PMF keyset is also observed. */
static inline int
airport_itlwm_iwn_direct_sae_trace_event_is_neutral(uint32_t event)
{
    return (event >= kAirportItlwmPostPltiTraceEventIwnScanCoalesced &&
            event <= kAirportItlwmPostPltiTraceEventScanNoCandidate) ||
        (event >= AIRPORT_ITLWM_POST_PLTI_TRACE_PMF_INGRESS_EVENT_FIRST &&
         event <= AIRPORT_ITLWM_POST_PLTI_TRACE_PMF_INGRESS_EVENT_LAST) ||
        event == kAirportItlwmPostPltiTraceEventAuthEnqueued ||
        event == kAirportItlwmPostPltiTraceEventAuthDequeued ||
        event == kAirportItlwmPostPltiTraceEventAuthFwSubmitted ||
        event == kAirportItlwmPostPltiTraceEventAuthTxDone ||
        event == kAirportItlwmPostPltiTraceEventAuthRxFromFirmware ||
        event == kAirportItlwmPostPltiTraceEventAuthRxNet80211 ||
        event == kAirportItlwmPostPltiTraceEventEapolFwSubmitted ||
        event == kAirportItlwmPostPltiTraceEventEapolTxDone;
}

static inline uint32_t
airport_itlwm_iwn_direct_sae_trace_published_igtk_slot(uint32_t event)
{
    switch (event) {
    case kAirportItlwmPostPltiTraceEventIwnIgtkSlot4Published:
        return 4;
    case kAirportItlwmPostPltiTraceEventIwnIgtkSlot5Published:
        return 5;
    default:
        return 0;
    }
}

static inline uint32_t
airport_itlwm_iwn_direct_sae_trace_selected_igtk_slot(uint32_t event)
{
    switch (event) {
    case kAirportItlwmPostPltiTraceEventIwnIgtkSlot4TxSelected:
        return 4;
    case kAirportItlwmPostPltiTraceEventIwnIgtkSlot5TxSelected:
        return 5;
    default:
        return 0;
    }
}

static inline enum AirportItlwmIwnDirectSaeTraceMissingStage
airport_itlwm_iwn_direct_sae_trace_first_missing(
    uint32_t fresh_scan_started, uint32_t bss_selected, uint32_t join_bss,
    uint32_t node_mfp,
    uint32_t auth_state,
    enum airport_itlwm_iwn_direct_sae_trace_phase direct_phase,
    uint32_t assoc_descriptor, uint32_t assoc_exchange, uint32_t run_entered,
    uint32_t eapol_rx, uint32_t eapol_kernel, uint32_t eapol_tx,
    uint32_t pmf_ptk_prepared, uint32_t pmf_gtk_prepared,
    uint32_t pmf_igtk_acknowledged, uint32_t pmf_active_igtk_slot,
    uint32_t pmf_keyset_published,
    uint32_t port_valid)
{
    if (!fresh_scan_started)
        return kAirportItlwmIwnDirectSaeTraceMissingStageFreshScan;
    if (!bss_selected)
        return kAirportItlwmIwnDirectSaeTraceMissingStageBssSelection;
    if (!join_bss)
        return kAirportItlwmIwnDirectSaeTraceMissingStageJoinBss;
    /* RSN selection sets NODE_MFP at the selected-BSS join boundary before
     * the direct owner may enter S_AUTH.  Preserve that causal order in a
     * sealed prefix instead of relabelling a missing PMF prerequisite as a
     * later missing AUTH state. */
    if (!node_mfp)
        return kAirportItlwmIwnDirectSaeTraceMissingStageNodeMfp;
    if (!auth_state)
        return kAirportItlwmIwnDirectSaeTraceMissingStageAuthState;
    if (direct_phase == airport_itlwm_iwn_direct_sae_trace_phase_need_commit)
        return kAirportItlwmIwnDirectSaeTraceMissingStageCommitTx;
    if (direct_phase ==
        airport_itlwm_iwn_direct_sae_trace_phase_need_peer_commit)
        return kAirportItlwmIwnDirectSaeTraceMissingStagePeerCommit;
    if (direct_phase == airport_itlwm_iwn_direct_sae_trace_phase_need_confirm)
        return kAirportItlwmIwnDirectSaeTraceMissingStageConfirmTx;
    if (direct_phase ==
        airport_itlwm_iwn_direct_sae_trace_phase_need_peer_confirm)
        return kAirportItlwmIwnDirectSaeTraceMissingStagePeerConfirm;
    if (direct_phase ==
        airport_itlwm_iwn_direct_sae_trace_phase_need_pmk_claim)
        return kAirportItlwmIwnDirectSaeTraceMissingStagePmkClaim;
    if (!assoc_descriptor || direct_phase ==
        airport_itlwm_iwn_direct_sae_trace_phase_need_assoc_descriptor)
        return kAirportItlwmIwnDirectSaeTraceMissingStageAssocDescriptor;
    if (!assoc_exchange)
        return kAirportItlwmIwnDirectSaeTraceMissingStageAssocExchange;
    if (!run_entered || eapol_rx < 2 || eapol_kernel < 2 || eapol_tx < 2)
        return kAirportItlwmIwnDirectSaeTraceMissingStageFourWay;
    if (!pmf_ptk_prepared)
        return kAirportItlwmIwnDirectSaeTraceMissingStagePmfPtkSoftwareCcmp;
    if (!pmf_gtk_prepared)
        return kAirportItlwmIwnDirectSaeTraceMissingStagePmfGtkSoftwareCcmp;
    if (!pmf_igtk_acknowledged)
        return kAirportItlwmIwnDirectSaeTraceMissingStagePmfIgtkStage;
    if (pmf_active_igtk_slot == 0)
        return kAirportItlwmIwnDirectSaeTraceMissingStagePmfIgtkPublication;
    if (!pmf_keyset_published)
        return kAirportItlwmIwnDirectSaeTraceMissingStagePmfKeysetPublication;
    if (!port_valid)
        return kAirportItlwmIwnDirectSaeTraceMissingStagePortValid;
    return kAirportItlwmIwnDirectSaeTraceMissingStageNone;
}

static inline enum AirportItlwmIwnDirectSaeTraceVerdict
airport_itlwm_iwn_direct_sae_trace_verdict_for_stage(
    enum AirportItlwmIwnDirectSaeTraceMissingStage stage)
{
    switch (stage) {
    case kAirportItlwmIwnDirectSaeTraceMissingStageFreshScan:
        return kAirportItlwmIwnDirectSaeTraceVerdictFreshScanNotObserved;
    case kAirportItlwmIwnDirectSaeTraceMissingStageBssSelection:
        return kAirportItlwmIwnDirectSaeTraceVerdictRequestNoBssSelection;
    case kAirportItlwmIwnDirectSaeTraceMissingStageJoinBss:
        return kAirportItlwmIwnDirectSaeTraceVerdictJoinBssNotObserved;
    case kAirportItlwmIwnDirectSaeTraceMissingStageNodeMfp:
        return kAirportItlwmIwnDirectSaeTraceVerdictNodeMfpNotNegotiated;
    case kAirportItlwmIwnDirectSaeTraceMissingStageAuthState:
        return kAirportItlwmIwnDirectSaeTraceVerdictAuthStateNotObserved;
    case kAirportItlwmIwnDirectSaeTraceMissingStageCommitTx:
        return kAirportItlwmIwnDirectSaeTraceVerdictCommitTxNotComplete;
    case kAirportItlwmIwnDirectSaeTraceMissingStagePeerCommit:
        return kAirportItlwmIwnDirectSaeTraceVerdictPeerCommitNotAccepted;
    case kAirportItlwmIwnDirectSaeTraceMissingStageConfirmTx:
        return kAirportItlwmIwnDirectSaeTraceVerdictConfirmTxNotComplete;
    case kAirportItlwmIwnDirectSaeTraceMissingStagePeerConfirm:
        return kAirportItlwmIwnDirectSaeTraceVerdictPeerConfirmNotValidated;
    case kAirportItlwmIwnDirectSaeTraceMissingStagePmkClaim:
        return kAirportItlwmIwnDirectSaeTraceVerdictPmkNotClaimed;
    case kAirportItlwmIwnDirectSaeTraceMissingStageAssocDescriptor:
        return kAirportItlwmIwnDirectSaeTraceVerdictAssocDescriptorNotAccepted;
    case kAirportItlwmIwnDirectSaeTraceMissingStageAssocExchange:
        return kAirportItlwmIwnDirectSaeTraceVerdictAssocExchangeNotComplete;
    case kAirportItlwmIwnDirectSaeTraceMissingStageFourWay:
    case kAirportItlwmIwnDirectSaeTraceMissingStagePortValid:
        return kAirportItlwmIwnDirectSaeTraceVerdictFourWayNotComplete;
    case kAirportItlwmIwnDirectSaeTraceMissingStagePmfPtkSoftwareCcmp:
        return kAirportItlwmIwnDirectSaeTraceVerdictPmfPtkSoftwareCcmpNotObserved;
    case kAirportItlwmIwnDirectSaeTraceMissingStagePmfGtkSoftwareCcmp:
        return kAirportItlwmIwnDirectSaeTraceVerdictPmfGtkSoftwareCcmpNotObserved;
    case kAirportItlwmIwnDirectSaeTraceMissingStagePmfIgtkStage:
        return kAirportItlwmIwnDirectSaeTraceVerdictPmfIgtkStageNotObserved;
    case kAirportItlwmIwnDirectSaeTraceMissingStagePmfIgtkPublication:
        return kAirportItlwmIwnDirectSaeTraceVerdictPmfIgtkPublicationNotObserved;
    case kAirportItlwmIwnDirectSaeTraceMissingStagePmfKeysetPublication:
        return kAirportItlwmIwnDirectSaeTraceVerdictPmfKeysetPublicationNotObserved;
    default:
        return kAirportItlwmIwnDirectSaeTraceVerdictIntegrityInconclusive;
    }
}

static inline enum AirportItlwmIwnDirectSaeTraceVerdict
airport_itlwm_iwn_direct_sae_trace_classify_entries_with_stage(
    const AirportItlwmPostPltiTraceEntry *entries, uint32_t count,
    int integrity, uint32_t backend, uint32_t episode_count,
    uint32_t active_episode,
    enum AirportItlwmIwnDirectSaeTraceMissingStage *out_stage)
{
    uint32_t episode, generation;
    uint32_t fresh_scan_started = 0, bss_selected = 0, join_bss = 0;
    uint32_t node_mfp = 0, auth_state = 0;
    uint32_t assoc_state = 0, assoc_enqueued = 0, assoc_dequeued = 0;
    uint32_t assoc_descriptor = 0, assoc_fw_submitted = 0, assoc_tx_done = 0;
    uint32_t assoc_rx_firmware = 0, assoc_rx_n80211 = 0, run_entered = 0;
    uint32_t eapol_rx = 0, eapol_kernel = 0, eapol_tx = 0;
    uint32_t pmf_ptk_prepared = 0, pmf_gtk_prepared = 0;
    uint32_t pmf_igtk_acknowledged = 0, pmf_published_igtk_slot = 0;
    uint32_t pmf_active_igtk_slot = 0, pmf_keyset_published = 0;
    uint32_t pmf_started = 0;
    uint32_t terminal = 0, port_valid = 0;
    enum airport_itlwm_iwn_direct_sae_trace_phase direct_phase =
        airport_itlwm_iwn_direct_sae_trace_phase_need_commit;

    airport_itlwm_iwn_direct_sae_trace_set_stage(
        out_stage, kAirportItlwmIwnDirectSaeTraceMissingStageUnknown);
    if (!integrity)
        return kAirportItlwmIwnDirectSaeTraceVerdictIntegrityInconclusive;
    if (backend != kAirportItlwmPostPltiTraceBackendIwn)
        return kAirportItlwmIwnDirectSaeTraceVerdictBackendUnsupported;
    if (episode_count == 0) {
        if (count == 0 && active_episode == 0) {
            airport_itlwm_iwn_direct_sae_trace_set_stage(
                out_stage, kAirportItlwmIwnDirectSaeTraceMissingStageNone);
            return kAirportItlwmIwnDirectSaeTraceVerdictBranchNotObserved;
        }
        return kAirportItlwmIwnDirectSaeTraceVerdictIntegrityInconclusive;
    }
    if (episode_count != 1 || active_episode != 0 || entries == 0 ||
        count == 0)
        return kAirportItlwmIwnDirectSaeTraceVerdictIntegrityInconclusive;

    episode = entries[0].episode;
    generation = entries[0].captureGeneration;
    if (episode == 0 || generation == 0 || entries[0].sequence == 0 ||
        entries[0].event !=
            kAirportItlwmPostPltiTraceEventIwnDirectSaeRequestAccepted)
        return kAirportItlwmIwnDirectSaeTraceVerdictIntegrityInconclusive;

    for (uint32_t i = 1; i < count; i++) {
        const uint32_t event = entries[i].event;
        const uint32_t pmf_published_slot =
            airport_itlwm_iwn_direct_sae_trace_published_igtk_slot(event);
        const uint32_t pmf_selected_slot =
            airport_itlwm_iwn_direct_sae_trace_selected_igtk_slot(event);

        if (entries[i].sequence != entries[0].sequence + i ||
            entries[i].captureGeneration != generation ||
            entries[i].episode != episode || event ==
                kAirportItlwmPostPltiTraceEventUnknown || event >=
                kAirportItlwmPostPltiTraceEventMax || terminal)
            return kAirportItlwmIwnDirectSaeTraceVerdictIntegrityInconclusive;
        if (event == kAirportItlwmPostPltiTraceEventEpisodeAborted ||
            event == kAirportItlwmPostPltiTraceEventWclPmkReadyScanResume ||
            event ==
                kAirportItlwmPostPltiTraceEventIwnDirectSaeRequestAccepted)
            return kAirportItlwmIwnDirectSaeTraceVerdictIntegrityInconclusive;

        if (event == kAirportItlwmPostPltiTraceEventCaptureWindowSealed) {
            const enum AirportItlwmIwnDirectSaeTraceMissingStage stage =
                airport_itlwm_iwn_direct_sae_trace_first_missing(
                    fresh_scan_started, bss_selected, join_bss, node_mfp,
                    auth_state, direct_phase,
                    assoc_descriptor,
                    assoc_fw_submitted && assoc_tx_done &&
                        assoc_rx_firmware && assoc_rx_n80211,
                    run_entered, eapol_rx, eapol_kernel, eapol_tx,
                    pmf_ptk_prepared, pmf_gtk_prepared,
                    pmf_igtk_acknowledged, pmf_active_igtk_slot,
                    pmf_keyset_published, 0);
            terminal = 1;
            airport_itlwm_iwn_direct_sae_trace_set_stage(out_stage, stage);
            return airport_itlwm_iwn_direct_sae_trace_verdict_for_stage(stage);
        }
        if (event == kAirportItlwmPostPltiTraceEventPortValidTransition) {
            /* Once the local PMF branch started, a port-valid transition
             * before its final atomic keyset publication is internally
             * inconsistent rather than a clean missing-stage observation. */
            if (pmf_started && !pmf_keyset_published)
                return kAirportItlwmIwnDirectSaeTraceVerdictIntegrityInconclusive;
            const enum AirportItlwmIwnDirectSaeTraceMissingStage stage =
                airport_itlwm_iwn_direct_sae_trace_first_missing(
                    fresh_scan_started, bss_selected, join_bss, node_mfp,
                    auth_state, direct_phase,
                    assoc_descriptor,
                    assoc_fw_submitted && assoc_tx_done &&
                        assoc_rx_firmware && assoc_rx_n80211,
                    run_entered, eapol_rx, eapol_kernel, eapol_tx,
                    pmf_ptk_prepared, pmf_gtk_prepared,
                    pmf_igtk_acknowledged, pmf_active_igtk_slot,
                    pmf_keyset_published, 1);
            terminal = 1;
            port_valid = 1;
            airport_itlwm_iwn_direct_sae_trace_set_stage(out_stage, stage);
            if (stage == kAirportItlwmIwnDirectSaeTraceMissingStageNone)
                return kAirportItlwmIwnDirectSaeTraceVerdictDirectSae4WayPortValid;
            return airport_itlwm_iwn_direct_sae_trace_verdict_for_stage(stage);
        }

        switch (event) {
        case kAirportItlwmPostPltiTraceEventIwnScanStarted:
            /* The direct episode is armed before resume_scan().  A started
             * IWN scan is therefore the only accepted fresh-scan fact; a
             * coalesced pre-existing scan never upgrades to this boundary. */
            if (fresh_scan_started || bss_selected || join_bss || auth_state)
                return kAirportItlwmIwnDirectSaeTraceVerdictIntegrityInconclusive;
            fresh_scan_started = 1;
            break;
        case kAirportItlwmPostPltiTraceEventBssSelected:
            if (!fresh_scan_started || bss_selected || join_bss)
                return kAirportItlwmIwnDirectSaeTraceVerdictIntegrityInconclusive;
            bss_selected = 1;
            break;
        case kAirportItlwmPostPltiTraceEventJoinBssEntered:
            if (!bss_selected || join_bss)
                return kAirportItlwmIwnDirectSaeTraceVerdictIntegrityInconclusive;
            join_bss = 1;
            break;
        case kAirportItlwmPostPltiTraceEventNodeMfpNegotiated:
            if (!join_bss || node_mfp || auth_state)
                return kAirportItlwmIwnDirectSaeTraceVerdictIntegrityInconclusive;
            node_mfp = 1;
            break;
        case kAirportItlwmPostPltiTraceEventAuthStateEntered:
            if (!join_bss || auth_state)
                return kAirportItlwmIwnDirectSaeTraceVerdictIntegrityInconclusive;
            auth_state = 1;
            break;
        case kAirportItlwmPostPltiTraceEventIwnDirectSaeCommitTxComplete:
            if (!bss_selected || !join_bss || !auth_state ||
                direct_phase >
                    airport_itlwm_iwn_direct_sae_trace_phase_need_peer_commit)
                return kAirportItlwmIwnDirectSaeTraceVerdictIntegrityInconclusive;
            direct_phase =
                airport_itlwm_iwn_direct_sae_trace_phase_need_peer_commit;
            break;
        case kAirportItlwmPostPltiTraceEventIwnDirectSaePeerCommitAccepted:
            if (direct_phase !=
                airport_itlwm_iwn_direct_sae_trace_phase_need_peer_commit)
                return kAirportItlwmIwnDirectSaeTraceVerdictIntegrityInconclusive;
            direct_phase =
                airport_itlwm_iwn_direct_sae_trace_phase_need_confirm;
            break;
        case kAirportItlwmPostPltiTraceEventIwnDirectSaeConfirmTxComplete:
            if (direct_phase <
                    airport_itlwm_iwn_direct_sae_trace_phase_need_confirm ||
                direct_phase >
                    airport_itlwm_iwn_direct_sae_trace_phase_need_peer_confirm)
                return kAirportItlwmIwnDirectSaeTraceVerdictIntegrityInconclusive;
            direct_phase =
                airport_itlwm_iwn_direct_sae_trace_phase_need_peer_confirm;
            break;
        case kAirportItlwmPostPltiTraceEventIwnDirectSaePeerConfirmValidated:
            if (direct_phase !=
                airport_itlwm_iwn_direct_sae_trace_phase_need_peer_confirm)
                return kAirportItlwmIwnDirectSaeTraceVerdictIntegrityInconclusive;
            direct_phase =
                airport_itlwm_iwn_direct_sae_trace_phase_need_pmk_claim;
            break;
        case kAirportItlwmPostPltiTraceEventIwnDirectSaePmkClaimed:
            if (!node_mfp || direct_phase !=
                airport_itlwm_iwn_direct_sae_trace_phase_need_pmk_claim)
                return kAirportItlwmIwnDirectSaeTraceVerdictIntegrityInconclusive;
            direct_phase =
                airport_itlwm_iwn_direct_sae_trace_phase_need_assoc_descriptor;
            break;
        case kAirportItlwmPostPltiTraceEventAssocStateEntered:
            if (direct_phase !=
                    airport_itlwm_iwn_direct_sae_trace_phase_need_assoc_descriptor ||
                assoc_state)
                return kAirportItlwmIwnDirectSaeTraceVerdictIntegrityInconclusive;
            assoc_state = 1;
            break;
        case kAirportItlwmPostPltiTraceEventAssocEnqueued:
            if (!assoc_state || assoc_enqueued)
                return kAirportItlwmIwnDirectSaeTraceVerdictIntegrityInconclusive;
            assoc_enqueued = 1;
            break;
        case kAirportItlwmPostPltiTraceEventAssocDequeued:
            if (!assoc_enqueued || assoc_dequeued)
                return kAirportItlwmIwnDirectSaeTraceVerdictIntegrityInconclusive;
            assoc_dequeued = 1;
            break;
        case kAirportItlwmPostPltiTraceEventIwnDirectSaeAssocDescriptorAccepted:
            if (!assoc_state || !assoc_enqueued || !assoc_dequeued ||
                assoc_descriptor || direct_phase !=
                    airport_itlwm_iwn_direct_sae_trace_phase_need_assoc_descriptor)
                return kAirportItlwmIwnDirectSaeTraceVerdictIntegrityInconclusive;
            assoc_descriptor = 1;
            direct_phase =
                airport_itlwm_iwn_direct_sae_trace_phase_after_assoc_descriptor;
            break;
        case kAirportItlwmPostPltiTraceEventAssocFwSubmitted:
            if (!assoc_descriptor || assoc_fw_submitted)
                return kAirportItlwmIwnDirectSaeTraceVerdictIntegrityInconclusive;
            assoc_fw_submitted = 1;
            break;
        case kAirportItlwmPostPltiTraceEventAssocTxDone:
            if (!assoc_fw_submitted || assoc_tx_done)
                return kAirportItlwmIwnDirectSaeTraceVerdictIntegrityInconclusive;
            assoc_tx_done = 1;
            break;
        case kAirportItlwmPostPltiTraceEventAssocRxFromFirmware:
            if (!assoc_tx_done || assoc_rx_firmware)
                return kAirportItlwmIwnDirectSaeTraceVerdictIntegrityInconclusive;
            assoc_rx_firmware = 1;
            break;
        case kAirportItlwmPostPltiTraceEventAssocRxNet80211:
            if (!assoc_rx_firmware || assoc_rx_n80211)
                return kAirportItlwmIwnDirectSaeTraceVerdictIntegrityInconclusive;
            assoc_rx_n80211 = 1;
            break;
        case kAirportItlwmPostPltiTraceEventRunEntered:
            if (!assoc_rx_n80211 || run_entered)
                return kAirportItlwmIwnDirectSaeTraceVerdictIntegrityInconclusive;
            run_entered = 1;
            break;
        case kAirportItlwmPostPltiTraceEventEapolRxDecapped:
            if (!run_entered)
                return kAirportItlwmIwnDirectSaeTraceVerdictIntegrityInconclusive;
            eapol_rx++;
            break;
        case kAirportItlwmPostPltiTraceEventEapolRxKernelPae:
            if (!run_entered || eapol_kernel >= eapol_rx)
                return kAirportItlwmIwnDirectSaeTraceVerdictIntegrityInconclusive;
            eapol_kernel++;
            break;
        case kAirportItlwmPostPltiTraceEventEapolTxEnqueued:
            if (!run_entered || eapol_tx >= eapol_kernel)
                return kAirportItlwmIwnDirectSaeTraceVerdictIntegrityInconclusive;
            eapol_tx++;
            break;
        case kAirportItlwmPostPltiTraceEventIwnMfpPaePtkSoftwarePrepared:
            if (!run_entered || pmf_ptk_prepared || pmf_gtk_prepared ||
                pmf_igtk_acknowledged || pmf_published_igtk_slot != 0 ||
                pmf_active_igtk_slot != 0 || pmf_keyset_published)
                return kAirportItlwmIwnDirectSaeTraceVerdictIntegrityInconclusive;
            pmf_started = 1;
            pmf_ptk_prepared = 1;
            break;
        case kAirportItlwmPostPltiTraceEventIwnMfpPaeGtkSoftwarePrepared:
            if (!run_entered || !pmf_ptk_prepared || pmf_gtk_prepared ||
                pmf_igtk_acknowledged || pmf_published_igtk_slot != 0 ||
                pmf_active_igtk_slot != 0 || pmf_keyset_published)
                return kAirportItlwmIwnDirectSaeTraceVerdictIntegrityInconclusive;
            pmf_started = 1;
            pmf_gtk_prepared = 1;
            break;
        case kAirportItlwmPostPltiTraceEventIwnMfpPaeIgtkStageAcknowledged:
            if (!run_entered || !pmf_ptk_prepared || !pmf_gtk_prepared ||
                pmf_igtk_acknowledged || pmf_published_igtk_slot != 0 ||
                pmf_active_igtk_slot != 0 || pmf_keyset_published)
                return kAirportItlwmIwnDirectSaeTraceVerdictIntegrityInconclusive;
            pmf_started = 1;
            pmf_igtk_acknowledged = 1;
            break;
        case kAirportItlwmPostPltiTraceEventIwnIgtkSlot4Published:
        case kAirportItlwmPostPltiTraceEventIwnIgtkSlot5Published:
            if (!run_entered || !pmf_ptk_prepared || !pmf_gtk_prepared ||
                !pmf_igtk_acknowledged || pmf_published_slot == 0 ||
                pmf_published_igtk_slot != 0 || pmf_active_igtk_slot != 0 ||
                pmf_keyset_published)
                return kAirportItlwmIwnDirectSaeTraceVerdictIntegrityInconclusive;
            pmf_started = 1;
            pmf_published_igtk_slot = pmf_published_slot;
            break;
        case kAirportItlwmPostPltiTraceEventIwnIgtkSlot4TxSelected:
        case kAirportItlwmPostPltiTraceEventIwnIgtkSlot5TxSelected:
            if (!run_entered || pmf_selected_slot == 0 ||
                pmf_published_igtk_slot == 0 ||
                pmf_selected_slot != pmf_published_igtk_slot ||
                pmf_active_igtk_slot != 0 || pmf_keyset_published)
                return kAirportItlwmIwnDirectSaeTraceVerdictIntegrityInconclusive;
            pmf_started = 1;
            pmf_active_igtk_slot = pmf_selected_slot;
            pmf_published_igtk_slot = 0;
            break;
        case kAirportItlwmPostPltiTraceEventIwnMfpPaeSoftwareCcmpBipPublished:
            if (!run_entered || !pmf_ptk_prepared || !pmf_gtk_prepared ||
                !pmf_igtk_acknowledged || pmf_active_igtk_slot == 0 ||
                pmf_keyset_published)
                return kAirportItlwmIwnDirectSaeTraceVerdictIntegrityInconclusive;
            pmf_started = 1;
            pmf_keyset_published = 1;
            break;
        default:
            if (!airport_itlwm_iwn_direct_sae_trace_event_is_neutral(event))
                return kAirportItlwmIwnDirectSaeTraceVerdictIntegrityInconclusive;
            break;
        }
    }

    (void)port_valid;
    return kAirportItlwmIwnDirectSaeTraceVerdictIntegrityInconclusive;
}

static inline enum AirportItlwmIwnDirectSaeTraceVerdict
airport_itlwm_iwn_direct_sae_trace_classify_entries(
    const AirportItlwmPostPltiTraceEntry *entries, uint32_t count,
    int integrity, uint32_t backend, uint32_t episode_count,
    uint32_t active_episode)
{
    return airport_itlwm_iwn_direct_sae_trace_classify_entries_with_stage(
        entries, count, integrity, backend, episode_count, active_episode, 0);
}

#endif /* AirportItlwmIwnDirectSaeTraceContracts_h */
