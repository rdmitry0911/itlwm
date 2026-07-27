/*
 * Laboratory-only direct-IWN SAE stimulus ABI, version 1.
 *
 * This is neither an Apple80211/WCL association carrier nor the private IWN
 * credential-stage ABI.  It exists solely in the separately compiled
 * IWN_SOFTWARE_PMF_LAB_BUILD artifact so a controlled physical AP can test
 * the driver-owned SAE/PMF lower half without inventing a CoreWLAN credential
 * provenance.  A normal artifact does not publish the matching UserClient
 * type or selector.
 */
#ifndef AIRPORT_ITLWM_IWN_LAB_DIRECT_SAE_STIMULUS_V1_H
#define AIRPORT_ITLWM_IWN_LAB_DIRECT_SAE_STIMULUS_V1_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define kAirportItlwmIwnLabDirectSaeStimulusV1Version 1u
#define kAirportItlwmIwnLabDirectSaeStimulusV1SsidMaxLength 32u
#define kAirportItlwmIwnLabDirectSaeStimulusV1PassphraseMinLength 8u
#define kAirportItlwmIwnLabDirectSaeStimulusV1PassphraseMaxLength 63u
#define kAirportItlwmIwnLabDirectSaeStimulusV1PasswordStorageLength 64u
#define kAirportItlwmIwnLabDirectSaeStimulusV1MacLength 6u

/* Separate from the product PLTI ('PLTI') channel.  This type is accepted
 * only when AIRPORT_ITLWM_IWN_DIRECT_SAE_LAB_STIMULUS is compiled in. */
#define kAirportItlwmIwnLabDirectSaeStimulusUserClientType ('ISAE')

enum AirportItlwmIwnLabDirectSaeStimulusSelectorV1 {
    kAirportItlwmIwnLabDirectSaeStimulusQueryReadySelector = 0,
    kAirportItlwmIwnLabDirectSaeStimulusSubmitSelector = 1,
    kAirportItlwmIwnLabDirectSaeStimulusQueryOutcomeSelector = 2,
    kAirportItlwmIwnLabDirectSaeStimulusSelectorCount = 3,
};

/* The caller selects no radio parameters.  This value only selects one of
 * the two exact SAE policy forms already accepted by the direct SAE engine;
 * the fresh selected BSS remains authoritative and any mismatch fails closed.
 */
enum AirportItlwmIwnLabDirectSaeStimulusProfileV1 {
    kAirportItlwmIwnLabDirectSaeStimulusPureSae = 1,
    kAirportItlwmIwnLabDirectSaeStimulusSaeWpa2PskTransition = 2,
};

enum AirportItlwmIwnLabDirectSaeStimulusReadyV1 {
    kAirportItlwmIwnLabDirectSaeStimulusUnsupported = 0,
    kAirportItlwmIwnLabDirectSaeStimulusNotReady = 1,
    kAirportItlwmIwnLabDirectSaeStimulusReady = 2,
};

/* QueryOutcome exposes only the first bounded dispatch stage.  It contains
 * no IOReturn value, radio identity, credential material, generation, or
 * protocol frame.  Pending is published before the request is copied into
 * the one-slot mailbox; every other value is terminal for that submission. */
enum AirportItlwmIwnLabDirectSaeStimulusOutcomeV1 {
    kAirportItlwmIwnLabDirectSaeStimulusOutcomePending = 0,
    kAirportItlwmIwnLabDirectSaeStimulusOutcomeStarted = 1,
    kAirportItlwmIwnLabDirectSaeStimulusOutcomeRejectedPrecondition = 2,
    kAirportItlwmIwnLabDirectSaeStimulusOutcomeRejectedRequestBegin = 3,
    kAirportItlwmIwnLabDirectSaeStimulusOutcomeRejectedAssociationOwner = 4,
    kAirportItlwmIwnLabDirectSaeStimulusOutcomeRejectedCredentialStage = 5,
    kAirportItlwmIwnLabDirectSaeStimulusOutcomeRejectedAuthType = 6,
    kAirportItlwmIwnLabDirectSaeStimulusOutcomeRejectedScanResume = 7,
    kAirportItlwmIwnLabDirectSaeStimulusOutcomeCancelled = 8,
    kAirportItlwmIwnLabDirectSaeStimulusOutcomeCount = 9,
};

/* Fixed input only.  In particular it contains no generation, PMK, PMKID,
 * PWE, KCK, RSN/RSNXE, channel, management frame, or pointer. */
struct AirportItlwmIwnLabDirectSaeStimulusRequestV1 {
    uint32_t version;
    uint32_t size;
    uint32_t profile;
    uint32_t ssid_len;
    uint32_t password_len;
    uint8_t bssid[kAirportItlwmIwnLabDirectSaeStimulusV1MacLength];
    uint8_t reserved0[2];
    uint8_t ssid[kAirportItlwmIwnLabDirectSaeStimulusV1SsidMaxLength];
    uint8_t password[kAirportItlwmIwnLabDirectSaeStimulusV1PasswordStorageLength];
    uint8_t reserved[4];
};

/* QueryReady is deliberately identity-free and credential-free. */
struct AirportItlwmIwnLabDirectSaeStimulusReadyReplyV1 {
    uint32_t version;
    uint32_t size;
    uint32_t readiness;
    uint32_t reserved;
};

/* QueryOutcome is bound implicitly to the kernel-generated cookie of the
 * calling UserClient. */
struct AirportItlwmIwnLabDirectSaeStimulusOutcomeReplyV1 {
    uint32_t version;
    uint32_t size;
    uint32_t outcome;
    uint32_t reserved;
};

#if defined(__cplusplus)
#define AIRPORT_ITLWM_IWN_LAB_DIRECT_SAE_STATIC_ASSERT(condition, message) \
    static_assert((condition), message)
#else
#define AIRPORT_ITLWM_IWN_LAB_DIRECT_SAE_STATIC_ASSERT(condition, message) \
    _Static_assert((condition), message)
#endif

AIRPORT_ITLWM_IWN_LAB_DIRECT_SAE_STATIC_ASSERT(
    sizeof(struct AirportItlwmIwnLabDirectSaeStimulusRequestV1) == 128,
    "direct SAE lab request ABI size");
AIRPORT_ITLWM_IWN_LAB_DIRECT_SAE_STATIC_ASSERT(
    offsetof(struct AirportItlwmIwnLabDirectSaeStimulusRequestV1, profile) == 8,
    "direct SAE lab profile offset");
AIRPORT_ITLWM_IWN_LAB_DIRECT_SAE_STATIC_ASSERT(
    offsetof(struct AirportItlwmIwnLabDirectSaeStimulusRequestV1, bssid) == 20,
    "direct SAE lab BSSID offset");
AIRPORT_ITLWM_IWN_LAB_DIRECT_SAE_STATIC_ASSERT(
    offsetof(struct AirportItlwmIwnLabDirectSaeStimulusRequestV1, ssid) == 28,
    "direct SAE lab SSID offset");
AIRPORT_ITLWM_IWN_LAB_DIRECT_SAE_STATIC_ASSERT(
    offsetof(struct AirportItlwmIwnLabDirectSaeStimulusRequestV1, password) == 60,
    "direct SAE lab password offset");
AIRPORT_ITLWM_IWN_LAB_DIRECT_SAE_STATIC_ASSERT(
    sizeof(struct AirportItlwmIwnLabDirectSaeStimulusReadyReplyV1) == 16,
    "direct SAE lab readiness ABI size");
AIRPORT_ITLWM_IWN_LAB_DIRECT_SAE_STATIC_ASSERT(
    sizeof(struct AirportItlwmIwnLabDirectSaeStimulusOutcomeReplyV1) == 16,
    "direct SAE lab outcome ABI size");

static inline bool
AirportItlwmIwnLabDirectSaeStimulusBytesAllZero(const uint8_t *bytes,
                                                size_t length)
{
    size_t index;

    if (bytes == NULL)
        return true;
    for (index = 0; index < length; ++index) {
        if (bytes[index] != 0)
            return false;
    }
    return true;
}

static inline bool
AirportItlwmIwnLabDirectSaeStimulusBssidIsUnicastNonzero(
    const uint8_t bssid[kAirportItlwmIwnLabDirectSaeStimulusV1MacLength])
{
    return bssid != NULL && (bssid[0] & 0x01u) == 0 &&
        !AirportItlwmIwnLabDirectSaeStimulusBytesAllZero(bssid,
            kAirportItlwmIwnLabDirectSaeStimulusV1MacLength);
}

static inline bool
AirportItlwmIwnLabDirectSaeStimulusProfileIsExact(uint32_t profile)
{
    return profile == kAirportItlwmIwnLabDirectSaeStimulusPureSae ||
        profile ==
            kAirportItlwmIwnLabDirectSaeStimulusSaeWpa2PskTransition;
}

static inline bool
AirportItlwmIwnLabDirectSaeStimulusRequestIsWellFormed(
    const struct AirportItlwmIwnLabDirectSaeStimulusRequestV1 *request)
{
    return request != NULL &&
        request->version == kAirportItlwmIwnLabDirectSaeStimulusV1Version &&
        request->size == sizeof(*request) &&
        AirportItlwmIwnLabDirectSaeStimulusProfileIsExact(request->profile) &&
        request->ssid_len != 0 &&
        request->ssid_len <= sizeof(request->ssid) &&
        request->password_len >=
            kAirportItlwmIwnLabDirectSaeStimulusV1PassphraseMinLength &&
        request->password_len <=
            kAirportItlwmIwnLabDirectSaeStimulusV1PassphraseMaxLength &&
        AirportItlwmIwnLabDirectSaeStimulusBssidIsUnicastNonzero(
            request->bssid) &&
        AirportItlwmIwnLabDirectSaeStimulusBytesAllZero(request->reserved0,
            sizeof(request->reserved0)) &&
        AirportItlwmIwnLabDirectSaeStimulusBytesAllZero(request->ssid +
            request->ssid_len, sizeof(request->ssid) - request->ssid_len) &&
        AirportItlwmIwnLabDirectSaeStimulusBytesAllZero(request->password +
            request->password_len,
            sizeof(request->password) - request->password_len) &&
        AirportItlwmIwnLabDirectSaeStimulusBytesAllZero(request->reserved,
            sizeof(request->reserved));
}

static inline bool
AirportItlwmIwnLabDirectSaeStimulusReadyReplyIsWellFormed(
    const struct AirportItlwmIwnLabDirectSaeStimulusReadyReplyV1 *reply)
{
    return reply != NULL &&
        reply->version == kAirportItlwmIwnLabDirectSaeStimulusV1Version &&
        reply->size == sizeof(*reply) &&
        reply->readiness <= kAirportItlwmIwnLabDirectSaeStimulusReady &&
        reply->reserved == 0;
}

static inline bool
AirportItlwmIwnLabDirectSaeStimulusOutcomeReplyIsWellFormed(
    const struct AirportItlwmIwnLabDirectSaeStimulusOutcomeReplyV1 *reply)
{
    return reply != NULL &&
        reply->version == kAirportItlwmIwnLabDirectSaeStimulusV1Version &&
        reply->size == sizeof(*reply) &&
        reply->outcome < kAirportItlwmIwnLabDirectSaeStimulusOutcomeCount &&
        reply->reserved == 0;
}

static inline void
AirportItlwmIwnLabDirectSaeStimulusRequestScrub(
    struct AirportItlwmIwnLabDirectSaeStimulusRequestV1 *request)
{
    volatile uint8_t *bytes;
    size_t index;

    if (request == NULL)
        return;
    bytes = (volatile uint8_t *)request;
    for (index = 0; index < sizeof(*request); ++index)
        bytes[index] = 0;
}

#endif /* AIRPORT_ITLWM_IWN_LAB_DIRECT_SAE_STIMULUS_V1_H */
