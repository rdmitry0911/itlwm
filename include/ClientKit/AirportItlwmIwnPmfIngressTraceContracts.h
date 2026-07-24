#ifndef AirportItlwmIwnPmfIngressTraceContracts_h
#define AirportItlwmIwnPmfIngressTraceContracts_h

/*
 * Counter-only evaluator for the two safe IWN PMF ingress boundaries.
 *
 * It consumes only the already-sanitised post-PLTI trace ring.  A positive
 * result says that the WCL PMF request was retained at scan resume and that
 * net80211 subsequently set NODE_MFP for the selected BSS.  It does not
 * establish a completed PMF key transaction, protected-frame exchange, or
 * WPA3/SAE functionality; those owners have separate evaluators.
 */

#include <stdint.h>

#include <ClientKit/AirportItlwmPostPltiTrace.h>

enum AirportItlwmIwnPmfIngressTraceVerdict {
    kAirportItlwmIwnPmfIngressTraceVerdictIntegrityInconclusive = 0,
    kAirportItlwmIwnPmfIngressTraceVerdictBackendUnsupported,
    kAirportItlwmIwnPmfIngressTraceVerdictBranchNotObserved,
    kAirportItlwmIwnPmfIngressTraceVerdictWclPmfRequestNotObserved,
    kAirportItlwmIwnPmfIngressTraceVerdictBssSelectionNotObserved,
    kAirportItlwmIwnPmfIngressTraceVerdictJoinBssNotObserved,
    kAirportItlwmIwnPmfIngressTraceVerdictNodeMfpNotNegotiated,
    kAirportItlwmIwnPmfIngressTraceVerdictNodeMfpNegotiated,
};

enum AirportItlwmIwnPmfIngressTraceMissingStage {
    kAirportItlwmIwnPmfIngressTraceMissingStageNone = 0,
    kAirportItlwmIwnPmfIngressTraceMissingStageCaptureSeal,
    kAirportItlwmIwnPmfIngressTraceMissingStageWclPmfRequest,
    kAirportItlwmIwnPmfIngressTraceMissingStageBssSelection,
    kAirportItlwmIwnPmfIngressTraceMissingStageJoinBss,
    kAirportItlwmIwnPmfIngressTraceMissingStageNodeMfp,
    kAirportItlwmIwnPmfIngressTraceMissingStageUnknown,
};

static inline void
airport_itlwm_iwn_pmf_ingress_trace_set_stage(
    enum AirportItlwmIwnPmfIngressTraceMissingStage *out_stage,
    enum AirportItlwmIwnPmfIngressTraceMissingStage stage)
{
    if (out_stage != 0)
        *out_stage = stage;
}

/* The generic chain and IWN software-PMF owner have independent evaluators.
 * They remain neutral here, while foreign IWX PMF facts fail closed. */
static inline int
airport_itlwm_iwn_pmf_ingress_trace_event_is_neutral(uint32_t event)
{
    return (event >= kAirportItlwmPostPltiTraceEventIwnScanCoalesced &&
            event <= kAirportItlwmPostPltiTraceEventScanNoCandidate) ||
        (event >= AIRPORT_ITLWM_POST_PLTI_TRACE_IWN_SOFTWARE_PMF_EVENT_FIRST &&
         event <= AIRPORT_ITLWM_POST_PLTI_TRACE_IWN_SOFTWARE_PMF_EVENT_LAST);
}

static inline enum AirportItlwmIwnPmfIngressTraceVerdict
airport_itlwm_iwn_pmf_ingress_trace_classify_entries_with_stage(
    const AirportItlwmPostPltiTraceEntry *entries, uint32_t count,
    int integrity, uint32_t backend, uint32_t episode_count,
    uint32_t active_episode,
    enum AirportItlwmIwnPmfIngressTraceMissingStage *out_stage)
{
    uint32_t episode, generation;
    uint32_t wcl_pmf_request = 0, bss_selected = 0, join_bss = 0;
    uint32_t node_mfp = 0, terminal = 0;
    uint32_t previous_event;

    airport_itlwm_iwn_pmf_ingress_trace_set_stage(
        out_stage, kAirportItlwmIwnPmfIngressTraceMissingStageUnknown);
    if (!integrity)
        return kAirportItlwmIwnPmfIngressTraceVerdictIntegrityInconclusive;
    if (backend != kAirportItlwmPostPltiTraceBackendIwn)
        return kAirportItlwmIwnPmfIngressTraceVerdictBackendUnsupported;
    if (episode_count == 0) {
        if (count == 0 && active_episode == 0) {
            airport_itlwm_iwn_pmf_ingress_trace_set_stage(
                out_stage, kAirportItlwmIwnPmfIngressTraceMissingStageNone);
            return kAirportItlwmIwnPmfIngressTraceVerdictBranchNotObserved;
        }
        return kAirportItlwmIwnPmfIngressTraceVerdictIntegrityInconclusive;
    }
    if (episode_count != 1 || active_episode != 0 || entries == 0 ||
        count == 0)
        return kAirportItlwmIwnPmfIngressTraceVerdictIntegrityInconclusive;

    episode = entries[0].episode;
    generation = entries[0].captureGeneration;
    if (episode == 0 || generation == 0 || entries[0].sequence == 0 ||
        entries[0].event !=
            kAirportItlwmPostPltiTraceEventWclPmkReadyScanResume)
        return kAirportItlwmIwnPmfIngressTraceVerdictIntegrityInconclusive;
    previous_event = entries[0].event;

    for (uint32_t i = 1; i < count; i++) {
        const uint32_t event = entries[i].event;

        if (entries[i].sequence != entries[0].sequence + i ||
            entries[i].captureGeneration != generation ||
            entries[i].episode != episode || event ==
                kAirportItlwmPostPltiTraceEventUnknown || event >=
                kAirportItlwmPostPltiTraceEventMax || terminal)
            return kAirportItlwmIwnPmfIngressTraceVerdictIntegrityInconclusive;
        if (event == kAirportItlwmPostPltiTraceEventEpisodeAborted ||
            event == kAirportItlwmPostPltiTraceEventWclPmkReadyScanResume)
            return kAirportItlwmIwnPmfIngressTraceVerdictIntegrityInconclusive;
        if (event == kAirportItlwmPostPltiTraceEventCaptureWindowSealed ||
            event == kAirportItlwmPostPltiTraceEventPortValidTransition) {
            terminal = 1;
            continue;
        }
        if (event ==
            kAirportItlwmPostPltiTraceEventWclPmfRequestRetained) {
            /* BeginEpisode writes WCL_PMK_READY first; the WCL fact follows
             * it before any scan-state producer can run. */
            if (i != 1 || wcl_pmf_request)
                return kAirportItlwmIwnPmfIngressTraceVerdictIntegrityInconclusive;
            wcl_pmf_request = 1;
            previous_event = event;
            continue;
        }
        if (event == kAirportItlwmPostPltiTraceEventBssSelected) {
            if (bss_selected)
                return kAirportItlwmIwnPmfIngressTraceVerdictIntegrityInconclusive;
            bss_selected = 1;
        } else if (event == kAirportItlwmPostPltiTraceEventJoinBssEntered) {
            if (!bss_selected || join_bss)
                return kAirportItlwmIwnPmfIngressTraceVerdictIntegrityInconclusive;
            join_bss = 1;
        } else if (event ==
            kAirportItlwmPostPltiTraceEventNodeMfpNegotiated) {
            /* choose_rsnparams records this immediately after JOIN_BSS. */
            if (!wcl_pmf_request || !join_bss || node_mfp || previous_event !=
                    kAirportItlwmPostPltiTraceEventJoinBssEntered)
                return kAirportItlwmIwnPmfIngressTraceVerdictIntegrityInconclusive;
            node_mfp = 1;
            previous_event = event;
            continue;
        } else if (!airport_itlwm_iwn_pmf_ingress_trace_event_is_neutral(
                       event)) {
            return kAirportItlwmIwnPmfIngressTraceVerdictIntegrityInconclusive;
        }
        previous_event = event;
    }

    if (!terminal) {
        airport_itlwm_iwn_pmf_ingress_trace_set_stage(
            out_stage, kAirportItlwmIwnPmfIngressTraceMissingStageCaptureSeal);
        return kAirportItlwmIwnPmfIngressTraceVerdictIntegrityInconclusive;
    }
    if (!wcl_pmf_request) {
        airport_itlwm_iwn_pmf_ingress_trace_set_stage(
            out_stage, kAirportItlwmIwnPmfIngressTraceMissingStageWclPmfRequest);
        return kAirportItlwmIwnPmfIngressTraceVerdictWclPmfRequestNotObserved;
    }
    if (!bss_selected) {
        airport_itlwm_iwn_pmf_ingress_trace_set_stage(
            out_stage, kAirportItlwmIwnPmfIngressTraceMissingStageBssSelection);
        return kAirportItlwmIwnPmfIngressTraceVerdictBssSelectionNotObserved;
    }
    if (!join_bss) {
        airport_itlwm_iwn_pmf_ingress_trace_set_stage(
            out_stage, kAirportItlwmIwnPmfIngressTraceMissingStageJoinBss);
        return kAirportItlwmIwnPmfIngressTraceVerdictJoinBssNotObserved;
    }
    if (!node_mfp) {
        airport_itlwm_iwn_pmf_ingress_trace_set_stage(
            out_stage, kAirportItlwmIwnPmfIngressTraceMissingStageNodeMfp);
        return kAirportItlwmIwnPmfIngressTraceVerdictNodeMfpNotNegotiated;
    }
    airport_itlwm_iwn_pmf_ingress_trace_set_stage(
        out_stage, kAirportItlwmIwnPmfIngressTraceMissingStageNone);
    return kAirportItlwmIwnPmfIngressTraceVerdictNodeMfpNegotiated;
}

static inline enum AirportItlwmIwnPmfIngressTraceVerdict
airport_itlwm_iwn_pmf_ingress_trace_classify_entries(
    const AirportItlwmPostPltiTraceEntry *entries, uint32_t count,
    int integrity, uint32_t backend, uint32_t episode_count,
    uint32_t active_episode)
{
    return airport_itlwm_iwn_pmf_ingress_trace_classify_entries_with_stage(
        entries, count, integrity, backend, episode_count, active_episode, 0);
}

#endif /* AirportItlwmIwnPmfIngressTraceContracts_h */
