#ifndef TahoeIwxPmfBipTraceContracts_hpp
#define TahoeIwxPmfBipTraceContracts_hpp

#include <cstdint>

#include <ClientKit/AirportItlwmIwxPmfBipTraceContracts.h>

namespace TahoeIwxPmfBipTraceContracts {

enum class Verdict : uint32_t {
    IntegrityInconclusive =
        kAirportItlwmIwxPmfBipTraceVerdictIntegrityInconclusive,
    BackendUnsupported =
        kAirportItlwmIwxPmfBipTraceVerdictBackendUnsupported,
    BranchNotObserved =
        kAirportItlwmIwxPmfBipTraceVerdictBranchNotObserved,
    PmfRxNotObserved =
        kAirportItlwmIwxPmfBipTraceVerdictPmfRxNotObserved,
    Q0DoorbellNotObserved =
        kAirportItlwmIwxPmfBipTraceVerdictQ0DoorbellNotObserved,
    Q0CompletionNotObserved =
        kAirportItlwmIwxPmfBipTraceVerdictQ0CompletionNotObserved,
    IgtkPublicationNotObserved =
        kAirportItlwmIwxPmfBipTraceVerdictIgtkPublicationNotObserved,
    ActiveSlotNotObserved =
        kAirportItlwmIwxPmfBipTraceVerdictActiveSlotNotObserved,
    PortValidNotObserved =
        kAirportItlwmIwxPmfBipTraceVerdictPortValidNotObserved,
    InitialPmfBipObserved =
        kAirportItlwmIwxPmfBipTraceVerdictInitialPmfBipObserved,
    CrossSlotRekeyObserved =
        kAirportItlwmIwxPmfBipTraceVerdictCrossSlotRekeyObserved,
};

enum class MissingStage : uint32_t {
    None = kAirportItlwmIwxPmfBipTraceMissingStageNone,
    CaptureSeal = kAirportItlwmIwxPmfBipTraceMissingStageCaptureSeal,
    PmfRx = kAirportItlwmIwxPmfBipTraceMissingStagePmfRx,
    Q0Doorbell = kAirportItlwmIwxPmfBipTraceMissingStageQ0Doorbell,
    Q0Completion = kAirportItlwmIwxPmfBipTraceMissingStageQ0Completion,
    IgtkPublication =
        kAirportItlwmIwxPmfBipTraceMissingStageIgtkPublication,
    ActiveSlot = kAirportItlwmIwxPmfBipTraceMissingStageActiveSlot,
    PortValid = kAirportItlwmIwxPmfBipTraceMissingStagePortValid,
    CrossSlotRekey =
        kAirportItlwmIwxPmfBipTraceMissingStageCrossSlotRekey,
    Unknown = kAirportItlwmIwxPmfBipTraceMissingStageUnknown,
};

inline Verdict
classifyEntries(const AirportItlwmPostPltiTraceEntry *entries, uint32_t count,
                bool integrity, uint32_t backend, uint32_t episodeCount,
                uint32_t activeEpisode, MissingStage *outMissingStage = nullptr)
{
    enum AirportItlwmIwxPmfBipTraceMissingStage stage =
        kAirportItlwmIwxPmfBipTraceMissingStageUnknown;
    const Verdict verdict = static_cast<Verdict>(
        airport_itlwm_iwx_pmf_bip_trace_classify_entries_with_stage(
            entries, count, integrity ? 1 : 0, backend, episodeCount,
            activeEpisode, &stage));
    if (outMissingStage != nullptr)
        *outMissingStage = static_cast<MissingStage>(stage);
    return verdict;
}

} // namespace TahoeIwxPmfBipTraceContracts

#endif /* TahoeIwxPmfBipTraceContracts_hpp */
