#ifndef AirportItlwmWclPhysicalScanTraceContracts_h
#define AirportItlwmWclPhysicalScanTraceContracts_h

/*
 * Safe categorical evaluator for the bounded one-or-two IWN-owned physical
 * WCL scans emitted by one public scan stimulus.
 *
 * It consumes the existing fixed post-PLTI trace ring rather than a second
 * driver ledger.  A positive result proves only that the request was
 * admitted, the IWN lower owner reserved its exact lease, a non-aborted
 * terminal reached the upper owner, and DONE was published.  It has no
 * identity, request, result, channel, signal, packet, credential,
 * pointer, or firmware field.
 */

#include <stdint.h>

#include <ClientKit/AirportItlwmPostPltiTrace.h>

#define AIRPORT_ITLWM_WCL_PHYSICAL_SCAN_TRACE_MAX_EPISODES 2U

enum AirportItlwmWclPhysicalScanTraceVerdict {
    kAirportItlwmWclPhysicalScanTraceVerdictIntegrityInconclusive = 0,
    kAirportItlwmWclPhysicalScanTraceVerdictBackendUnsupported,
    kAirportItlwmWclPhysicalScanTraceVerdictBranchNotObserved,
    kAirportItlwmWclPhysicalScanTraceVerdictLowerLeaseNotObserved,
    kAirportItlwmWclPhysicalScanTraceVerdictTerminalNotObserved,
    kAirportItlwmWclPhysicalScanTraceVerdictTerminalAborted,
    kAirportItlwmWclPhysicalScanTraceVerdictDoneNotPublished,
    kAirportItlwmWclPhysicalScanTraceVerdictPhysicalScanObserved,
};

enum AirportItlwmWclPhysicalScanTraceMissingStage {
    kAirportItlwmWclPhysicalScanTraceMissingStageNone = 0,
    kAirportItlwmWclPhysicalScanTraceMissingStageRequest,
    kAirportItlwmWclPhysicalScanTraceMissingStageLowerLease,
    kAirportItlwmWclPhysicalScanTraceMissingStageTerminal,
    kAirportItlwmWclPhysicalScanTraceMissingStageDone,
    kAirportItlwmWclPhysicalScanTraceMissingStageUnknown,
};

static inline void
airport_itlwm_wcl_physical_scan_trace_set_stage(
    enum AirportItlwmWclPhysicalScanTraceMissingStage *out_stage,
    enum AirportItlwmWclPhysicalScanTraceMissingStage stage)
{
    if (out_stage != 0)
        *out_stage = stage;
}

/* The established generic IWN scan facts may occur between the new exact
 * ownership boundaries.  They neither create nor complete this proof. */
static inline int
airport_itlwm_wcl_physical_scan_trace_event_is_neutral(uint32_t event)
{
    return event == kAirportItlwmPostPltiTraceEventIwnScanCoalesced ||
        event == kAirportItlwmPostPltiTraceEventIwnScanStarted ||
        event == kAirportItlwmPostPltiTraceEventScanCompleted ||
        event == kAirportItlwmPostPltiTraceEventSelectionHeld ||
        event == kAirportItlwmPostPltiTraceEventStateScanSelfRequestObserved ||
        event == kAirportItlwmPostPltiTraceEventIwnScanStateEntered ||
        event == kAirportItlwmPostPltiTraceEventIwnScanCommandRejected ||
        event == kAirportItlwmPostPltiTraceEventScanNoCandidate;
}

static inline enum AirportItlwmWclPhysicalScanTraceVerdict
airport_itlwm_wcl_physical_scan_trace_classify_entries_with_stage(
    const AirportItlwmPostPltiTraceEntry *entries, uint32_t count,
    int integrity, uint32_t backend, uint32_t episode_count,
    uint32_t active_episode,
    enum AirportItlwmWclPhysicalScanTraceMissingStage *out_stage)
{
    uint32_t generation = 0;
    uint32_t episode = 0;
    uint32_t phase = 0;
    int sealed = 0;

    airport_itlwm_wcl_physical_scan_trace_set_stage(
        out_stage, kAirportItlwmWclPhysicalScanTraceMissingStageUnknown);
    if (!integrity || entries == 0 || count >
            AIRPORT_ITLWM_POST_PLTI_TRACE_MAX_ENTRIES) {
        return kAirportItlwmWclPhysicalScanTraceVerdictIntegrityInconclusive;
    }
    if (backend != kAirportItlwmPostPltiTraceBackendIwn) {
        return kAirportItlwmWclPhysicalScanTraceVerdictBackendUnsupported;
    }
    if (episode_count == 0 || count == 0) {
        if (episode_count == 0 && count == 0 && active_episode == 0) {
            airport_itlwm_wcl_physical_scan_trace_set_stage(
                out_stage, kAirportItlwmWclPhysicalScanTraceMissingStageRequest);
            return kAirportItlwmWclPhysicalScanTraceVerdictBranchNotObserved;
        }
        return kAirportItlwmWclPhysicalScanTraceVerdictIntegrityInconclusive;
    }
    if (episode_count > AIRPORT_ITLWM_WCL_PHYSICAL_SCAN_TRACE_MAX_EPISODES ||
        active_episode != 0) {
        return kAirportItlwmWclPhysicalScanTraceVerdictIntegrityInconclusive;
    }

    generation = entries[0].captureGeneration;
    episode = entries[0].episode;
    if (generation == 0 || episode != 1) {
        return kAirportItlwmWclPhysicalScanTraceVerdictIntegrityInconclusive;
    }

    for (uint32_t index = 0; index < count; ++index) {
        const uint32_t event = entries[index].event;
        if (entries[index].sequence != entries[0].sequence + index ||
            entries[index].captureGeneration != generation ||
            event == kAirportItlwmPostPltiTraceEventUnknown ||
            event >= kAirportItlwmPostPltiTraceEventMax) {
            return kAirportItlwmWclPhysicalScanTraceVerdictIntegrityInconclusive;
        }
        if (entries[index].episode != episode) {
            /* One fixed CoreWLAN scan may ask WCL to cover two adjacent
             * physical passes.  The next pass is admissible only after the
             * prior pass reached DONE, with no skipped/reused episode id. */
            if (sealed || phase != 5 || entries[index].episode != episode + 1 ||
                episode >= episode_count) {
                return kAirportItlwmWclPhysicalScanTraceVerdictIntegrityInconclusive;
            }
            episode++;
            phase = 0;
        }
        if (event == kAirportItlwmPostPltiTraceEventEpisodeAborted) {
            return kAirportItlwmWclPhysicalScanTraceVerdictIntegrityInconclusive;
        }
        if (event == kAirportItlwmPostPltiTraceEventCaptureWindowSealed) {
            if (sealed || phase == 0)
                return kAirportItlwmWclPhysicalScanTraceVerdictIntegrityInconclusive;
            sealed = 1;
            continue;
        }
        if (sealed || phase >= 5) {
            return kAirportItlwmWclPhysicalScanTraceVerdictIntegrityInconclusive;
        }
        if (event ==
            kAirportItlwmPostPltiTraceEventWclPhysicalScanRequestAccepted) {
            if (phase != 0)
                return kAirportItlwmWclPhysicalScanTraceVerdictIntegrityInconclusive;
            phase = 1;
            continue;
        }
        if (event ==
            kAirportItlwmPostPltiTraceEventWclPhysicalScanLowerLeaseReserved) {
            if (phase != 1)
                return kAirportItlwmWclPhysicalScanTraceVerdictIntegrityInconclusive;
            phase = 2;
            continue;
        }
        if (event ==
            kAirportItlwmPostPltiTraceEventWclPhysicalScanTerminalComplete) {
            if (phase != 2)
                return kAirportItlwmWclPhysicalScanTraceVerdictIntegrityInconclusive;
            phase = 3;
            continue;
        }
        if (event ==
            kAirportItlwmPostPltiTraceEventWclPhysicalScanResultPublicationIssued) {
            /* A nonempty result publication call was issued.  It is
             * optional: a valid scan can complete with no results at all. */
            if (phase != 3)
                return kAirportItlwmWclPhysicalScanTraceVerdictIntegrityInconclusive;
            phase = 4;
            continue;
        }
        if (event ==
            kAirportItlwmPostPltiTraceEventWclPhysicalScanDonePublicationIssued) {
            if (phase != 3 && phase != 4)
                return kAirportItlwmWclPhysicalScanTraceVerdictIntegrityInconclusive;
            phase = 5;
            continue;
        }
        if (event ==
            kAirportItlwmPostPltiTraceEventWclPhysicalScanTerminalAborted) {
            if (phase != 2)
                return kAirportItlwmWclPhysicalScanTraceVerdictIntegrityInconclusive;
            phase = 6;
            continue;
        }
        if (phase == 0 ||
            !airport_itlwm_wcl_physical_scan_trace_event_is_neutral(event)) {
            return kAirportItlwmWclPhysicalScanTraceVerdictIntegrityInconclusive;
        }
    }

    /* A complete first pass cannot stand in for an advertised second pass. */
    if (episode != episode_count)
        return kAirportItlwmWclPhysicalScanTraceVerdictIntegrityInconclusive;

    switch (phase) {
    case 0:
        airport_itlwm_wcl_physical_scan_trace_set_stage(
            out_stage, kAirportItlwmWclPhysicalScanTraceMissingStageRequest);
        return kAirportItlwmWclPhysicalScanTraceVerdictBranchNotObserved;
    case 1:
        airport_itlwm_wcl_physical_scan_trace_set_stage(
            out_stage, kAirportItlwmWclPhysicalScanTraceMissingStageLowerLease);
        return kAirportItlwmWclPhysicalScanTraceVerdictLowerLeaseNotObserved;
    case 2:
        airport_itlwm_wcl_physical_scan_trace_set_stage(
            out_stage, kAirportItlwmWclPhysicalScanTraceMissingStageTerminal);
        return kAirportItlwmWclPhysicalScanTraceVerdictTerminalNotObserved;
    case 3:
        airport_itlwm_wcl_physical_scan_trace_set_stage(
            out_stage, kAirportItlwmWclPhysicalScanTraceMissingStageDone);
        return kAirportItlwmWclPhysicalScanTraceVerdictDoneNotPublished;
    case 4:
        airport_itlwm_wcl_physical_scan_trace_set_stage(
            out_stage, kAirportItlwmWclPhysicalScanTraceMissingStageDone);
        return kAirportItlwmWclPhysicalScanTraceVerdictDoneNotPublished;
    case 5:
        airport_itlwm_wcl_physical_scan_trace_set_stage(
            out_stage, kAirportItlwmWclPhysicalScanTraceMissingStageNone);
        return kAirportItlwmWclPhysicalScanTraceVerdictPhysicalScanObserved;
    case 6:
        airport_itlwm_wcl_physical_scan_trace_set_stage(
            out_stage, kAirportItlwmWclPhysicalScanTraceMissingStageTerminal);
        return kAirportItlwmWclPhysicalScanTraceVerdictTerminalAborted;
    default:
        return kAirportItlwmWclPhysicalScanTraceVerdictIntegrityInconclusive;
    }
}

/* This is deliberately a boolean fact, not a count.  Callers must still
 * require a positive classified verdict before treating it as evidence. */
static inline int
airport_itlwm_wcl_physical_scan_trace_result_publication_issued(
    const AirportItlwmPostPltiTraceEntry *entries, uint32_t count)
{
    if (entries == 0 || count > AIRPORT_ITLWM_POST_PLTI_TRACE_MAX_ENTRIES)
        return 0;
    for (uint32_t index = 0; index < count; ++index) {
        if (entries[index].event ==
            kAirportItlwmPostPltiTraceEventWclPhysicalScanResultPublicationIssued)
            return 1;
    }
    return 0;
}

static inline enum AirportItlwmWclPhysicalScanTraceVerdict
airport_itlwm_wcl_physical_scan_trace_classify_entries(
    const AirportItlwmPostPltiTraceEntry *entries, uint32_t count,
    int integrity, uint32_t backend, uint32_t episode_count,
    uint32_t active_episode)
{
    return airport_itlwm_wcl_physical_scan_trace_classify_entries_with_stage(
        entries, count, integrity, backend, episode_count, active_episode, 0);
}

#endif /* AirportItlwmWclPhysicalScanTraceContracts_h */
