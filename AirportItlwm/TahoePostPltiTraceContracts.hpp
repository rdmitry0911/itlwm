#ifndef TahoePostPltiTraceContracts_hpp
#define TahoePostPltiTraceContracts_hpp

#include <cstdint>

#include <ClientKit/AirportItlwmPostPltiTraceMatrixContracts.h>

/*
 * C++ façade for the single C/C++ safe-trace evaluator.  The evaluator takes
 * ordered categorical entries, not a bitset: a bitset cannot prove causality
 * or distinguish a complete exchange from events assembled across retries.
 */
namespace TahoePostPltiTraceContracts {

enum class Verdict : uint32_t {
    IntegrityInconclusive =
        kAirportItlwmPostPltiTraceMatrixVerdictIntegrityInconclusive,
    BackendUnsupported =
        kAirportItlwmPostPltiTraceMatrixVerdictBackendUnsupported,
    BranchNotObserved =
        kAirportItlwmPostPltiTraceMatrixVerdictBranchNotObserved,
    ResumeNoStateRequest =
        kAirportItlwmPostPltiTraceMatrixVerdictResumeNoStateRequest,
    ResumeNoIwnDispatch =
        kAirportItlwmPostPltiTraceMatrixVerdictResumeNoIwnDispatch,
    ScanCommandRejected =
        kAirportItlwmPostPltiTraceMatrixVerdictScanCommandRejected,
    ScanIncomplete =
        kAirportItlwmPostPltiTraceMatrixVerdictScanIncomplete,
    ScanNoCandidate =
        kAirportItlwmPostPltiTraceMatrixVerdictScanNoCandidate,
    ResumeNoSelection =
        kAirportItlwmPostPltiTraceMatrixVerdictResumeNoSelection,
    AuthNotDrained =
        kAirportItlwmPostPltiTraceMatrixVerdictAuthNotDrained,
    TxNoCompletion =
        kAirportItlwmPostPltiTraceMatrixVerdictTxNoCompletion,
    NoEapol = kAirportItlwmPostPltiTraceMatrixVerdictNoEapol,
    KernelChainObserved =
        kAirportItlwmPostPltiTraceMatrixVerdictKernelChainObserved,
};

enum class MissingStage : uint32_t {
    None = kAirportItlwmPostPltiTraceMissingStageNone,
    StateScanSelfRequest =
        kAirportItlwmPostPltiTraceMissingStageStateScanSelfRequest,
    IwnScanState = kAirportItlwmPostPltiTraceMissingStageIwnScanState,
    IwnScanCommand = kAirportItlwmPostPltiTraceMissingStageIwnScanCommand,
    ScanCompletion =
        kAirportItlwmPostPltiTraceMissingStageScanCompletion,
    BssSelection =
        kAirportItlwmPostPltiTraceMissingStageBssSelection,
    JoinBss = kAirportItlwmPostPltiTraceMissingStageJoinBss,
    AuthState = kAirportItlwmPostPltiTraceMissingStageAuthState,
    AuthEnqueue = kAirportItlwmPostPltiTraceMissingStageAuthEnqueue,
    AuthDequeue = kAirportItlwmPostPltiTraceMissingStageAuthDequeue,
    AuthFirmwareSubmit =
        kAirportItlwmPostPltiTraceMissingStageAuthFirmwareSubmit,
    AuthExchange = kAirportItlwmPostPltiTraceMissingStageAuthExchange,
    AssocState = kAirportItlwmPostPltiTraceMissingStageAssocState,
    AssocEnqueue = kAirportItlwmPostPltiTraceMissingStageAssocEnqueue,
    AssocDequeue = kAirportItlwmPostPltiTraceMissingStageAssocDequeue,
    AssocFirmwareSubmit =
        kAirportItlwmPostPltiTraceMissingStageAssocFirmwareSubmit,
    AssocExchange = kAirportItlwmPostPltiTraceMissingStageAssocExchange,
    RunState = kAirportItlwmPostPltiTraceMissingStageRunState,
    EapolDecapped =
        kAirportItlwmPostPltiTraceMissingStageEapolDecapped,
    EapolKernelPae =
        kAirportItlwmPostPltiTraceMissingStageEapolKernelPae,
    EapolEnqueue = kAirportItlwmPostPltiTraceMissingStageEapolEnqueue,
    PortValid = kAirportItlwmPostPltiTraceMissingStagePortValid,
    Unknown = kAirportItlwmPostPltiTraceMissingStageUnknown,
};

inline Verdict
classifyEntries(const AirportItlwmPostPltiTraceEntry *entries, uint32_t count,
                bool integrity, uint32_t backend, uint32_t episodeCount,
                uint32_t activeEpisode, MissingStage *outMissingStage = nullptr)
{
    enum AirportItlwmPostPltiTraceMissingStage stage =
        kAirportItlwmPostPltiTraceMissingStageUnknown;
    const Verdict verdict = static_cast<Verdict>(
        airport_itlwm_post_plti_trace_matrix_classify_entries_with_stage(
            entries, count, integrity ? 1 : 0, backend, episodeCount,
            activeEpisode, &stage));
    if (outMissingStage != nullptr)
        *outMissingStage = static_cast<MissingStage>(stage);
    return verdict;
}

/* A mask-only caller can never establish the ordered success verdict. */
constexpr Verdict classify(uint64_t, bool integrity, uint32_t episodeCount)
{
    return !integrity || episodeCount != 0 ? Verdict::IntegrityInconclusive :
        Verdict::BranchNotObserved;
}

} // namespace TahoePostPltiTraceContracts

#endif /* TahoePostPltiTraceContracts_hpp */
