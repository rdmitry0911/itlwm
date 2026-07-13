//
//  TahoeBssBlacklistContracts.hpp
//  AirportItlwm
//

#ifndef TahoeBssBlacklistContracts_hpp
#define TahoeBssBlacklistContracts_hpp

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace TahoeBssBlacklistContracts {

static constexpr uint32_t kBadArgumentStatus = 0xe00002bc;
static constexpr uint32_t kSuccessStatus = 0;
static constexpr uint32_t kSelector = 0x174;
static constexpr uint32_t kNoInterfaceStatus = 0x66;
static constexpr uint32_t kInvalidArgumentStatus = 0x16;
static constexpr uint32_t kClassOwnerAbsentStatus = 0xe082280e;
static constexpr uint32_t kEventMessage = 0xa3;
static constexpr size_t kBssidLength = 6;
static constexpr size_t kMaxEntries = 7;
static constexpr size_t kRequestLength = 1 + kMaxEntries * kBssidLength;
static constexpr size_t kEventBodyLength = kMaxEntries * kBssidLength + 2;
static constexpr size_t kEventCapacity =
    sizeof(uint32_t) + kEventBodyLength;

struct AppliedState
{
    uint8_t count;
    uint8_t bssids[kMaxEntries][kBssidLength];
};
static_assert(sizeof(AppliedState) == kRequestLength,
              "BSS blacklist applied state must preserve seven BSSIDs");

struct EventCarrier
{
    uint32_t count;
    uint8_t body[kEventBodyLength];
};
static_assert(sizeof(EventCarrier) == kEventCapacity,
              "BSS blacklist event carrier must be 48 bytes");
static_assert(offsetof(EventCarrier, body) == sizeof(uint32_t),
              "BSS blacklist event variable body starts at +0x04");

// The current 25C56 selector routes test the interface before touching the
// request carrier. Keeping this scalar decision here makes the public
// short-circuit table independently testable without constructing an ifnet.
inline uint32_t routePreflightStatus(bool hasInterface, size_t requestLength,
                                     const void *requestData)
{
    if (!hasInterface)
        return kNoInterfaceStatus;
    if (requestLength != kRequestLength || requestData == nullptr)
        return kInvalidArgumentStatus;
    return kSuccessStatus;
}

// The vtable slot is ABI-declared as bool locally, so its exact local raw
// status domain is 0/1. Preserve that domain rather than changing the slot.
inline uint32_t localAdmissionStatus(bool prohibited)
{
    return prohibited ? 1U : kSuccessStatus;
}

// Reference wrappers propagate admission before attempting their required
// class cast. The helper retains arbitrary non-zero values for the recovered
// wrapper contract even though the current local vtable gate is boolean.
inline uint32_t wrapperStatus(uint32_t admissionStatus,
                              bool requiredClassCastSucceeded)
{
    if (admissionStatus != kSuccessStatus)
        return admissionStatus;
    return requiredClassCastSucceeded ? kSuccessStatus
                                      : kClassOwnerAbsentStatus;
}

inline size_t eventTrailingOffset(uint8_t count)
{
    return sizeof(uint32_t) + count * kBssidLength;
}

inline bool decodeAppliedState(const uint8_t *request, AppliedState *state)
{
    if (request == nullptr || state == nullptr)
        return false;

    const uint8_t count = request[0];
    if (count > kMaxEntries)
        return false;

    memset(state, 0, sizeof(*state));
    state->count = count;
    if (count != 0) {
        memcpy(state->bssids, request + 1, count * kBssidLength);
    }
    return true;
}

inline size_t buildEventCarrier(uint8_t count, const uint8_t *bssids,
                                EventCarrier *event)
{
    if (event == nullptr || count > kMaxEntries ||
        (count != 0 && bssids == nullptr))
        return 0;

    memset(event, 0, sizeof(*event));
    event->count = count;
    if (count == 0)
        return 0;

    memcpy(event->body, bssids, count * kBssidLength);
    return eventTrailingOffset(count) + 2;
}

} // namespace TahoeBssBlacklistContracts

#endif /* TahoeBssBlacklistContracts_hpp */
