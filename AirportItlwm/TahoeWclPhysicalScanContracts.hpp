//
//  TahoeWclPhysicalScanContracts.hpp
//  AirportItlwm
//
//  Small, identity-free reducer for the WCL physical-scan ticket.  It keeps
//  admission and terminal ownership testable without bringing IOKit or a
//  net80211 controller into a unit test.
//

#ifndef TahoeWclPhysicalScanContracts_hpp
#define TahoeWclPhysicalScanContracts_hpp

#include <stdint.h>

namespace TahoeWclPhysicalScanContracts {

enum class Phase : uint8_t {
    Idle,
    Starting,
    Active,
    Aborting,
    Completing,
    Draining,
};

enum class StartDisposition : uint8_t {
    Lost,
    Active,
    TerminalPending,
};

enum class CompletionDisposition : uint8_t {
    None,
    Publish,
    Suppress,
};

struct State {
    uint64_t nextGeneration;
    uint64_t activeGeneration;
    Phase phase;
};

inline void reset(State *state)
{
    if (state == nullptr)
        return;
    state->activeGeneration = 0;
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
    state->phase = Phase::Starting;
    *generation = state->activeGeneration;
    return true;
}

inline StartDisposition activate(State *state, uint64_t generation)
{
    if (state == nullptr || generation == 0 ||
        state->activeGeneration != generation)
        return StartDisposition::Lost;

    if (state->phase == Phase::Starting) {
        state->phase = Phase::Active;
        return StartDisposition::Active;
    }
    if (state->phase == Phase::Active)
        return StartDisposition::Active;
    if (state->phase == Phase::Completing)
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

inline bool markAborting(State *state, uint64_t *generation)
{
    if (state == nullptr || generation == nullptr)
        return false;

    if (state->phase != Phase::Starting && state->phase != Phase::Active)
        return false;

    state->phase = Phase::Aborting;
    *generation = state->activeGeneration;
    return true;
}

/*
 * A backend may reject an abort after the ticket was marked but before it
 * accepted the command.  The original radio scan is still live in that case,
 * so restore normal terminal semantics instead of leaving WCL permanently in
 * an abort state.  A raced terminal already owns Completing and is never
 * rewritten.
 */
inline bool resumeAfterAbortFailure(State *state, uint64_t generation)
{
    if (state == nullptr || generation == 0 ||
        state->activeGeneration != generation ||
        state->phase != Phase::Aborting)
        return false;

    state->phase = Phase::Active;
    return true;
}

inline CompletionDisposition claimCompletion(
    State *state, uint64_t *generation, bool *aborted)
{
    if (state == nullptr || generation == nullptr || aborted == nullptr)
        return CompletionDisposition::None;

    if (state->phase == Phase::Draining) {
        reset(state);
        return CompletionDisposition::Suppress;
    }

    if (state->phase != Phase::Starting && state->phase != Phase::Active &&
        state->phase != Phase::Aborting)
        return CompletionDisposition::None;

    *generation = state->activeGeneration;
    *aborted = state->phase == Phase::Aborting;
    state->phase = Phase::Completing;
    return CompletionDisposition::Publish;
}

inline bool ownsCompletion(const State *state, uint64_t generation)
{
    return state != nullptr && generation != 0 &&
           state->activeGeneration == generation &&
           state->phase == Phase::Completing;
}

inline void finishCompletion(State *state, uint64_t generation)
{
    if (ownsCompletion(state, generation))
        reset(state);
}

/*
 * A power-off/teardown invalidates the WCL client before hardware can emit a
 * late terminal edge.  Keep the ticket in Draining so another request cannot
 * borrow that edge.  A terminal consumes it silently; a confirmed radio
 * reset/re-enable may reopen it when no old scan can still run.
 */
inline void beginDraining(State *state)
{
    if (state != nullptr && state->phase != Phase::Idle)
        state->phase = Phase::Draining;
}

inline void reopenAfterRadioReset(State *state)
{
    if (state != nullptr && state->phase == Phase::Draining)
        reset(state);
}

} // namespace TahoeWclPhysicalScanContracts

#endif /* TahoeWclPhysicalScanContracts_hpp */
