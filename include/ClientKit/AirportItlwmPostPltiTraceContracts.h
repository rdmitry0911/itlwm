#ifndef AirportItlwmPostPltiTraceContracts_h
#define AirportItlwmPostPltiTraceContracts_h

#include <stdint.h>

#include <ClientKit/AirportItlwmPostPltiTrace.h>

/*
 * Shared, allocation-free verdict evaluator for the safe trace client and
 * host-side unit tests.  It accepts only already-sanitised categorical ring
 * entries.  A successful verdict requires one closed IWN episode in exact
 * causal order; every ambiguity is deliberately INTEGRITY_INCONCLUSIVE.
 */
enum AirportItlwmPostPltiTraceVerdict {
    kAirportItlwmPostPltiTraceVerdictIntegrityInconclusive = 0,
    kAirportItlwmPostPltiTraceVerdictBackendUnsupported,
    kAirportItlwmPostPltiTraceVerdictBranchNotObserved,
    kAirportItlwmPostPltiTraceVerdictResumeNoScan,
    kAirportItlwmPostPltiTraceVerdictResumeNoSelection,
    kAirportItlwmPostPltiTraceVerdictAuthNotDrained,
    kAirportItlwmPostPltiTraceVerdictTxNoCompletion,
    kAirportItlwmPostPltiTraceVerdictNoEapol,
    kAirportItlwmPostPltiTraceVerdictKernelChainObserved,
};

static inline uint64_t
airport_itlwm_post_plti_trace_event_bit(uint32_t event)
{
    return event > 0 && event < 64 ? (UINT64_C(1) << event) : 0;
}

static inline int
airport_itlwm_post_plti_trace_seen(uint64_t seen, uint32_t event)
{
    return (seen & airport_itlwm_post_plti_trace_event_bit(event)) != 0;
}

static inline enum AirportItlwmPostPltiTraceVerdict
airport_itlwm_post_plti_trace_classify_entries(
    const AirportItlwmPostPltiTraceEntry *entries, uint32_t count,
    int integrity, uint32_t backend, uint32_t episodeCount,
    uint32_t activeEpisode)
{
    uint64_t seen = 0;
    uint32_t episode;
    /*
     * `PortValidTransition` is synchronous with the kernel accepting the
     * final EAPOL response, while the IWN submit/completion callbacks can
     * arrive afterwards.  Therefore enqueue is the required local TX proof;
     * firmware-submit/TX_DONE are optional corroborating markers when they
     * precede port-valid, never a precondition that would make a real
     * successful association appear incomplete.
     */
    int eapolPhase = 0;
    int terminal = 0;
    int selectionHeld = 0;

    if (!integrity)
        return kAirportItlwmPostPltiTraceVerdictIntegrityInconclusive;
    if (backend != kAirportItlwmPostPltiTraceBackendIwn)
        return kAirportItlwmPostPltiTraceVerdictBackendUnsupported;
    if (episodeCount == 0)
        return count == 0 && activeEpisode == 0 ?
            kAirportItlwmPostPltiTraceVerdictBranchNotObserved :
            kAirportItlwmPostPltiTraceVerdictIntegrityInconclusive;
    if (episodeCount != 1 || activeEpisode != 0 || entries == 0 || count == 0)
        return kAirportItlwmPostPltiTraceVerdictIntegrityInconclusive;

    episode = entries[0].episode;
    if (episode == 0 ||
        entries[0].event !=
            kAirportItlwmPostPltiTraceEventWclPmkReadyScanResume)
        return kAirportItlwmPostPltiTraceVerdictIntegrityInconclusive;
    seen |= airport_itlwm_post_plti_trace_event_bit(entries[0].event);

    for (uint32_t i = 1; i < count; i++) {
        const uint32_t event = entries[i].event;
        const uint64_t bit = airport_itlwm_post_plti_trace_event_bit(event);
        if (entries[i].episode != episode || bit == 0 || terminal ||
            selectionHeld || event == kAirportItlwmPostPltiTraceEventEpisodeAborted)
            return kAirportItlwmPostPltiTraceVerdictIntegrityInconclusive;

        switch (event) {
        case kAirportItlwmPostPltiTraceEventIwnScanCoalesced:
        case kAirportItlwmPostPltiTraceEventIwnScanStarted:
            if (airport_itlwm_post_plti_trace_seen(
                    seen, kAirportItlwmPostPltiTraceEventScanCompleted) ||
                airport_itlwm_post_plti_trace_seen(
                    seen, kAirportItlwmPostPltiTraceEventIwnScanCoalesced) ||
                airport_itlwm_post_plti_trace_seen(
                    seen, kAirportItlwmPostPltiTraceEventIwnScanStarted))
                return kAirportItlwmPostPltiTraceVerdictIntegrityInconclusive;
            break;
        case kAirportItlwmPostPltiTraceEventScanCompleted:
            if (!airport_itlwm_post_plti_trace_seen(
                    seen, kAirportItlwmPostPltiTraceEventIwnScanCoalesced) &&
                !airport_itlwm_post_plti_trace_seen(
                    seen, kAirportItlwmPostPltiTraceEventIwnScanStarted))
                return kAirportItlwmPostPltiTraceVerdictIntegrityInconclusive;
            break;
        case kAirportItlwmPostPltiTraceEventSelectionHeld:
            if (!airport_itlwm_post_plti_trace_seen(
                    seen, kAirportItlwmPostPltiTraceEventScanCompleted) ||
                airport_itlwm_post_plti_trace_seen(
                    seen, kAirportItlwmPostPltiTraceEventBssSelected))
                return kAirportItlwmPostPltiTraceVerdictIntegrityInconclusive;
            selectionHeld = 1;
            break;
        case kAirportItlwmPostPltiTraceEventBssSelected:
            if (!airport_itlwm_post_plti_trace_seen(
                    seen, kAirportItlwmPostPltiTraceEventScanCompleted) ||
                airport_itlwm_post_plti_trace_seen(
                    seen, kAirportItlwmPostPltiTraceEventBssSelected))
                return kAirportItlwmPostPltiTraceVerdictIntegrityInconclusive;
            break;
        case kAirportItlwmPostPltiTraceEventJoinBssEntered:
            if (!airport_itlwm_post_plti_trace_seen(
                    seen, kAirportItlwmPostPltiTraceEventBssSelected) ||
                airport_itlwm_post_plti_trace_seen(
                    seen, kAirportItlwmPostPltiTraceEventJoinBssEntered))
                return kAirportItlwmPostPltiTraceVerdictIntegrityInconclusive;
            break;
        case kAirportItlwmPostPltiTraceEventAuthStateEntered:
            if (!airport_itlwm_post_plti_trace_seen(
                    seen, kAirportItlwmPostPltiTraceEventJoinBssEntered) ||
                airport_itlwm_post_plti_trace_seen(
                    seen, kAirportItlwmPostPltiTraceEventAuthStateEntered))
                return kAirportItlwmPostPltiTraceVerdictIntegrityInconclusive;
            break;
        case kAirportItlwmPostPltiTraceEventAuthEnqueued:
            if (!airport_itlwm_post_plti_trace_seen(
                    seen, kAirportItlwmPostPltiTraceEventAuthStateEntered) ||
                airport_itlwm_post_plti_trace_seen(
                    seen, kAirportItlwmPostPltiTraceEventAuthEnqueued))
                return kAirportItlwmPostPltiTraceVerdictIntegrityInconclusive;
            break;
        case kAirportItlwmPostPltiTraceEventAuthDequeued:
            if (!airport_itlwm_post_plti_trace_seen(
                    seen, kAirportItlwmPostPltiTraceEventAuthEnqueued) ||
                airport_itlwm_post_plti_trace_seen(
                    seen, kAirportItlwmPostPltiTraceEventAuthDequeued))
                return kAirportItlwmPostPltiTraceVerdictIntegrityInconclusive;
            break;
        case kAirportItlwmPostPltiTraceEventAuthFwSubmitted:
            if (!airport_itlwm_post_plti_trace_seen(
                    seen, kAirportItlwmPostPltiTraceEventAuthDequeued) ||
                airport_itlwm_post_plti_trace_seen(
                    seen, kAirportItlwmPostPltiTraceEventAuthFwSubmitted))
                return kAirportItlwmPostPltiTraceVerdictIntegrityInconclusive;
            break;
        case kAirportItlwmPostPltiTraceEventAuthTxDone:
        case kAirportItlwmPostPltiTraceEventAuthRxFromFirmware:
            if (!airport_itlwm_post_plti_trace_seen(
                    seen, kAirportItlwmPostPltiTraceEventAuthFwSubmitted) ||
                airport_itlwm_post_plti_trace_seen(seen, event))
                return kAirportItlwmPostPltiTraceVerdictIntegrityInconclusive;
            break;
        case kAirportItlwmPostPltiTraceEventAuthRxNet80211:
            if (!airport_itlwm_post_plti_trace_seen(
                    seen, kAirportItlwmPostPltiTraceEventAuthRxFromFirmware) ||
                airport_itlwm_post_plti_trace_seen(
                    seen, kAirportItlwmPostPltiTraceEventAuthRxNet80211))
                return kAirportItlwmPostPltiTraceVerdictIntegrityInconclusive;
            break;
        case kAirportItlwmPostPltiTraceEventAssocStateEntered:
            if (!airport_itlwm_post_plti_trace_seen(
                    seen, kAirportItlwmPostPltiTraceEventAuthTxDone) ||
                !airport_itlwm_post_plti_trace_seen(
                    seen, kAirportItlwmPostPltiTraceEventAuthRxNet80211) ||
                airport_itlwm_post_plti_trace_seen(
                    seen, kAirportItlwmPostPltiTraceEventAssocStateEntered))
                return kAirportItlwmPostPltiTraceVerdictIntegrityInconclusive;
            break;
        case kAirportItlwmPostPltiTraceEventAssocEnqueued:
            if (!airport_itlwm_post_plti_trace_seen(
                    seen, kAirportItlwmPostPltiTraceEventAssocStateEntered) ||
                airport_itlwm_post_plti_trace_seen(
                    seen, kAirportItlwmPostPltiTraceEventAssocEnqueued))
                return kAirportItlwmPostPltiTraceVerdictIntegrityInconclusive;
            break;
        case kAirportItlwmPostPltiTraceEventAssocDequeued:
            if (!airport_itlwm_post_plti_trace_seen(
                    seen, kAirportItlwmPostPltiTraceEventAssocEnqueued) ||
                airport_itlwm_post_plti_trace_seen(
                    seen, kAirportItlwmPostPltiTraceEventAssocDequeued))
                return kAirportItlwmPostPltiTraceVerdictIntegrityInconclusive;
            break;
        case kAirportItlwmPostPltiTraceEventAssocFwSubmitted:
            if (!airport_itlwm_post_plti_trace_seen(
                    seen, kAirportItlwmPostPltiTraceEventAssocDequeued) ||
                airport_itlwm_post_plti_trace_seen(
                    seen, kAirportItlwmPostPltiTraceEventAssocFwSubmitted))
                return kAirportItlwmPostPltiTraceVerdictIntegrityInconclusive;
            break;
        case kAirportItlwmPostPltiTraceEventAssocTxDone:
        case kAirportItlwmPostPltiTraceEventAssocRxFromFirmware:
            if (!airport_itlwm_post_plti_trace_seen(
                    seen, kAirportItlwmPostPltiTraceEventAssocFwSubmitted) ||
                airport_itlwm_post_plti_trace_seen(seen, event))
                return kAirportItlwmPostPltiTraceVerdictIntegrityInconclusive;
            break;
        case kAirportItlwmPostPltiTraceEventAssocRxNet80211:
            if (!airport_itlwm_post_plti_trace_seen(
                    seen, kAirportItlwmPostPltiTraceEventAssocRxFromFirmware) ||
                airport_itlwm_post_plti_trace_seen(
                    seen, kAirportItlwmPostPltiTraceEventAssocRxNet80211))
                return kAirportItlwmPostPltiTraceVerdictIntegrityInconclusive;
            break;
        case kAirportItlwmPostPltiTraceEventRunEntered:
            if (!airport_itlwm_post_plti_trace_seen(
                    seen, kAirportItlwmPostPltiTraceEventAssocTxDone) ||
                !airport_itlwm_post_plti_trace_seen(
                    seen, kAirportItlwmPostPltiTraceEventAssocRxNet80211) ||
                airport_itlwm_post_plti_trace_seen(
                    seen, kAirportItlwmPostPltiTraceEventRunEntered))
                return kAirportItlwmPostPltiTraceVerdictIntegrityInconclusive;
            break;
        case kAirportItlwmPostPltiTraceEventEapolRxDecapped:
            if (!airport_itlwm_post_plti_trace_seen(
                    seen, kAirportItlwmPostPltiTraceEventRunEntered) ||
                eapolPhase == 1 || eapolPhase == 2)
                return kAirportItlwmPostPltiTraceVerdictIntegrityInconclusive;
            eapolPhase = 1;
            break;
        case kAirportItlwmPostPltiTraceEventEapolRxKernelPae:
            if (eapolPhase != 1)
                return kAirportItlwmPostPltiTraceVerdictIntegrityInconclusive;
            eapolPhase = 2;
            break;
        case kAirportItlwmPostPltiTraceEventEapolTxEnqueued:
            if (eapolPhase != 2)
                return kAirportItlwmPostPltiTraceVerdictIntegrityInconclusive;
            eapolPhase = 3;
            break;
        case kAirportItlwmPostPltiTraceEventEapolFwSubmitted:
            if (eapolPhase != 3)
                return kAirportItlwmPostPltiTraceVerdictIntegrityInconclusive;
            eapolPhase = 4;
            break;
        case kAirportItlwmPostPltiTraceEventEapolTxDone:
            if (eapolPhase != 4)
                return kAirportItlwmPostPltiTraceVerdictIntegrityInconclusive;
            eapolPhase = 5;
            break;
        case kAirportItlwmPostPltiTraceEventPortValidTransition:
            if (eapolPhase < 3)
                return kAirportItlwmPostPltiTraceVerdictIntegrityInconclusive;
            terminal = 1;
            break;
        default:
            return kAirportItlwmPostPltiTraceVerdictIntegrityInconclusive;
        }
        seen |= bit;
    }

    if (selectionHeld)
        return kAirportItlwmPostPltiTraceVerdictResumeNoSelection;
    if (!airport_itlwm_post_plti_trace_seen(
            seen, kAirportItlwmPostPltiTraceEventIwnScanCoalesced) &&
        !airport_itlwm_post_plti_trace_seen(
            seen, kAirportItlwmPostPltiTraceEventIwnScanStarted))
        return kAirportItlwmPostPltiTraceVerdictResumeNoScan;
    if (!airport_itlwm_post_plti_trace_seen(
            seen, kAirportItlwmPostPltiTraceEventScanCompleted) ||
        !airport_itlwm_post_plti_trace_seen(
            seen, kAirportItlwmPostPltiTraceEventBssSelected) ||
        !airport_itlwm_post_plti_trace_seen(
            seen, kAirportItlwmPostPltiTraceEventJoinBssEntered))
        return kAirportItlwmPostPltiTraceVerdictResumeNoSelection;
    if (!airport_itlwm_post_plti_trace_seen(
            seen, kAirportItlwmPostPltiTraceEventAuthEnqueued) ||
        !airport_itlwm_post_plti_trace_seen(
            seen, kAirportItlwmPostPltiTraceEventAuthDequeued) ||
        !airport_itlwm_post_plti_trace_seen(
            seen, kAirportItlwmPostPltiTraceEventAuthFwSubmitted))
        return kAirportItlwmPostPltiTraceVerdictAuthNotDrained;
    if (!airport_itlwm_post_plti_trace_seen(
            seen, kAirportItlwmPostPltiTraceEventAuthTxDone) ||
        !airport_itlwm_post_plti_trace_seen(
            seen, kAirportItlwmPostPltiTraceEventAssocTxDone) ||
        !airport_itlwm_post_plti_trace_seen(
            seen, kAirportItlwmPostPltiTraceEventRunEntered))
        return kAirportItlwmPostPltiTraceVerdictTxNoCompletion;
    if (!terminal)
        return kAirportItlwmPostPltiTraceVerdictNoEapol;
    return kAirportItlwmPostPltiTraceVerdictKernelChainObserved;
}

#endif /* AirportItlwmPostPltiTraceContracts_h */
