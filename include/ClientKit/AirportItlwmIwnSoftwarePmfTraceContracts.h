#ifndef AirportItlwmIwnSoftwarePmfTraceContracts_h
#define AirportItlwmIwnSoftwarePmfTraceContracts_h

/*
 * Counter-only evaluator for IWN's lab-gated software PMF owner.
 *
 * It accepts only an already-sanitised fixed trace ring.  A positive verdict
 * requires one closed IWN association episode in which the driver prepared
 * software CCMP in the fixed PTK-to-GTK-to-IGTK order, acknowledged the IGTK
 * stage, the shared BIP publisher selected a live IGTK slot, the IWN finish
 * callback verified the resulting CCMP+BIP keyset under the selected-BSS
 * fence, and the port became valid.  No ID, descriptor, key byte, frame, or
 * peer value participates in the decision.
 */

#include <stdint.h>

#include <ClientKit/AirportItlwmPostPltiTrace.h>

enum AirportItlwmIwnSoftwarePmfTraceVerdict {
    kAirportItlwmIwnSoftwarePmfTraceVerdictIntegrityInconclusive = 0,
    kAirportItlwmIwnSoftwarePmfTraceVerdictBackendUnsupported,
    kAirportItlwmIwnSoftwarePmfTraceVerdictBranchNotObserved,
    kAirportItlwmIwnSoftwarePmfTraceVerdictPtkSoftwareCcmpNotObserved,
    kAirportItlwmIwnSoftwarePmfTraceVerdictGtkSoftwareCcmpNotObserved,
    kAirportItlwmIwnSoftwarePmfTraceVerdictIgtkStageNotObserved,
    kAirportItlwmIwnSoftwarePmfTraceVerdictIgtkPublicationNotObserved,
    kAirportItlwmIwnSoftwarePmfTraceVerdictKeysetPublicationNotObserved,
    kAirportItlwmIwnSoftwarePmfTraceVerdictPortValidNotObserved,
    kAirportItlwmIwnSoftwarePmfTraceVerdictInitialSoftwarePmfObserved,
};

enum AirportItlwmIwnSoftwarePmfTraceMissingStage {
    kAirportItlwmIwnSoftwarePmfTraceMissingStageNone = 0,
    kAirportItlwmIwnSoftwarePmfTraceMissingStageCaptureSeal,
    kAirportItlwmIwnSoftwarePmfTraceMissingStagePtkSoftwareCcmp,
    kAirportItlwmIwnSoftwarePmfTraceMissingStageGtkSoftwareCcmp,
    kAirportItlwmIwnSoftwarePmfTraceMissingStageIgtkStage,
    kAirportItlwmIwnSoftwarePmfTraceMissingStageIgtkPublication,
    kAirportItlwmIwnSoftwarePmfTraceMissingStageKeysetPublication,
    kAirportItlwmIwnSoftwarePmfTraceMissingStagePortValid,
    kAirportItlwmIwnSoftwarePmfTraceMissingStageUnknown,
};

static inline void
airport_itlwm_iwn_software_pmf_trace_set_stage(
    enum AirportItlwmIwnSoftwarePmfTraceMissingStage *out_stage,
    enum AirportItlwmIwnSoftwarePmfTraceMissingStage stage)
{
    if (out_stage != 0)
        *out_stage = stage;
}

static inline uint32_t
airport_itlwm_iwn_software_pmf_trace_published_slot(uint32_t event)
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
airport_itlwm_iwn_software_pmf_trace_selected_slot(uint32_t event)
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

/* The established association vocabulary is deliberately neutral here.  It
 * is evaluated by the ordered IWN matrix separately; foreign IWX PMF facts
 * remain non-neutral and therefore fail closed. */
static inline int
airport_itlwm_iwn_software_pmf_trace_event_is_neutral(uint32_t event)
{
    return event >= kAirportItlwmPostPltiTraceEventWclPmkReadyScanResume &&
        event <= kAirportItlwmPostPltiTraceEventCaptureWindowSealed;
}

static inline enum AirportItlwmIwnSoftwarePmfTraceMissingStage
airport_itlwm_iwn_software_pmf_trace_first_missing(
    uint32_t ptk_prepared, uint32_t gtk_prepared, uint32_t igtk_acknowledged,
    uint32_t active_slot, uint32_t keyset_published, uint32_t port_valid)
{
    if (!ptk_prepared)
        return kAirportItlwmIwnSoftwarePmfTraceMissingStagePtkSoftwareCcmp;
    if (!gtk_prepared)
        return kAirportItlwmIwnSoftwarePmfTraceMissingStageGtkSoftwareCcmp;
    if (!igtk_acknowledged)
        return kAirportItlwmIwnSoftwarePmfTraceMissingStageIgtkStage;
    if (active_slot == 0)
        return kAirportItlwmIwnSoftwarePmfTraceMissingStageIgtkPublication;
    if (!keyset_published)
        return kAirportItlwmIwnSoftwarePmfTraceMissingStageKeysetPublication;
    if (!port_valid)
        return kAirportItlwmIwnSoftwarePmfTraceMissingStagePortValid;
    return kAirportItlwmIwnSoftwarePmfTraceMissingStageNone;
}

static inline enum AirportItlwmIwnSoftwarePmfTraceVerdict
airport_itlwm_iwn_software_pmf_trace_classify_entries_with_stage(
    const AirportItlwmPostPltiTraceEntry *entries, uint32_t count,
    int integrity, uint32_t backend, uint32_t episode_count,
    uint32_t active_episode,
    enum AirportItlwmIwnSoftwarePmfTraceMissingStage *out_stage)
{
    uint32_t episode, generation;
    uint32_t ptk_prepared = 0, gtk_prepared = 0, igtk_acknowledged = 0;
    uint32_t published_slot = 0, active_slot = 0;
    uint32_t keyset_published = 0, port_valid = 0, terminal = 0;
    uint32_t saw_iwn_software_pmf_event = 0;

    airport_itlwm_iwn_software_pmf_trace_set_stage(
        out_stage, kAirportItlwmIwnSoftwarePmfTraceMissingStageUnknown);
    if (!integrity)
        return kAirportItlwmIwnSoftwarePmfTraceVerdictIntegrityInconclusive;
    if (backend != kAirportItlwmPostPltiTraceBackendIwn)
        return kAirportItlwmIwnSoftwarePmfTraceVerdictBackendUnsupported;
    if (episode_count == 0) {
        if (count == 0 && active_episode == 0) {
            airport_itlwm_iwn_software_pmf_trace_set_stage(
                out_stage, kAirportItlwmIwnSoftwarePmfTraceMissingStageNone);
            return kAirportItlwmIwnSoftwarePmfTraceVerdictBranchNotObserved;
        }
        return kAirportItlwmIwnSoftwarePmfTraceVerdictIntegrityInconclusive;
    }
    if (episode_count != 1 || active_episode != 0 || entries == 0 ||
        count == 0)
        return kAirportItlwmIwnSoftwarePmfTraceVerdictIntegrityInconclusive;

    episode = entries[0].episode;
    generation = entries[0].captureGeneration;
    if (episode == 0 || generation == 0 || entries[0].sequence == 0 ||
        entries[0].event !=
            kAirportItlwmPostPltiTraceEventWclPmkReadyScanResume)
        return kAirportItlwmIwnSoftwarePmfTraceVerdictIntegrityInconclusive;

    for (uint32_t i = 1; i < count; i++) {
        const uint32_t event = entries[i].event;
        const uint32_t publication_slot =
            airport_itlwm_iwn_software_pmf_trace_published_slot(event);
        const uint32_t selected_slot =
            airport_itlwm_iwn_software_pmf_trace_selected_slot(event);

        if (entries[i].sequence != entries[0].sequence + i ||
            entries[i].captureGeneration != generation ||
            entries[i].episode != episode || event ==
                kAirportItlwmPostPltiTraceEventUnknown || event >=
                kAirportItlwmPostPltiTraceEventMax || terminal)
            return kAirportItlwmIwnSoftwarePmfTraceVerdictIntegrityInconclusive;
        if (event == kAirportItlwmPostPltiTraceEventEpisodeAborted)
            return kAirportItlwmIwnSoftwarePmfTraceVerdictIntegrityInconclusive;
        if (event == kAirportItlwmPostPltiTraceEventCaptureWindowSealed) {
            terminal = 1;
            continue;
        }
        if (event == kAirportItlwmPostPltiTraceEventPortValidTransition) {
            /* A valid generic association may close without entering the
             * optional IWN software-PMF branch.  That is a deterministic
             * first-missing-stage result, not a torn PMF trace.  Once that
             * branch has begun, however, an incomplete keyset remains an
             * integrity failure rather than a benign negative observation. */
            if (port_valid || (saw_iwn_software_pmf_event &&
                               !keyset_published))
                return kAirportItlwmIwnSoftwarePmfTraceVerdictIntegrityInconclusive;
            port_valid = 1;
            terminal = 1;
            continue;
        }
        if (airport_itlwm_iwn_software_pmf_trace_event_is_neutral(event))
            continue;

        if (event ==
            kAirportItlwmPostPltiTraceEventIwnMfpPaePtkSoftwarePrepared) {
            if (ptk_prepared || gtk_prepared || igtk_acknowledged ||
                published_slot != 0 || active_slot != 0 || keyset_published)
                return kAirportItlwmIwnSoftwarePmfTraceVerdictIntegrityInconclusive;
            saw_iwn_software_pmf_event = 1;
            ptk_prepared = 1;
            continue;
        }
        if (event ==
            kAirportItlwmPostPltiTraceEventIwnMfpPaeGtkSoftwarePrepared) {
            if (!ptk_prepared || gtk_prepared || igtk_acknowledged ||
                published_slot != 0 || active_slot != 0 || keyset_published)
                return kAirportItlwmIwnSoftwarePmfTraceVerdictIntegrityInconclusive;
            saw_iwn_software_pmf_event = 1;
            gtk_prepared = 1;
            continue;
        }
        if (event ==
            kAirportItlwmPostPltiTraceEventIwnMfpPaeIgtkStageAcknowledged) {
            if (!ptk_prepared || !gtk_prepared || igtk_acknowledged ||
                published_slot != 0 || active_slot != 0 || keyset_published)
                return kAirportItlwmIwnSoftwarePmfTraceVerdictIntegrityInconclusive;
            saw_iwn_software_pmf_event = 1;
            igtk_acknowledged = 1;
            continue;
        }
        if (publication_slot != 0) {
            if (!ptk_prepared || !gtk_prepared || !igtk_acknowledged ||
                published_slot != 0 || active_slot != 0 || keyset_published)
                return kAirportItlwmIwnSoftwarePmfTraceVerdictIntegrityInconclusive;
            saw_iwn_software_pmf_event = 1;
            published_slot = publication_slot;
            continue;
        }
        if (selected_slot != 0) {
            if (published_slot == 0 || selected_slot != published_slot ||
                active_slot != 0 || keyset_published)
                return kAirportItlwmIwnSoftwarePmfTraceVerdictIntegrityInconclusive;
            saw_iwn_software_pmf_event = 1;
            active_slot = selected_slot;
            published_slot = 0;
            continue;
        }
        if (event ==
            kAirportItlwmPostPltiTraceEventIwnMfpPaeSoftwareCcmpBipPublished) {
            if (!ptk_prepared || !gtk_prepared || !igtk_acknowledged ||
                active_slot == 0 || keyset_published)
                return kAirportItlwmIwnSoftwarePmfTraceVerdictIntegrityInconclusive;
            saw_iwn_software_pmf_event = 1;
            keyset_published = 1;
            continue;
        }
        return kAirportItlwmIwnSoftwarePmfTraceVerdictIntegrityInconclusive;
    }

    if (!terminal) {
        airport_itlwm_iwn_software_pmf_trace_set_stage(
            out_stage, kAirportItlwmIwnSoftwarePmfTraceMissingStageCaptureSeal);
        return kAirportItlwmIwnSoftwarePmfTraceVerdictIntegrityInconclusive;
    }

    const enum AirportItlwmIwnSoftwarePmfTraceMissingStage stage =
        airport_itlwm_iwn_software_pmf_trace_first_missing(ptk_prepared,
            gtk_prepared, igtk_acknowledged, active_slot, keyset_published,
            port_valid);
    airport_itlwm_iwn_software_pmf_trace_set_stage(out_stage, stage);
    switch (stage) {
    case kAirportItlwmIwnSoftwarePmfTraceMissingStageNone:
        return kAirportItlwmIwnSoftwarePmfTraceVerdictInitialSoftwarePmfObserved;
    case kAirportItlwmIwnSoftwarePmfTraceMissingStagePtkSoftwareCcmp:
        return kAirportItlwmIwnSoftwarePmfTraceVerdictPtkSoftwareCcmpNotObserved;
    case kAirportItlwmIwnSoftwarePmfTraceMissingStageGtkSoftwareCcmp:
        return kAirportItlwmIwnSoftwarePmfTraceVerdictGtkSoftwareCcmpNotObserved;
    case kAirportItlwmIwnSoftwarePmfTraceMissingStageIgtkStage:
        return kAirportItlwmIwnSoftwarePmfTraceVerdictIgtkStageNotObserved;
    case kAirportItlwmIwnSoftwarePmfTraceMissingStageIgtkPublication:
        return kAirportItlwmIwnSoftwarePmfTraceVerdictIgtkPublicationNotObserved;
    case kAirportItlwmIwnSoftwarePmfTraceMissingStageKeysetPublication:
        return kAirportItlwmIwnSoftwarePmfTraceVerdictKeysetPublicationNotObserved;
    case kAirportItlwmIwnSoftwarePmfTraceMissingStagePortValid:
        return kAirportItlwmIwnSoftwarePmfTraceVerdictPortValidNotObserved;
    default:
        return kAirportItlwmIwnSoftwarePmfTraceVerdictIntegrityInconclusive;
    }
}

static inline enum AirportItlwmIwnSoftwarePmfTraceVerdict
airport_itlwm_iwn_software_pmf_trace_classify_entries(
    const AirportItlwmPostPltiTraceEntry *entries, uint32_t count,
    int integrity, uint32_t backend, uint32_t episode_count,
    uint32_t active_episode)
{
    return airport_itlwm_iwn_software_pmf_trace_classify_entries_with_stage(
        entries, count, integrity, backend, episode_count, active_episode, 0);
}

#endif /* AirportItlwmIwnSoftwarePmfTraceContracts_h */
