#ifndef TahoePostPltiTraceContracts_hpp
#define TahoePostPltiTraceContracts_hpp

#include <cstdint>

#include <ClientKit/AirportItlwmPostPltiTraceContracts.h>

/*
 * C++ façade for the single C/C++ safe-trace evaluator.  The evaluator takes
 * ordered categorical entries, not a bitset: a bitset cannot prove causality
 * or distinguish a complete exchange from events assembled across retries.
 */
namespace TahoePostPltiTraceContracts {

enum class Verdict : uint32_t {
    IntegrityInconclusive =
        kAirportItlwmPostPltiTraceVerdictIntegrityInconclusive,
    BackendUnsupported =
        kAirportItlwmPostPltiTraceVerdictBackendUnsupported,
    BranchNotObserved =
        kAirportItlwmPostPltiTraceVerdictBranchNotObserved,
    ResumeNoScan = kAirportItlwmPostPltiTraceVerdictResumeNoScan,
    ResumeNoSelection =
        kAirportItlwmPostPltiTraceVerdictResumeNoSelection,
    AuthNotDrained = kAirportItlwmPostPltiTraceVerdictAuthNotDrained,
    TxNoCompletion = kAirportItlwmPostPltiTraceVerdictTxNoCompletion,
    NoEapol = kAirportItlwmPostPltiTraceVerdictNoEapol,
    KernelChainObserved =
        kAirportItlwmPostPltiTraceVerdictKernelChainObserved,
};

inline Verdict
classifyEntries(const AirportItlwmPostPltiTraceEntry *entries, uint32_t count,
                bool integrity, uint32_t backend, uint32_t episodeCount,
                uint32_t activeEpisode)
{
    return static_cast<Verdict>(airport_itlwm_post_plti_trace_classify_entries(
        entries, count, integrity ? 1 : 0, backend, episodeCount,
        activeEpisode));
}

/* A mask-only caller can never establish the ordered success verdict. */
constexpr Verdict classify(uint64_t, bool integrity, uint32_t episodeCount)
{
    return !integrity || episodeCount != 0 ? Verdict::IntegrityInconclusive :
        Verdict::BranchNotObserved;
}

} // namespace TahoePostPltiTraceContracts

#endif /* TahoePostPltiTraceContracts_hpp */
