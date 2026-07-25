//
//  TahoeStandardScanContracts.hpp
//  AirportItlwm
//
//  Identity-free reducer for one CoreWLAN normal-scan request that owns an
//  exact lower IWN physical-scan lease.  It is deliberately separate from
//  WCL: the normal SCAN_REQ terminal remains APPLE80211_M_SCAN_DONE.
//

#ifndef TahoeStandardScanContracts_hpp
#define TahoeStandardScanContracts_hpp

#include <stdint.h>

namespace TahoeStandardScanContracts {

enum class Phase : uint8_t {
    Idle,
    Starting,
    Active,
    Completing,
    Draining,
};

enum class StartDisposition : uint8_t {
    Lost,
    Active,
    TerminalPending,
};

enum class TerminalDisposition : uint8_t {
    None,
    PendingStart,
    Complete,
};

struct State {
    uint64_t nextGeneration;
    uint64_t activeGeneration;
    uint32_t activeBackendGeneration;
    uint32_t terminalBackendGeneration;
    uint32_t terminalStatus;
    Phase phase;
};

inline void reset(State *state)
{
    if (state == nullptr)
        return;
    state->activeGeneration = 0;
    state->activeBackendGeneration = 0;
    state->terminalBackendGeneration = 0;
    state->terminalStatus = 0;
    state->phase = Phase::Idle;
}

inline bool reserve(State *state, uint64_t *generation)
{
    if (state == nullptr || generation == nullptr || state->phase != Phase::Idle)
        return false;

    ++state->nextGeneration;
    if (state->nextGeneration == 0)
        ++state->nextGeneration;
    state->activeGeneration = state->nextGeneration;
    state->activeBackendGeneration = 0;
    state->terminalBackendGeneration = 0;
    state->terminalStatus = 0;
    state->phase = Phase::Starting;
    *generation = state->activeGeneration;
    return true;
}

inline StartDisposition activate(State *state, uint64_t generation,
                                 uint32_t backendGeneration)
{
    if (state == nullptr || generation == 0 || backendGeneration == 0 ||
        state->activeGeneration != generation)
        return StartDisposition::Lost;

    if (state->phase == Phase::Starting) {
        state->activeBackendGeneration = backendGeneration;
        state->phase = Phase::Active;
        return StartDisposition::Active;
    }
    if (state->phase == Phase::Active &&
        state->activeBackendGeneration == backendGeneration)
        return StartDisposition::Active;
    if (state->phase == Phase::Completing &&
        state->activeBackendGeneration == backendGeneration &&
        state->terminalBackendGeneration == backendGeneration)
        return StartDisposition::TerminalPending;
    return StartDisposition::Lost;
}

inline StartDisposition failStart(State *state, uint64_t generation)
{
    if (state == nullptr || generation == 0 ||
        state->activeGeneration != generation)
        return StartDisposition::Lost;

    if (state->phase == Phase::Starting) {
        reset(state);
        return StartDisposition::Lost;
    }
    if (state->phase == Phase::Completing)
        return StartDisposition::TerminalPending;
    if (state->phase == Phase::Active)
        return StartDisposition::Active;
    return StartDisposition::Lost;
}

/* The tagged lower terminal is emitted only after the matching generic
 * IEEE80211_EVT_SCAN_DONE has returned.  An active request can therefore be
 * retired immediately; a terminal racing begin() remains Completing until
 * that setter reconciles its returned backend generation. */
inline TerminalDisposition claimTerminal(State *state, uint64_t generation,
                                         uint32_t backendGeneration,
                                         uint32_t terminalStatus)
{
    if (state == nullptr || generation == 0 || backendGeneration == 0 ||
        (terminalStatus != 0 && terminalStatus != 1) ||
        state->activeGeneration != generation)
        return TerminalDisposition::None;
    if (state->phase != Phase::Starting && state->phase != Phase::Active)
        return TerminalDisposition::None;
    if (state->activeBackendGeneration != 0 &&
        state->activeBackendGeneration != backendGeneration)
        return TerminalDisposition::None;

    state->activeBackendGeneration = backendGeneration;
    state->terminalBackendGeneration = backendGeneration;
    state->terminalStatus = terminalStatus;
    if (state->phase == Phase::Starting) {
        state->phase = Phase::Completing;
        return TerminalDisposition::PendingStart;
    }
    reset(state);
    return TerminalDisposition::Complete;
}

inline void finishPendingTerminal(State *state, uint64_t generation,
                                  uint32_t backendGeneration)
{
    if (state != nullptr && generation != 0 && backendGeneration != 0 &&
        state->activeGeneration == generation &&
        state->activeBackendGeneration == backendGeneration &&
        state->terminalBackendGeneration == backendGeneration &&
        state->phase == Phase::Completing)
        reset(state);
}

inline bool invalidate(State *state, uint64_t generation,
                       uint32_t backendGeneration)
{
    if (state == nullptr || generation == 0 || backendGeneration == 0 ||
        state->activeGeneration != generation ||
        (state->activeBackendGeneration != 0 &&
         state->activeBackendGeneration != backendGeneration))
        return false;
    state->phase = Phase::Draining;
    return true;
}

inline void beginDraining(State *state)
{
    if (state != nullptr)
        state->phase = Phase::Draining;
}

inline void reopenAfterRadioReset(State *state)
{
    if (state != nullptr && state->phase == Phase::Draining)
        reset(state);
}

inline bool idle(const State *state)
{
    return state != nullptr && state->phase == Phase::Idle;
}

} // namespace TahoeStandardScanContracts

#endif /* TahoeStandardScanContracts_hpp */
