#ifndef ITL_STATE_TRANSITION_LEASE_HPP
#define ITL_STATE_TRANSITION_LEASE_HPP

#include <stdint.h>

/* Copied at lower ingress. The join sequence changes even when a request
 * names the same BSS; generation zero records cancellation/ordinary policy.
 * The caller obtains all three fields under the selected-BSS leaf. */
struct ItlStateTransitionIdentity {
    uint64_t joinSequence;
    uint64_t joinGeneration;
    uint64_t associationEpoch;

    bool equals(const ItlStateTransitionIdentity &other) const
    {
        return joinSequence == other.joinSequence &&
            joinGeneration == other.joinGeneration &&
            associationEpoch == other.associationEpoch;
    }
};

struct ItlStateTransitionRequest {
    enum : uint8_t { RunStopped = 1, Deauthenticated = 2 };
    uint64_t serial;
    uint32_t hardwareGeneration;
    int state;
    int argument;
    ItlStateTransitionIdentity identity;
    /* Host-only copied SCAN ingress; not a firmware or Apple carrier ABI. */
    uint64_t scanGeneration;
    uint64_t scanJoinGeneration;
    uint32_t scanHomeAwayMs;
    uint8_t scanSsidLength;
    uint8_t scanSsid[32];
    uint8_t lowerCompleted;
};

/* Value-only queue owner, serialized by the HAL's scan leaf. A callback
 * takes a copy, not a pointer into the slot another ingress will replace. */
struct ItlStateTransitionLease {
    enum class Stage : uint8_t { Empty, Prepared, Queued, Executing, Deferred, Pending, Committed };
    uint64_t nextSerial;
    ItlStateTransitionRequest request;
    Stage stage;
    int result;

    void invalidate()
    {
        request = ItlStateTransitionRequest{};
        stage = Stage::Empty;
        result = 0;
    }

    bool current(const ItlStateTransitionRequest &copy, uint32_t generation) const
    {
        return copy.serial != 0 && request.serial == copy.serial &&
            request.hardwareGeneration == generation &&
            copy.hardwareGeneration == generation;
    }

    bool duplicate(uint32_t generation, int state, int argument,
                   const ItlStateTransitionIdentity &identity) const
    {
        return request.serial != 0 && request.hardwareGeneration == generation &&
            request.state == state && request.argument == argument &&
            request.identity.equals(identity);
    }

    bool prepare(uint32_t generation, int state, int argument,
                 const ItlStateTransitionIdentity &identity,
                 ItlStateTransitionRequest *out)
    {
        if (out == nullptr || nextSerial == UINT64_MAX)
            return false;
        request = ItlStateTransitionRequest{};
        request.serial = ++nextSerial;
        request.hardwareGeneration = generation;
        request.state = state;
        request.argument = argument;
        request.identity = identity;
        stage = Stage::Prepared;
        result = 0;
        *out = request;
        return true;
    }

    bool enqueue(const ItlStateTransitionRequest &copy, uint32_t generation)
    {
        if (!current(copy, generation) || stage != Stage::Prepared)
            return false;
        stage = Stage::Queued;
        return true;
    }

    bool take(uint32_t generation, ItlStateTransitionRequest *out)
    {
        if (out == nullptr || stage != Stage::Queued || request.serial == 0 ||
            request.hardwareGeneration != generation)
            return false;
        *out = request;
        stage = Stage::Executing;
        return true;
    }

    bool publish(const ItlStateTransitionRequest &copy, uint32_t generation,
                 int lowerResult)
    {
        if (!current(copy, generation) ||
            (stage != Stage::Prepared && stage != Stage::Executing))
            return false;
        result = lowerResult;
        stage = Stage::Pending;
        return true;
    }

    bool completeLowerStep(ItlStateTransitionRequest *copy, uint32_t generation,
                           uint8_t step)
    {
        if (copy == nullptr || !current(*copy, generation) || stage != Stage::Executing ||
            (step != ItlStateTransitionRequest::RunStopped &&
             step != ItlStateTransitionRequest::Deauthenticated))
            return false;
        request.lowerCompleted |= step;
        copy->lowerCompleted = request.lowerCompleted;
        return true;
    }

    bool defer(const ItlStateTransitionRequest &copy, uint32_t generation)
    {
        if (!current(copy, generation) || stage != Stage::Executing)
            return false;
        stage = Stage::Deferred;
        return true;
    }

    bool resume(uint32_t generation)
    {
        if (stage != Stage::Deferred || request.serial == 0 ||
            request.hardwareGeneration != generation)
            return false;
        stage = Stage::Queued;
        return true;
    }

    bool takeCommit(uint32_t generation, ItlStateTransitionRequest *out,
                    int *lowerResult)
    {
        if (out == nullptr || lowerResult == nullptr || stage != Stage::Pending ||
            request.serial == 0 || request.hardwareGeneration != generation)
            return false;
        *out = request;
        *lowerResult = result;
        stage = Stage::Committed;
        return true;
    }
};

#endif
