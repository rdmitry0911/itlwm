#ifndef AirportItlwmPostPltiTraceMatrixContracts_h
#define AirportItlwmPostPltiTraceMatrixContracts_h

/*
 * IWN ordered association evaluator for the safe-only post-PLTI trace.
 *
 * This contract accepts fixed categorical records only.  It is deliberately
 * IWN-only and does not carry a channel, BSS, address, key, status, or frame
 * field.  CaptureWindowSealed makes an unfinished but coherent prefix
 * diagnosable without treating runner teardown as an abort.
 */

#include <stdint.h>

#include <ClientKit/AirportItlwmPostPltiTrace.h>

enum AirportItlwmPostPltiTraceMatrixVerdict {
    kAirportItlwmPostPltiTraceMatrixVerdictIntegrityInconclusive = 0,
    kAirportItlwmPostPltiTraceMatrixVerdictBackendUnsupported,
    kAirportItlwmPostPltiTraceMatrixVerdictBranchNotObserved,
    kAirportItlwmPostPltiTraceMatrixVerdictResumeNoStateRequest,
    kAirportItlwmPostPltiTraceMatrixVerdictResumeNoIwnDispatch,
    kAirportItlwmPostPltiTraceMatrixVerdictScanCommandRejected,
    kAirportItlwmPostPltiTraceMatrixVerdictScanIncomplete,
    kAirportItlwmPostPltiTraceMatrixVerdictScanNoCandidate,
    kAirportItlwmPostPltiTraceMatrixVerdictResumeNoSelection,
    kAirportItlwmPostPltiTraceMatrixVerdictAuthNotDrained,
    kAirportItlwmPostPltiTraceMatrixVerdictTxNoCompletion,
    kAirportItlwmPostPltiTraceMatrixVerdictNoEapol,
    kAirportItlwmPostPltiTraceMatrixVerdictKernelChainObserved,
};

enum AirportItlwmPostPltiTraceMissingStage {
    kAirportItlwmPostPltiTraceMissingStageNone = 0,
    kAirportItlwmPostPltiTraceMissingStageStateScanSelfRequest,
    kAirportItlwmPostPltiTraceMissingStageIwnScanState,
    kAirportItlwmPostPltiTraceMissingStageIwnScanCommand,
    kAirportItlwmPostPltiTraceMissingStageScanCompletion,
    kAirportItlwmPostPltiTraceMissingStageBssSelection,
    kAirportItlwmPostPltiTraceMissingStageJoinBss,
    kAirportItlwmPostPltiTraceMissingStageAuthState,
    kAirportItlwmPostPltiTraceMissingStageAuthEnqueue,
    kAirportItlwmPostPltiTraceMissingStageAuthDequeue,
    kAirportItlwmPostPltiTraceMissingStageAuthFirmwareSubmit,
    kAirportItlwmPostPltiTraceMissingStageAuthExchange,
    kAirportItlwmPostPltiTraceMissingStageAssocState,
    kAirportItlwmPostPltiTraceMissingStageAssocEnqueue,
    kAirportItlwmPostPltiTraceMissingStageAssocDequeue,
    kAirportItlwmPostPltiTraceMissingStageAssocFirmwareSubmit,
    kAirportItlwmPostPltiTraceMissingStageAssocExchange,
    kAirportItlwmPostPltiTraceMissingStageRunState,
    kAirportItlwmPostPltiTraceMissingStageEapolDecapped,
    kAirportItlwmPostPltiTraceMissingStageEapolKernelPae,
    kAirportItlwmPostPltiTraceMissingStageEapolEnqueue,
    kAirportItlwmPostPltiTraceMissingStagePortValid,
    /* The trace was not sealed or failed structural integrity. */
    kAirportItlwmPostPltiTraceMissingStageUnknown,
};

enum airport_itlwm_post_plti_trace_matrix_phase {
    airport_itlwm_post_plti_trace_phase_need_state_scan = 0,
    airport_itlwm_post_plti_trace_phase_need_iwn_scan_state,
    airport_itlwm_post_plti_trace_phase_need_scan_outcome,
    airport_itlwm_post_plti_trace_phase_need_scan_complete,
    airport_itlwm_post_plti_trace_phase_need_selection,
    airport_itlwm_post_plti_trace_phase_need_seal_scan_rejected,
    airport_itlwm_post_plti_trace_phase_need_seal_selection_held,
    airport_itlwm_post_plti_trace_phase_need_join,
    airport_itlwm_post_plti_trace_phase_need_auth_state,
    airport_itlwm_post_plti_trace_phase_need_auth_enqueue,
    airport_itlwm_post_plti_trace_phase_need_auth_dequeue,
    airport_itlwm_post_plti_trace_phase_need_auth_submit,
    airport_itlwm_post_plti_trace_phase_wait_auth,
    airport_itlwm_post_plti_trace_phase_need_assoc_state,
    airport_itlwm_post_plti_trace_phase_need_assoc_enqueue,
    airport_itlwm_post_plti_trace_phase_need_assoc_dequeue,
    airport_itlwm_post_plti_trace_phase_need_assoc_submit,
    airport_itlwm_post_plti_trace_phase_wait_assoc,
    airport_itlwm_post_plti_trace_phase_need_run,
    airport_itlwm_post_plti_trace_phase_need_eapol_decapped,
    airport_itlwm_post_plti_trace_phase_need_eapol_kernel_pae,
    airport_itlwm_post_plti_trace_phase_need_eapol_enqueue,
    airport_itlwm_post_plti_trace_phase_wait_port_valid,
};

/* The dedicated IWN software-PMF evaluator consumes these facts.  The ordered
 * association matrix retains its prior scope and treats them as neutral
 * post-PAE corroboration rather than allowing them to perturb a scan/auth/
 * association verdict. */
static inline int
airport_itlwm_post_plti_trace_matrix_event_is_iwn_software_pmf(uint32_t event)
{
    return event >= AIRPORT_ITLWM_POST_PLTI_TRACE_IWN_SOFTWARE_PMF_EVENT_FIRST &&
        event <= AIRPORT_ITLWM_POST_PLTI_TRACE_IWN_SOFTWARE_PMF_EVENT_LAST;
}

static inline enum AirportItlwmPostPltiTraceMissingStage
airport_itlwm_post_plti_trace_matrix_phase_missing_stage(
    enum airport_itlwm_post_plti_trace_matrix_phase phase,
    uint32_t saw_no_candidate)
{
    switch (phase) {
    case airport_itlwm_post_plti_trace_phase_need_state_scan:
        return saw_no_candidate ?
            kAirportItlwmPostPltiTraceMissingStageBssSelection :
            kAirportItlwmPostPltiTraceMissingStageStateScanSelfRequest;
    case airport_itlwm_post_plti_trace_phase_need_iwn_scan_state:
        return kAirportItlwmPostPltiTraceMissingStageIwnScanState;
    case airport_itlwm_post_plti_trace_phase_need_scan_outcome:
    case airport_itlwm_post_plti_trace_phase_need_seal_scan_rejected:
        return kAirportItlwmPostPltiTraceMissingStageIwnScanCommand;
    case airport_itlwm_post_plti_trace_phase_need_scan_complete:
        return kAirportItlwmPostPltiTraceMissingStageScanCompletion;
    case airport_itlwm_post_plti_trace_phase_need_selection:
    case airport_itlwm_post_plti_trace_phase_need_seal_selection_held:
        return kAirportItlwmPostPltiTraceMissingStageBssSelection;
    case airport_itlwm_post_plti_trace_phase_need_join:
        return kAirportItlwmPostPltiTraceMissingStageJoinBss;
    case airport_itlwm_post_plti_trace_phase_need_auth_state:
        return kAirportItlwmPostPltiTraceMissingStageAuthState;
    case airport_itlwm_post_plti_trace_phase_need_auth_enqueue:
        return kAirportItlwmPostPltiTraceMissingStageAuthEnqueue;
    case airport_itlwm_post_plti_trace_phase_need_auth_dequeue:
        return kAirportItlwmPostPltiTraceMissingStageAuthDequeue;
    case airport_itlwm_post_plti_trace_phase_need_auth_submit:
        return kAirportItlwmPostPltiTraceMissingStageAuthFirmwareSubmit;
    case airport_itlwm_post_plti_trace_phase_wait_auth:
        return kAirportItlwmPostPltiTraceMissingStageAuthExchange;
    case airport_itlwm_post_plti_trace_phase_need_assoc_state:
        return kAirportItlwmPostPltiTraceMissingStageAssocState;
    case airport_itlwm_post_plti_trace_phase_need_assoc_enqueue:
        return kAirportItlwmPostPltiTraceMissingStageAssocEnqueue;
    case airport_itlwm_post_plti_trace_phase_need_assoc_dequeue:
        return kAirportItlwmPostPltiTraceMissingStageAssocDequeue;
    case airport_itlwm_post_plti_trace_phase_need_assoc_submit:
        return kAirportItlwmPostPltiTraceMissingStageAssocFirmwareSubmit;
    case airport_itlwm_post_plti_trace_phase_wait_assoc:
        return kAirportItlwmPostPltiTraceMissingStageAssocExchange;
    case airport_itlwm_post_plti_trace_phase_need_run:
        return kAirportItlwmPostPltiTraceMissingStageRunState;
    case airport_itlwm_post_plti_trace_phase_need_eapol_decapped:
        return kAirportItlwmPostPltiTraceMissingStageEapolDecapped;
    case airport_itlwm_post_plti_trace_phase_need_eapol_kernel_pae:
        return kAirportItlwmPostPltiTraceMissingStageEapolKernelPae;
    case airport_itlwm_post_plti_trace_phase_need_eapol_enqueue:
        return kAirportItlwmPostPltiTraceMissingStageEapolEnqueue;
    case airport_itlwm_post_plti_trace_phase_wait_port_valid:
        return kAirportItlwmPostPltiTraceMissingStagePortValid;
    default:
        return kAirportItlwmPostPltiTraceMissingStageUnknown;
    }
}

static inline enum AirportItlwmPostPltiTraceMatrixVerdict
airport_itlwm_post_plti_trace_matrix_sealed_verdict(
    enum airport_itlwm_post_plti_trace_matrix_phase phase,
    uint32_t saw_no_candidate, uint32_t saw_scan_rejection)
{
    switch (phase) {
    case airport_itlwm_post_plti_trace_phase_need_state_scan:
        return saw_no_candidate ?
            kAirportItlwmPostPltiTraceMatrixVerdictScanNoCandidate :
            kAirportItlwmPostPltiTraceMatrixVerdictResumeNoStateRequest;
    case airport_itlwm_post_plti_trace_phase_need_iwn_scan_state:
        return kAirportItlwmPostPltiTraceMatrixVerdictResumeNoIwnDispatch;
    case airport_itlwm_post_plti_trace_phase_need_scan_outcome:
        return kAirportItlwmPostPltiTraceMatrixVerdictScanIncomplete;
    case airport_itlwm_post_plti_trace_phase_need_scan_complete:
        return saw_scan_rejection ?
            kAirportItlwmPostPltiTraceMatrixVerdictScanCommandRejected :
            kAirportItlwmPostPltiTraceMatrixVerdictScanIncomplete;
    case airport_itlwm_post_plti_trace_phase_need_seal_scan_rejected:
        return kAirportItlwmPostPltiTraceMatrixVerdictScanCommandRejected;
    case airport_itlwm_post_plti_trace_phase_need_selection:
        return kAirportItlwmPostPltiTraceMatrixVerdictResumeNoSelection;
    case airport_itlwm_post_plti_trace_phase_need_seal_selection_held:
        return kAirportItlwmPostPltiTraceMatrixVerdictResumeNoSelection;
    case airport_itlwm_post_plti_trace_phase_need_join:
    case airport_itlwm_post_plti_trace_phase_need_auth_state:
    case airport_itlwm_post_plti_trace_phase_need_auth_enqueue:
    case airport_itlwm_post_plti_trace_phase_need_auth_dequeue:
    case airport_itlwm_post_plti_trace_phase_need_auth_submit:
    case airport_itlwm_post_plti_trace_phase_wait_auth:
        return kAirportItlwmPostPltiTraceMatrixVerdictAuthNotDrained;
    case airport_itlwm_post_plti_trace_phase_need_assoc_state:
    case airport_itlwm_post_plti_trace_phase_need_assoc_enqueue:
    case airport_itlwm_post_plti_trace_phase_need_assoc_dequeue:
    case airport_itlwm_post_plti_trace_phase_need_assoc_submit:
    case airport_itlwm_post_plti_trace_phase_wait_assoc:
    case airport_itlwm_post_plti_trace_phase_need_run:
        return kAirportItlwmPostPltiTraceMatrixVerdictTxNoCompletion;
    case airport_itlwm_post_plti_trace_phase_need_eapol_decapped:
    case airport_itlwm_post_plti_trace_phase_need_eapol_kernel_pae:
    case airport_itlwm_post_plti_trace_phase_need_eapol_enqueue:
    case airport_itlwm_post_plti_trace_phase_wait_port_valid:
        return kAirportItlwmPostPltiTraceMatrixVerdictNoEapol;
    default:
        return kAirportItlwmPostPltiTraceMatrixVerdictIntegrityInconclusive;
    }
}

static inline enum AirportItlwmPostPltiTraceMatrixVerdict
airport_itlwm_post_plti_trace_matrix_classify_entries_with_stage(
    const AirportItlwmPostPltiTraceEntry *entries, uint32_t count,
    int integrity, uint32_t backend, uint32_t episode_count,
    uint32_t active_episode,
    enum AirportItlwmPostPltiTraceMissingStage *out_missing_stage)
{
    enum airport_itlwm_post_plti_trace_matrix_phase phase;
    uint32_t episode;
    uint32_t saw_no_candidate = 0;
    uint32_t saw_scan_rejection = 0;
    uint32_t auth_tx_done = 0;
    uint32_t auth_fw_rx = 0;
    uint32_t auth_net_rx = 0;
    uint32_t assoc_tx_done = 0;
    uint32_t assoc_fw_rx = 0;
    uint32_t assoc_net_rx = 0;
    uint32_t eapol_enqueued = 0;
    uint32_t eapol_submitted = 0;
    uint32_t eapol_done = 0;
    uint32_t terminal = 0;
    enum AirportItlwmPostPltiTraceMatrixVerdict terminal_verdict;
    enum AirportItlwmPostPltiTraceMissingStage terminal_missing_stage =
        kAirportItlwmPostPltiTraceMissingStageUnknown;

    if (out_missing_stage != 0)
        *out_missing_stage = kAirportItlwmPostPltiTraceMissingStageUnknown;

    if (!integrity)
        return kAirportItlwmPostPltiTraceMatrixVerdictIntegrityInconclusive;
    if (backend != kAirportItlwmPostPltiTraceBackendIwn)
        return kAirportItlwmPostPltiTraceMatrixVerdictBackendUnsupported;
    if (episode_count == 0)
        return count == 0 && active_episode == 0 ?
            kAirportItlwmPostPltiTraceMatrixVerdictBranchNotObserved :
            kAirportItlwmPostPltiTraceMatrixVerdictIntegrityInconclusive;
    if (episode_count != 1 || active_episode != 0 || entries == 0 ||
        count == 0)
        return kAirportItlwmPostPltiTraceMatrixVerdictIntegrityInconclusive;

    episode = entries[0].episode;
    if (episode == 0 || entries[0].event !=
        kAirportItlwmPostPltiTraceEventWclPmkReadyScanResume)
        return kAirportItlwmPostPltiTraceMatrixVerdictIntegrityInconclusive;
    phase = airport_itlwm_post_plti_trace_phase_need_state_scan;

    for (uint32_t i = 1; i < count; i++) {
        const uint32_t event = entries[i].event;
        if (entries[i].episode != episode || event ==
            kAirportItlwmPostPltiTraceEventUnknown || event >=
            kAirportItlwmPostPltiTraceEventMax || event ==
            kAirportItlwmPostPltiTraceEventEpisodeAborted)
            return kAirportItlwmPostPltiTraceMatrixVerdictIntegrityInconclusive;
        if (terminal)
            return kAirportItlwmPostPltiTraceMatrixVerdictIntegrityInconclusive;

        if (event == kAirportItlwmPostPltiTraceEventCaptureWindowSealed) {
            terminal = 1;
            terminal_verdict =
                airport_itlwm_post_plti_trace_matrix_sealed_verdict(
                    phase, saw_no_candidate, saw_scan_rejection);
            terminal_missing_stage =
                airport_itlwm_post_plti_trace_matrix_phase_missing_stage(
                    phase, saw_no_candidate);
            continue;
        }
        if (event == kAirportItlwmPostPltiTraceEventPortValidTransition) {
            if (phase != airport_itlwm_post_plti_trace_phase_wait_port_valid)
                return kAirportItlwmPostPltiTraceMatrixVerdictIntegrityInconclusive;
            terminal = 1;
            terminal_verdict =
                kAirportItlwmPostPltiTraceMatrixVerdictKernelChainObserved;
            terminal_missing_stage =
                kAirportItlwmPostPltiTraceMissingStageNone;
            continue;
        }
        if (airport_itlwm_post_plti_trace_matrix_event_is_iwn_software_pmf(
                event))
            continue;
        /* Optional TX corroboration must still follow a real EAPOL enqueue. */
        if (event == kAirportItlwmPostPltiTraceEventEapolFwSubmitted) {
            if (eapol_submitted >= eapol_enqueued)
                return kAirportItlwmPostPltiTraceMatrixVerdictIntegrityInconclusive;
            eapol_submitted++;
            continue;
        }
        if (event == kAirportItlwmPostPltiTraceEventEapolTxDone) {
            if (eapol_done >= eapol_submitted)
                return kAirportItlwmPostPltiTraceMatrixVerdictIntegrityInconclusive;
            eapol_done++;
            continue;
        }

        switch (phase) {
        case airport_itlwm_post_plti_trace_phase_need_state_scan:
            if (event !=
                kAirportItlwmPostPltiTraceEventStateScanSelfRequestObserved)
                return kAirportItlwmPostPltiTraceMatrixVerdictIntegrityInconclusive;
            phase = airport_itlwm_post_plti_trace_phase_need_iwn_scan_state;
            break;
        case airport_itlwm_post_plti_trace_phase_need_iwn_scan_state:
            if (event != kAirportItlwmPostPltiTraceEventIwnScanStateEntered)
                return kAirportItlwmPostPltiTraceMatrixVerdictIntegrityInconclusive;
            phase = airport_itlwm_post_plti_trace_phase_need_scan_outcome;
            break;
        case airport_itlwm_post_plti_trace_phase_need_scan_outcome:
            if (event == kAirportItlwmPostPltiTraceEventIwnScanStarted ||
                event == kAirportItlwmPostPltiTraceEventIwnScanCoalesced) {
                phase = airport_itlwm_post_plti_trace_phase_need_scan_complete;
                break;
            }
            if (event == kAirportItlwmPostPltiTraceEventIwnScanCommandRejected) {
                phase =
                    airport_itlwm_post_plti_trace_phase_need_seal_scan_rejected;
                break;
            }
            return kAirportItlwmPostPltiTraceMatrixVerdictIntegrityInconclusive;
        case airport_itlwm_post_plti_trace_phase_need_scan_complete:
            /* IWN may launch its ordinary second-band pass before completion. */
            if (event == kAirportItlwmPostPltiTraceEventIwnScanStarted)
                break;
            if (event == kAirportItlwmPostPltiTraceEventIwnScanCommandRejected) {
                saw_scan_rejection = 1;
                break;
            }
            if (event != kAirportItlwmPostPltiTraceEventScanCompleted)
                return kAirportItlwmPostPltiTraceMatrixVerdictIntegrityInconclusive;
            phase = airport_itlwm_post_plti_trace_phase_need_selection;
            break;
        case airport_itlwm_post_plti_trace_phase_need_selection:
            if (event == kAirportItlwmPostPltiTraceEventScanNoCandidate) {
                saw_no_candidate = 1;
                phase = airport_itlwm_post_plti_trace_phase_need_state_scan;
                break;
            }
            if (event == kAirportItlwmPostPltiTraceEventSelectionHeld) {
                phase =
                    airport_itlwm_post_plti_trace_phase_need_seal_selection_held;
                break;
            }
            if (event == kAirportItlwmPostPltiTraceEventBssSelected) {
                phase = airport_itlwm_post_plti_trace_phase_need_join;
                break;
            }
            return kAirportItlwmPostPltiTraceMatrixVerdictIntegrityInconclusive;
        case airport_itlwm_post_plti_trace_phase_need_join:
            if (event != kAirportItlwmPostPltiTraceEventJoinBssEntered)
                return kAirportItlwmPostPltiTraceMatrixVerdictIntegrityInconclusive;
            phase = airport_itlwm_post_plti_trace_phase_need_auth_state;
            break;
        case airport_itlwm_post_plti_trace_phase_need_auth_state:
            if (event != kAirportItlwmPostPltiTraceEventAuthStateEntered)
                return kAirportItlwmPostPltiTraceMatrixVerdictIntegrityInconclusive;
            phase = airport_itlwm_post_plti_trace_phase_need_auth_enqueue;
            break;
        case airport_itlwm_post_plti_trace_phase_need_auth_enqueue:
            if (event != kAirportItlwmPostPltiTraceEventAuthEnqueued)
                return kAirportItlwmPostPltiTraceMatrixVerdictIntegrityInconclusive;
            phase = airport_itlwm_post_plti_trace_phase_need_auth_dequeue;
            break;
        case airport_itlwm_post_plti_trace_phase_need_auth_dequeue:
            if (event != kAirportItlwmPostPltiTraceEventAuthDequeued)
                return kAirportItlwmPostPltiTraceMatrixVerdictIntegrityInconclusive;
            phase = airport_itlwm_post_plti_trace_phase_need_auth_submit;
            break;
        case airport_itlwm_post_plti_trace_phase_need_auth_submit:
            if (event != kAirportItlwmPostPltiTraceEventAuthFwSubmitted)
                return kAirportItlwmPostPltiTraceMatrixVerdictIntegrityInconclusive;
            phase = airport_itlwm_post_plti_trace_phase_wait_auth;
            break;
        case airport_itlwm_post_plti_trace_phase_wait_auth:
            if (event == kAirportItlwmPostPltiTraceEventAuthTxDone &&
                !auth_tx_done) {
                auth_tx_done = 1;
                if (auth_net_rx)
                    phase =
                        airport_itlwm_post_plti_trace_phase_need_assoc_state;
                break;
            }
            if (event == kAirportItlwmPostPltiTraceEventAuthRxFromFirmware &&
                !auth_fw_rx) {
                auth_fw_rx = 1;
                break;
            }
            if (event == kAirportItlwmPostPltiTraceEventAuthRxNet80211 &&
                auth_fw_rx && !auth_net_rx) {
                auth_net_rx = 1;
                if (auth_tx_done)
                    phase =
                        airport_itlwm_post_plti_trace_phase_need_assoc_state;
                break;
            }
            return kAirportItlwmPostPltiTraceMatrixVerdictIntegrityInconclusive;
        case airport_itlwm_post_plti_trace_phase_need_assoc_state:
            if (event != kAirportItlwmPostPltiTraceEventAssocStateEntered)
                return kAirportItlwmPostPltiTraceMatrixVerdictIntegrityInconclusive;
            phase = airport_itlwm_post_plti_trace_phase_need_assoc_enqueue;
            break;
        case airport_itlwm_post_plti_trace_phase_need_assoc_enqueue:
            if (event != kAirportItlwmPostPltiTraceEventAssocEnqueued)
                return kAirportItlwmPostPltiTraceMatrixVerdictIntegrityInconclusive;
            phase = airport_itlwm_post_plti_trace_phase_need_assoc_dequeue;
            break;
        case airport_itlwm_post_plti_trace_phase_need_assoc_dequeue:
            if (event != kAirportItlwmPostPltiTraceEventAssocDequeued)
                return kAirportItlwmPostPltiTraceMatrixVerdictIntegrityInconclusive;
            phase = airport_itlwm_post_plti_trace_phase_need_assoc_submit;
            break;
        case airport_itlwm_post_plti_trace_phase_need_assoc_submit:
            if (event != kAirportItlwmPostPltiTraceEventAssocFwSubmitted)
                return kAirportItlwmPostPltiTraceMatrixVerdictIntegrityInconclusive;
            phase = airport_itlwm_post_plti_trace_phase_wait_assoc;
            break;
        case airport_itlwm_post_plti_trace_phase_wait_assoc:
            if (event == kAirportItlwmPostPltiTraceEventAssocTxDone &&
                !assoc_tx_done) {
                assoc_tx_done = 1;
                if (assoc_net_rx)
                    phase = airport_itlwm_post_plti_trace_phase_need_run;
                break;
            }
            if (event == kAirportItlwmPostPltiTraceEventAssocRxFromFirmware &&
                !assoc_fw_rx) {
                assoc_fw_rx = 1;
                break;
            }
            if (event == kAirportItlwmPostPltiTraceEventAssocRxNet80211 &&
                assoc_fw_rx && !assoc_net_rx) {
                assoc_net_rx = 1;
                if (assoc_tx_done)
                    phase = airport_itlwm_post_plti_trace_phase_need_run;
                break;
            }
            return kAirportItlwmPostPltiTraceMatrixVerdictIntegrityInconclusive;
        case airport_itlwm_post_plti_trace_phase_need_run:
            if (event != kAirportItlwmPostPltiTraceEventRunEntered)
                return kAirportItlwmPostPltiTraceMatrixVerdictIntegrityInconclusive;
            phase =
                airport_itlwm_post_plti_trace_phase_need_eapol_decapped;
            break;
        case airport_itlwm_post_plti_trace_phase_need_eapol_decapped:
            if (event != kAirportItlwmPostPltiTraceEventEapolRxDecapped)
                return kAirportItlwmPostPltiTraceMatrixVerdictIntegrityInconclusive;
            phase =
                airport_itlwm_post_plti_trace_phase_need_eapol_kernel_pae;
            break;
        case airport_itlwm_post_plti_trace_phase_need_eapol_kernel_pae:
            if (event != kAirportItlwmPostPltiTraceEventEapolRxKernelPae)
                return kAirportItlwmPostPltiTraceMatrixVerdictIntegrityInconclusive;
            phase =
                airport_itlwm_post_plti_trace_phase_need_eapol_enqueue;
            break;
        case airport_itlwm_post_plti_trace_phase_need_eapol_enqueue:
            if (event != kAirportItlwmPostPltiTraceEventEapolTxEnqueued)
                return kAirportItlwmPostPltiTraceMatrixVerdictIntegrityInconclusive;
            eapol_enqueued++;
            phase =
                airport_itlwm_post_plti_trace_phase_wait_port_valid;
            break;
        case airport_itlwm_post_plti_trace_phase_wait_port_valid:
            if (event == kAirportItlwmPostPltiTraceEventEapolRxDecapped) {
                /* A normal 4-way exchange has more than one inbound EAPOL. */
                phase =
                    airport_itlwm_post_plti_trace_phase_need_eapol_kernel_pae;
                break;
            }
            return kAirportItlwmPostPltiTraceMatrixVerdictIntegrityInconclusive;
        default:
            return kAirportItlwmPostPltiTraceMatrixVerdictIntegrityInconclusive;
        }
    }

    if (terminal && out_missing_stage != 0)
        *out_missing_stage = terminal_missing_stage;
    return terminal ? terminal_verdict :
        kAirportItlwmPostPltiTraceMatrixVerdictIntegrityInconclusive;
}

static inline enum AirportItlwmPostPltiTraceMatrixVerdict
airport_itlwm_post_plti_trace_matrix_classify_entries(
    const AirportItlwmPostPltiTraceEntry *entries, uint32_t count,
    int integrity, uint32_t backend, uint32_t episode_count,
    uint32_t active_episode)
{
    return airport_itlwm_post_plti_trace_matrix_classify_entries_with_stage(
        entries, count, integrity, backend, episode_count, active_episode, 0);
}

#endif /* AirportItlwmPostPltiTraceMatrixContracts_h */
