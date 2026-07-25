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
    Pending,
    Publish,
    Suppress,
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

inline bool markAborting(State *state, uint64_t *generation)
{
    if (state == nullptr || generation == nullptr)
        return false;

    /* The first shipping backend does not submit an abort while its lower
     * command is still arming.  The caller gets Busy and retries after the
     * exact backend generation has become active. */
    if (state->phase != Phase::Active)
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
    State *state, uint64_t generation, uint32_t backendGeneration,
    uint32_t terminalStatus)
{
    if (state == nullptr || generation == 0 || backendGeneration == 0 ||
        (terminalStatus != 0 && terminalStatus != 1) ||
        state->activeGeneration != generation)
        return CompletionDisposition::None;

    if (state->phase == Phase::Draining) {
        if (state->activeBackendGeneration != 0 &&
            state->activeBackendGeneration != backendGeneration)
            return CompletionDisposition::None;
        /* A late terminal proves only that the old physical lease ended; it
         * does not prove that the radio has completed its reset/init cycle.
         * Keep admission closed until the lower backend emits its explicit
         * REOPENED fence, otherwise a concurrent WCL request could attach to
         * a half-reset radio. */
        return CompletionDisposition::Suppress;
    }

    if (state->phase != Phase::Starting && state->phase != Phase::Active &&
        state->phase != Phase::Aborting)
        return CompletionDisposition::None;

    if (state->activeBackendGeneration != 0 &&
        state->activeBackendGeneration != backendGeneration)
        return CompletionDisposition::None;
    state->activeBackendGeneration = backendGeneration;
    state->terminalBackendGeneration = backendGeneration;
    state->terminalStatus = terminalStatus;
    const bool starting = state->phase == Phase::Starting;
    state->phase = Phase::Completing;
    return starting ? CompletionDisposition::Pending :
        CompletionDisposition::Publish;
}

inline bool pendingCompletion(const State *state, uint64_t generation,
                              uint32_t backendGeneration,
                              uint32_t *terminalStatus)
{
    if (state == nullptr || generation == 0 || backendGeneration == 0 ||
        terminalStatus == nullptr || state->activeGeneration != generation ||
        state->activeBackendGeneration != backendGeneration ||
        state->terminalBackendGeneration != backendGeneration ||
        state->phase != Phase::Completing)
        return false;
    *terminalStatus = state->terminalStatus;
    return true;
}

/* The begin call can observe a submission error after firmware has already
 * produced the tagged terminal.  In that narrow race its out-generation is
 * still zero, while the terminal mailbox has the exact backend identity.
 * Accept only that unknown-at-call-site form, then return the recorded
 * identity to the caller; a nonzero caller identity remains an exact fence. */
inline bool pendingCompletionForGeneration(const State *state,
                                           uint64_t generation,
                                           uint32_t *backendGeneration,
                                           uint32_t *terminalStatus)
{
    if (state == nullptr || backendGeneration == nullptr)
        return false;
    uint32_t expectedBackendGeneration = *backendGeneration;
    if (expectedBackendGeneration == 0)
        expectedBackendGeneration = state->activeBackendGeneration;
    if (!pendingCompletion(state, generation, expectedBackendGeneration,
                           terminalStatus))
        return false;
    *backendGeneration = expectedBackendGeneration;
    return true;
}

inline bool ownsCompletion(const State *state, uint64_t generation,
                           uint32_t backendGeneration)
{
    return state != nullptr && generation != 0 && backendGeneration != 0 &&
        state->activeGeneration == generation &&
        state->activeBackendGeneration == backendGeneration &&
        state->terminalBackendGeneration == backendGeneration &&
        state->phase == Phase::Completing;
}

inline void finishCompletion(State *state, uint64_t generation,
                             uint32_t backendGeneration)
{
    if (ownsCompletion(state, generation, backendGeneration))
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
    /* A radio reset must close new WCL admission even when no request owns a
     * ticket yet.  REOPENED, rather than an accidental idle interval, is the
     * only evidence that a subsequent physical scan may be submitted. */
    if (state != nullptr)
        state->phase = Phase::Draining;
}

inline void reopenAfterRadioReset(State *state)
{
    if (state != nullptr && state->phase == Phase::Draining)
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

inline bool starting(const State *state)
{
    return state != nullptr && state->phase == Phase::Starting;
}

} // namespace TahoeWclPhysicalScanContracts

#endif /* TahoeWclPhysicalScanContracts_hpp */
