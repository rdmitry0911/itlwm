#ifndef AirportItlwmIwxPmfBipTraceContracts_h
#define AirportItlwmIwxPmfBipTraceContracts_h

/*
 * IWX-specific PMF/BIP safe-trace evaluator.
 *
 * The existing post-PLTI matrix intentionally remains the IWN ordered
 * association evaluator.  This companion accepts only the already-sanitised
 * IWX categorical owner facts required to distinguish an initial IGTK
 * publication from a cross-slot 4 <-> 5 rekey.  It never accepts a bitset,
 * a raw frame, a firmware result, or any key-derived value.
 */

#include <stdint.h>

#include <ClientKit/AirportItlwmPostPltiTrace.h>

enum AirportItlwmIwxPmfBipTraceVerdict {
    kAirportItlwmIwxPmfBipTraceVerdictIntegrityInconclusive = 0,
    kAirportItlwmIwxPmfBipTraceVerdictBackendUnsupported,
    kAirportItlwmIwxPmfBipTraceVerdictBranchNotObserved,
    kAirportItlwmIwxPmfBipTraceVerdictPmfRxNotObserved,
    kAirportItlwmIwxPmfBipTraceVerdictQ0DoorbellNotObserved,
    kAirportItlwmIwxPmfBipTraceVerdictQ0CompletionNotObserved,
    kAirportItlwmIwxPmfBipTraceVerdictIgtkPublicationNotObserved,
    kAirportItlwmIwxPmfBipTraceVerdictActiveSlotNotObserved,
    kAirportItlwmIwxPmfBipTraceVerdictPortValidNotObserved,
    kAirportItlwmIwxPmfBipTraceVerdictInitialPmfBipObserved,
    kAirportItlwmIwxPmfBipTraceVerdictCrossSlotRekeyObserved,
};

enum AirportItlwmIwxPmfBipTraceMissingStage {
    kAirportItlwmIwxPmfBipTraceMissingStageNone = 0,
    kAirportItlwmIwxPmfBipTraceMissingStageCaptureSeal,
    kAirportItlwmIwxPmfBipTraceMissingStagePmfRx,
    kAirportItlwmIwxPmfBipTraceMissingStageQ0Doorbell,
    kAirportItlwmIwxPmfBipTraceMissingStageQ0Completion,
    kAirportItlwmIwxPmfBipTraceMissingStageIgtkPublication,
    kAirportItlwmIwxPmfBipTraceMissingStageActiveSlot,
    kAirportItlwmIwxPmfBipTraceMissingStagePortValid,
    kAirportItlwmIwxPmfBipTraceMissingStageCrossSlotRekey,
    kAirportItlwmIwxPmfBipTraceMissingStageUnknown,
};

/* A progress result is deliberately narrower than a sealed verdict.  It lets
 * a bounded runner authorize a later group-rekey request only after the
 * initial post-acknowledgement PMF/BIP chain has been observed in the still
 * active episode.  It never turns an unsealed episode into a final success. */
enum AirportItlwmIwxPmfBipTraceInitialProgress {
    kAirportItlwmIwxPmfBipTraceInitialProgressIntegrityInconclusive = 0,
    kAirportItlwmIwxPmfBipTraceInitialProgressBackendUnsupported,
    kAirportItlwmIwxPmfBipTraceInitialProgressBranchNotObserved,
    kAirportItlwmIwxPmfBipTraceInitialProgressInitialPmfBipReady,
};

enum airport_itlwm_iwx_pmf_bip_trace_phase {
    airport_itlwm_iwx_pmf_bip_trace_phase_need_pmf_rx = 0,
    airport_itlwm_iwx_pmf_bip_trace_phase_need_q0_doorbell,
    airport_itlwm_iwx_pmf_bip_trace_phase_wait_q0_completion,
    airport_itlwm_iwx_pmf_bip_trace_phase_need_igtk_publication,
    airport_itlwm_iwx_pmf_bip_trace_phase_need_active_slot,
};

static inline void
airport_itlwm_iwx_pmf_bip_trace_set_stage(
    enum AirportItlwmIwxPmfBipTraceMissingStage *out_stage,
    enum AirportItlwmIwxPmfBipTraceMissingStage stage)
{
    if (out_stage != 0)
        *out_stage = stage;
}

static inline enum AirportItlwmIwxPmfBipTraceMissingStage
airport_itlwm_iwx_pmf_bip_trace_phase_stage(
    enum airport_itlwm_iwx_pmf_bip_trace_phase phase)
{
    switch (phase) {
    case airport_itlwm_iwx_pmf_bip_trace_phase_need_pmf_rx:
        return kAirportItlwmIwxPmfBipTraceMissingStagePmfRx;
    case airport_itlwm_iwx_pmf_bip_trace_phase_need_q0_doorbell:
        return kAirportItlwmIwxPmfBipTraceMissingStageQ0Doorbell;
    case airport_itlwm_iwx_pmf_bip_trace_phase_wait_q0_completion:
        return kAirportItlwmIwxPmfBipTraceMissingStageQ0Completion;
    case airport_itlwm_iwx_pmf_bip_trace_phase_need_igtk_publication:
        return kAirportItlwmIwxPmfBipTraceMissingStageIgtkPublication;
    case airport_itlwm_iwx_pmf_bip_trace_phase_need_active_slot:
        return kAirportItlwmIwxPmfBipTraceMissingStageActiveSlot;
    default:
        return kAirportItlwmIwxPmfBipTraceMissingStageUnknown;
    }
}

static inline uint32_t
airport_itlwm_iwx_pmf_bip_trace_published_slot(uint32_t event)
{
    switch (event) {
    case kAirportItlwmPostPltiTraceEventIwxIgtkSlot4Published:
        return 4;
    case kAirportItlwmPostPltiTraceEventIwxIgtkSlot5Published:
        return 5;
    default:
        return 0;
    }
}

static inline uint32_t
airport_itlwm_iwx_pmf_bip_trace_selected_slot(uint32_t event)
{
    switch (event) {
    case kAirportItlwmPostPltiTraceEventIwxIgtkSlot4TxSelected:
        return 4;
    case kAirportItlwmPostPltiTraceEventIwxIgtkSlot5TxSelected:
        return 5;
    default:
        return 0;
    }
}

/* Generic association markers are tolerated but never contribute evidence.
 * They are shared producers that can occur around IWX's asynchronous PMF
 * worker, while IWN-only events cannot make this IWX evaluator succeed. */
static inline int
airport_itlwm_iwx_pmf_bip_trace_event_is_neutral(uint32_t event)
{
    switch (event) {
    case kAirportItlwmPostPltiTraceEventIwnScanCoalesced:
    case kAirportItlwmPostPltiTraceEventIwnScanStarted:
    case kAirportItlwmPostPltiTraceEventScanCompleted:
    case kAirportItlwmPostPltiTraceEventSelectionHeld:
    case kAirportItlwmPostPltiTraceEventBssSelected:
    case kAirportItlwmPostPltiTraceEventJoinBssEntered:
    case kAirportItlwmPostPltiTraceEventAuthStateEntered:
    case kAirportItlwmPostPltiTraceEventAuthEnqueued:
    case kAirportItlwmPostPltiTraceEventAuthDequeued:
    case kAirportItlwmPostPltiTraceEventAuthFwSubmitted:
    case kAirportItlwmPostPltiTraceEventAuthTxDone:
    case kAirportItlwmPostPltiTraceEventAuthRxFromFirmware:
    case kAirportItlwmPostPltiTraceEventAuthRxNet80211:
    case kAirportItlwmPostPltiTraceEventAssocStateEntered:
    case kAirportItlwmPostPltiTraceEventAssocEnqueued:
    case kAirportItlwmPostPltiTraceEventAssocDequeued:
    case kAirportItlwmPostPltiTraceEventAssocFwSubmitted:
    case kAirportItlwmPostPltiTraceEventAssocTxDone:
    case kAirportItlwmPostPltiTraceEventAssocRxFromFirmware:
    case kAirportItlwmPostPltiTraceEventAssocRxNet80211:
    case kAirportItlwmPostPltiTraceEventRunEntered:
    case kAirportItlwmPostPltiTraceEventEapolRxDecapped:
    case kAirportItlwmPostPltiTraceEventEapolRxKernelPae:
    case kAirportItlwmPostPltiTraceEventEapolTxEnqueued:
    case kAirportItlwmPostPltiTraceEventEapolFwSubmitted:
    case kAirportItlwmPostPltiTraceEventEapolTxDone:
    case kAirportItlwmPostPltiTraceEventStateScanSelfRequestObserved:
    case kAirportItlwmPostPltiTraceEventIwnScanStateEntered:
    case kAirportItlwmPostPltiTraceEventIwnScanCommandRejected:
    case kAirportItlwmPostPltiTraceEventScanNoCandidate:
        return 1;
    default:
        return 0;
    }
}

static inline enum AirportItlwmIwxPmfBipTraceVerdict
airport_itlwm_iwx_pmf_bip_trace_classify_entries_with_stage(
    const AirportItlwmPostPltiTraceEntry *entries, uint32_t count,
    int integrity, uint32_t backend, uint32_t episode_count,
    uint32_t active_episode,
    enum AirportItlwmIwxPmfBipTraceMissingStage *out_stage)
{
    enum airport_itlwm_iwx_pmf_bip_trace_phase phase =
        airport_itlwm_iwx_pmf_bip_trace_phase_need_pmf_rx;
    uint32_t episode;
    uint32_t generation;
    uint32_t active_slot = 0;
    uint32_t published_slot = 0;
    uint32_t saw_pmf_rx = 0;
    uint32_t saw_q0_doorbell = 0;
    uint32_t saw_q0_completion = 0;
    uint32_t saw_igtk_publication = 0;
    uint32_t initial_active = 0;
    uint32_t port_valid = 0;
    uint32_t rekey_count = 0;
    uint32_t terminal = 0;

    airport_itlwm_iwx_pmf_bip_trace_set_stage(
        out_stage, kAirportItlwmIwxPmfBipTraceMissingStageUnknown);
    if (!integrity)
        return kAirportItlwmIwxPmfBipTraceVerdictIntegrityInconclusive;
    if (backend != kAirportItlwmPostPltiTraceBackendIwx)
        return kAirportItlwmIwxPmfBipTraceVerdictBackendUnsupported;
    if (episode_count == 0) {
        if (count == 0 && active_episode == 0) {
            airport_itlwm_iwx_pmf_bip_trace_set_stage(
                out_stage, kAirportItlwmIwxPmfBipTraceMissingStageNone);
            return kAirportItlwmIwxPmfBipTraceVerdictBranchNotObserved;
        }
        return kAirportItlwmIwxPmfBipTraceVerdictIntegrityInconclusive;
    }
    if (episode_count != 1 || entries == 0 || count == 0)
        return kAirportItlwmIwxPmfBipTraceVerdictIntegrityInconclusive;
    if (active_episode != 0) {
        airport_itlwm_iwx_pmf_bip_trace_set_stage(
            out_stage, kAirportItlwmIwxPmfBipTraceMissingStageCaptureSeal);
        return kAirportItlwmIwxPmfBipTraceVerdictIntegrityInconclusive;
    }

    episode = entries[0].episode;
    generation = entries[0].captureGeneration;
    if (episode == 0 || generation == 0 || entries[0].sequence == 0 ||
        entries[0].event !=
            kAirportItlwmPostPltiTraceEventWclPmkReadyScanResume)
        return kAirportItlwmIwxPmfBipTraceVerdictIntegrityInconclusive;

    for (uint32_t i = 1; i < count; i++) {
        const uint32_t event = entries[i].event;
        const uint32_t publication_slot =
            airport_itlwm_iwx_pmf_bip_trace_published_slot(event);
        const uint32_t selected_slot =
            airport_itlwm_iwx_pmf_bip_trace_selected_slot(event);

        if (entries[i].sequence != entries[0].sequence + i ||
            entries[i].captureGeneration != generation ||
            entries[i].episode != episode || event ==
                kAirportItlwmPostPltiTraceEventUnknown || event >=
                kAirportItlwmPostPltiTraceEventMax || terminal)
            return kAirportItlwmIwxPmfBipTraceVerdictIntegrityInconclusive;

        if (event == kAirportItlwmPostPltiTraceEventEpisodeAborted) {
            airport_itlwm_iwx_pmf_bip_trace_set_stage(
                out_stage,
                airport_itlwm_iwx_pmf_bip_trace_phase_stage(phase));
            return kAirportItlwmIwxPmfBipTraceVerdictIntegrityInconclusive;
        }
        if (event == kAirportItlwmPostPltiTraceEventCaptureWindowSealed) {
            terminal = 1;
            continue;
        }
        if (event == kAirportItlwmPostPltiTraceEventWclPmkReadyScanResume)
            return kAirportItlwmIwxPmfBipTraceVerdictIntegrityInconclusive;
        if (event == kAirportItlwmPostPltiTraceEventPortValidTransition) {
            if (!initial_active || port_valid || phase !=
                    airport_itlwm_iwx_pmf_bip_trace_phase_need_pmf_rx) {
                airport_itlwm_iwx_pmf_bip_trace_set_stage(
                    out_stage,
                    airport_itlwm_iwx_pmf_bip_trace_phase_stage(phase));
                return kAirportItlwmIwxPmfBipTraceVerdictIntegrityInconclusive;
            }
            port_valid = 1;
            continue;
        }
        if (airport_itlwm_iwx_pmf_bip_trace_event_is_neutral(event))
            continue;

        if (event == kAirportItlwmPostPltiTraceEventIwxMfpPaeRxDelivered) {
            if (phase != airport_itlwm_iwx_pmf_bip_trace_phase_need_pmf_rx &&
                phase != airport_itlwm_iwx_pmf_bip_trace_phase_need_q0_doorbell &&
                phase != airport_itlwm_iwx_pmf_bip_trace_phase_need_igtk_publication) {
                airport_itlwm_iwx_pmf_bip_trace_set_stage(
                    out_stage,
                    airport_itlwm_iwx_pmf_bip_trace_phase_stage(phase));
                return kAirportItlwmIwxPmfBipTraceVerdictIntegrityInconclusive;
            }
            saw_pmf_rx = 1;
            phase = airport_itlwm_iwx_pmf_bip_trace_phase_need_q0_doorbell;
            continue;
        }
        if (event == kAirportItlwmPostPltiTraceEventIwxMfpPaeQ0Doorbelled) {
            if (phase != airport_itlwm_iwx_pmf_bip_trace_phase_need_q0_doorbell &&
                phase != airport_itlwm_iwx_pmf_bip_trace_phase_need_igtk_publication) {
                airport_itlwm_iwx_pmf_bip_trace_set_stage(
                    out_stage,
                    phase == airport_itlwm_iwx_pmf_bip_trace_phase_need_pmf_rx ?
                        kAirportItlwmIwxPmfBipTraceMissingStagePmfRx :
                        airport_itlwm_iwx_pmf_bip_trace_phase_stage(phase));
                return kAirportItlwmIwxPmfBipTraceVerdictIntegrityInconclusive;
            }
            saw_q0_doorbell = 1;
            phase =
                airport_itlwm_iwx_pmf_bip_trace_phase_wait_q0_completion;
            continue;
        }
        if (event ==
            kAirportItlwmPostPltiTraceEventIwxMfpPaeQ0CompletionObserved) {
            if (phase !=
                airport_itlwm_iwx_pmf_bip_trace_phase_wait_q0_completion) {
                airport_itlwm_iwx_pmf_bip_trace_set_stage(
                    out_stage,
                    phase == airport_itlwm_iwx_pmf_bip_trace_phase_need_q0_doorbell ?
                        kAirportItlwmIwxPmfBipTraceMissingStageQ0Doorbell :
                        kAirportItlwmIwxPmfBipTraceMissingStageQ0Completion);
                return kAirportItlwmIwxPmfBipTraceVerdictIntegrityInconclusive;
            }
            saw_q0_completion = 1;
            phase =
                airport_itlwm_iwx_pmf_bip_trace_phase_need_igtk_publication;
            continue;
        }
        if (publication_slot != 0) {
            if (phase !=
                airport_itlwm_iwx_pmf_bip_trace_phase_need_igtk_publication) {
                airport_itlwm_iwx_pmf_bip_trace_set_stage(
                    out_stage,
                    phase == airport_itlwm_iwx_pmf_bip_trace_phase_need_pmf_rx ?
                        kAirportItlwmIwxPmfBipTraceMissingStagePmfRx :
                        airport_itlwm_iwx_pmf_bip_trace_phase_stage(phase));
                return kAirportItlwmIwxPmfBipTraceVerdictIntegrityInconclusive;
            }
            saw_igtk_publication = 1;
            published_slot = publication_slot;
            phase =
                airport_itlwm_iwx_pmf_bip_trace_phase_need_active_slot;
            continue;
        }
        if (selected_slot != 0) {
            if (phase !=
                    airport_itlwm_iwx_pmf_bip_trace_phase_need_active_slot ||
                selected_slot != published_slot) {
                airport_itlwm_iwx_pmf_bip_trace_set_stage(
                    out_stage, initial_active ?
                        kAirportItlwmIwxPmfBipTraceMissingStageCrossSlotRekey :
                        kAirportItlwmIwxPmfBipTraceMissingStageIgtkPublication);
                return kAirportItlwmIwxPmfBipTraceVerdictIntegrityInconclusive;
            }
            if (!initial_active) {
                initial_active = 1;
            } else {
                /* The bounded runner authorizes one REKEY_GTK request only.
                 * A second otherwise-valid cross-slot chain is not evidence
                 * for that request: retain no ambiguous success verdict. */
                if (!port_valid || selected_slot == active_slot ||
                    rekey_count != 0) {
                    airport_itlwm_iwx_pmf_bip_trace_set_stage(
                        out_stage,
                        kAirportItlwmIwxPmfBipTraceMissingStageCrossSlotRekey);
                    return kAirportItlwmIwxPmfBipTraceVerdictIntegrityInconclusive;
                }
                rekey_count++;
            }
            active_slot = selected_slot;
            published_slot = 0;
            phase = airport_itlwm_iwx_pmf_bip_trace_phase_need_pmf_rx;
            continue;
        }

        return kAirportItlwmIwxPmfBipTraceVerdictIntegrityInconclusive;
    }

    if (!terminal) {
        airport_itlwm_iwx_pmf_bip_trace_set_stage(
            out_stage, kAirportItlwmIwxPmfBipTraceMissingStageCaptureSeal);
        return kAirportItlwmIwxPmfBipTraceVerdictIntegrityInconclusive;
    }
    if (!initial_active) {
        enum AirportItlwmIwxPmfBipTraceMissingStage stage =
            airport_itlwm_iwx_pmf_bip_trace_phase_stage(phase);
        if (!saw_pmf_rx)
            stage = kAirportItlwmIwxPmfBipTraceMissingStagePmfRx;
        else if (!saw_q0_doorbell)
            stage = kAirportItlwmIwxPmfBipTraceMissingStageQ0Doorbell;
        else if (!saw_q0_completion)
            stage = kAirportItlwmIwxPmfBipTraceMissingStageQ0Completion;
        else if (!saw_igtk_publication)
            stage = kAirportItlwmIwxPmfBipTraceMissingStageIgtkPublication;
        airport_itlwm_iwx_pmf_bip_trace_set_stage(out_stage, stage);
        switch (stage) {
        case kAirportItlwmIwxPmfBipTraceMissingStagePmfRx:
            return kAirportItlwmIwxPmfBipTraceVerdictPmfRxNotObserved;
        case kAirportItlwmIwxPmfBipTraceMissingStageQ0Doorbell:
            return kAirportItlwmIwxPmfBipTraceVerdictQ0DoorbellNotObserved;
        case kAirportItlwmIwxPmfBipTraceMissingStageQ0Completion:
            return kAirportItlwmIwxPmfBipTraceVerdictQ0CompletionNotObserved;
        case kAirportItlwmIwxPmfBipTraceMissingStageIgtkPublication:
            return kAirportItlwmIwxPmfBipTraceVerdictIgtkPublicationNotObserved;
        default:
            return kAirportItlwmIwxPmfBipTraceVerdictActiveSlotNotObserved;
        }
    }
    if (!port_valid) {
        airport_itlwm_iwx_pmf_bip_trace_set_stage(
            out_stage, kAirportItlwmIwxPmfBipTraceMissingStagePortValid);
        return kAirportItlwmIwxPmfBipTraceVerdictPortValidNotObserved;
    }
    if (phase != airport_itlwm_iwx_pmf_bip_trace_phase_need_pmf_rx) {
        airport_itlwm_iwx_pmf_bip_trace_set_stage(
            out_stage, airport_itlwm_iwx_pmf_bip_trace_phase_stage(phase));
        return kAirportItlwmIwxPmfBipTraceVerdictIntegrityInconclusive;
    }
    airport_itlwm_iwx_pmf_bip_trace_set_stage(
        out_stage, kAirportItlwmIwxPmfBipTraceMissingStageNone);
    return rekey_count == 1 ?
        kAirportItlwmIwxPmfBipTraceVerdictCrossSlotRekeyObserved :
        kAirportItlwmIwxPmfBipTraceVerdictInitialPmfBipObserved;
}

static inline enum AirportItlwmIwxPmfBipTraceInitialProgress
airport_itlwm_iwx_pmf_bip_trace_classify_initial_prefix_with_stage(
    const AirportItlwmPostPltiTraceEntry *entries, uint32_t count,
    int integrity, uint32_t backend, uint32_t episode_count,
    uint32_t active_episode,
    enum AirportItlwmIwxPmfBipTraceMissingStage *out_stage)
{
    enum airport_itlwm_iwx_pmf_bip_trace_phase phase =
        airport_itlwm_iwx_pmf_bip_trace_phase_need_pmf_rx;
    uint32_t episode;
    uint32_t generation;
    uint32_t published_slot = 0;
    uint32_t initial_active = 0;
    uint32_t port_valid = 0;

    airport_itlwm_iwx_pmf_bip_trace_set_stage(
        out_stage, kAirportItlwmIwxPmfBipTraceMissingStageUnknown);
    if (!integrity)
        return kAirportItlwmIwxPmfBipTraceInitialProgressIntegrityInconclusive;
    if (backend != kAirportItlwmPostPltiTraceBackendIwx)
        return kAirportItlwmIwxPmfBipTraceInitialProgressBackendUnsupported;
    if (episode_count == 0) {
        if (count == 0 && active_episode == 0) {
            airport_itlwm_iwx_pmf_bip_trace_set_stage(
                out_stage, kAirportItlwmIwxPmfBipTraceMissingStageNone);
            return kAirportItlwmIwxPmfBipTraceInitialProgressBranchNotObserved;
        }
        return kAirportItlwmIwxPmfBipTraceInitialProgressIntegrityInconclusive;
    }
    if (episode_count != 1 || active_episode == 0 || entries == 0 ||
        count == 0)
        return kAirportItlwmIwxPmfBipTraceInitialProgressIntegrityInconclusive;

    episode = entries[0].episode;
    generation = entries[0].captureGeneration;
    if (episode == 0 || generation == 0 || active_episode != episode ||
        entries[0].sequence == 0 || entries[0].event !=
            kAirportItlwmPostPltiTraceEventWclPmkReadyScanResume)
        return kAirportItlwmIwxPmfBipTraceInitialProgressIntegrityInconclusive;

    for (uint32_t i = 1; i < count; i++) {
        const uint32_t event = entries[i].event;
        const uint32_t publication_slot =
            airport_itlwm_iwx_pmf_bip_trace_published_slot(event);
        const uint32_t selected_slot =
            airport_itlwm_iwx_pmf_bip_trace_selected_slot(event);

        if (entries[i].sequence != entries[0].sequence + i ||
            entries[i].captureGeneration != generation ||
            entries[i].episode != episode || event ==
                kAirportItlwmPostPltiTraceEventUnknown || event >=
                kAirportItlwmPostPltiTraceEventMax || event ==
                kAirportItlwmPostPltiTraceEventEpisodeAborted || event ==
                kAirportItlwmPostPltiTraceEventCaptureWindowSealed)
            return kAirportItlwmIwxPmfBipTraceInitialProgressIntegrityInconclusive;

        if (airport_itlwm_iwx_pmf_bip_trace_event_is_neutral(event))
            continue;
        if (event == kAirportItlwmPostPltiTraceEventPortValidTransition) {
            if (!initial_active || port_valid || phase !=
                    airport_itlwm_iwx_pmf_bip_trace_phase_need_pmf_rx) {
                airport_itlwm_iwx_pmf_bip_trace_set_stage(
                    out_stage,
                    airport_itlwm_iwx_pmf_bip_trace_phase_stage(phase));
                return kAirportItlwmIwxPmfBipTraceInitialProgressIntegrityInconclusive;
            }
            port_valid = 1;
            continue;
        }
        if (port_valid) {
            airport_itlwm_iwx_pmf_bip_trace_set_stage(
                out_stage, kAirportItlwmIwxPmfBipTraceMissingStageCrossSlotRekey);
            return kAirportItlwmIwxPmfBipTraceInitialProgressIntegrityInconclusive;
        }
        if (event == kAirportItlwmPostPltiTraceEventIwxMfpPaeRxDelivered) {
            if (phase != airport_itlwm_iwx_pmf_bip_trace_phase_need_pmf_rx &&
                phase != airport_itlwm_iwx_pmf_bip_trace_phase_need_q0_doorbell &&
                phase != airport_itlwm_iwx_pmf_bip_trace_phase_need_igtk_publication) {
                airport_itlwm_iwx_pmf_bip_trace_set_stage(
                    out_stage,
                    airport_itlwm_iwx_pmf_bip_trace_phase_stage(phase));
                return kAirportItlwmIwxPmfBipTraceInitialProgressIntegrityInconclusive;
            }
            phase = airport_itlwm_iwx_pmf_bip_trace_phase_need_q0_doorbell;
            continue;
        }
        if (event == kAirportItlwmPostPltiTraceEventIwxMfpPaeQ0Doorbelled) {
            if (phase != airport_itlwm_iwx_pmf_bip_trace_phase_need_q0_doorbell &&
                phase != airport_itlwm_iwx_pmf_bip_trace_phase_need_igtk_publication) {
                airport_itlwm_iwx_pmf_bip_trace_set_stage(
                    out_stage,
                    airport_itlwm_iwx_pmf_bip_trace_phase_stage(phase));
                return kAirportItlwmIwxPmfBipTraceInitialProgressIntegrityInconclusive;
            }
            phase =
                airport_itlwm_iwx_pmf_bip_trace_phase_wait_q0_completion;
            continue;
        }
        if (event ==
            kAirportItlwmPostPltiTraceEventIwxMfpPaeQ0CompletionObserved) {
            if (phase !=
                airport_itlwm_iwx_pmf_bip_trace_phase_wait_q0_completion) {
                airport_itlwm_iwx_pmf_bip_trace_set_stage(
                    out_stage,
                    airport_itlwm_iwx_pmf_bip_trace_phase_stage(phase));
                return kAirportItlwmIwxPmfBipTraceInitialProgressIntegrityInconclusive;
            }
            phase =
                airport_itlwm_iwx_pmf_bip_trace_phase_need_igtk_publication;
            continue;
        }
        if (publication_slot != 0) {
            if (phase !=
                airport_itlwm_iwx_pmf_bip_trace_phase_need_igtk_publication) {
                airport_itlwm_iwx_pmf_bip_trace_set_stage(
                    out_stage,
                    airport_itlwm_iwx_pmf_bip_trace_phase_stage(phase));
                return kAirportItlwmIwxPmfBipTraceInitialProgressIntegrityInconclusive;
            }
            published_slot = publication_slot;
            phase =
                airport_itlwm_iwx_pmf_bip_trace_phase_need_active_slot;
            continue;
        }
        if (selected_slot != 0) {
            if (phase !=
                    airport_itlwm_iwx_pmf_bip_trace_phase_need_active_slot ||
                selected_slot != published_slot) {
                airport_itlwm_iwx_pmf_bip_trace_set_stage(
                    out_stage, kAirportItlwmIwxPmfBipTraceMissingStageIgtkPublication);
                return kAirportItlwmIwxPmfBipTraceInitialProgressIntegrityInconclusive;
            }
            initial_active = 1;
            published_slot = 0;
            phase = airport_itlwm_iwx_pmf_bip_trace_phase_need_pmf_rx;
            continue;
        }
        return kAirportItlwmIwxPmfBipTraceInitialProgressIntegrityInconclusive;
    }

    if (initial_active && port_valid && phase ==
            airport_itlwm_iwx_pmf_bip_trace_phase_need_pmf_rx) {
        airport_itlwm_iwx_pmf_bip_trace_set_stage(
            out_stage, kAirportItlwmIwxPmfBipTraceMissingStageNone);
        return kAirportItlwmIwxPmfBipTraceInitialProgressInitialPmfBipReady;
    }
    airport_itlwm_iwx_pmf_bip_trace_set_stage(
        out_stage, airport_itlwm_iwx_pmf_bip_trace_phase_stage(phase));
    return kAirportItlwmIwxPmfBipTraceInitialProgressIntegrityInconclusive;
}

static inline enum AirportItlwmIwxPmfBipTraceVerdict
airport_itlwm_iwx_pmf_bip_trace_classify_entries(
    const AirportItlwmPostPltiTraceEntry *entries, uint32_t count,
    int integrity, uint32_t backend, uint32_t episode_count,
    uint32_t active_episode)
{
    return airport_itlwm_iwx_pmf_bip_trace_classify_entries_with_stage(
        entries, count, integrity, backend, episode_count, active_episode,
        0);
}

static inline enum AirportItlwmIwxPmfBipTraceInitialProgress
airport_itlwm_iwx_pmf_bip_trace_classify_initial_prefix(
    const AirportItlwmPostPltiTraceEntry *entries, uint32_t count,
    int integrity, uint32_t backend, uint32_t episode_count,
    uint32_t active_episode)
{
    return airport_itlwm_iwx_pmf_bip_trace_classify_initial_prefix_with_stage(
        entries, count, integrity, backend, episode_count, active_episode,
        0);
}

#endif /* AirportItlwmIwxPmfBipTraceContracts_h */
