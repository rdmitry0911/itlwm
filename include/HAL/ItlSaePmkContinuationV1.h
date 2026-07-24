/*
 * Private SAE PMK continuation ABI.
 *
 * This is intentionally separate from ItlSaeAuthTransportV1.h.  The latter
 * remains a credential-free Algorithm-3/selected-BSS transport boundary;
 * this record crosses only the controller -> HAL handoff after the relay FSM
 * has accepted a verified completion.  It is never a UserClient ABI, mailbox
 * payload, log value, or generic PMK carrier.
 */
#ifndef _HAL_ITL_SAE_PMK_CONTINUATION_V1_H_
#define _HAL_ITL_SAE_PMK_CONTINUATION_V1_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define kItlSaePmkContinuationV1Version 1u
#define kItlSaePmkContinuationV1MacLength 6u
#define kItlSaePmkContinuationV1PmkLength 32u
#define kItlSaePmkContinuationV1PmkidLength 16u

/* Non-secret cancellation/revalidation identity for one completion. */
struct ItlSaePmkContinuationIdentityV1 {
    uint32_t version;
    uint32_t size;
    uint64_t request_generation;
    uint64_t association_epoch;
    uint64_t relay_generation;
    uint64_t event_sequence;
    uint8_t bssid[kItlSaePmkContinuationV1MacLength];
    uint8_t sta[kItlSaePmkContinuationV1MacLength];
    uint8_t reserved[4];
};

/*
 * Secret one-shot handoff.  The HAL must copy it before returning and scrub
 * every retained/local copy on claim, cancellation, failure, stop, and
 * detach.  PMKID is supplied by the Agent but is canonicalized and checked by
 * net80211 before it can reach a node; an all-zero PMKID is not rejected at
 * syntax-validation time because V1 has no "pmkid present" field.
 */
struct ItlSaePmkContinuationV1 {
    struct ItlSaePmkContinuationIdentityV1 identity;
    uint8_t pmk[kItlSaePmkContinuationV1PmkLength];
    uint8_t pmkid[kItlSaePmkContinuationV1PmkidLength];
    uint8_t reserved[8];
};

#if defined(__cplusplus)
#define ITL_SAE_PMK_CONTINUATION_STATIC_ASSERT(condition, message) \
    static_assert((condition), message)
#else
#define ITL_SAE_PMK_CONTINUATION_STATIC_ASSERT(condition, message) \
    _Static_assert((condition), message)
#endif

ITL_SAE_PMK_CONTINUATION_STATIC_ASSERT(
    sizeof(struct ItlSaePmkContinuationIdentityV1) == 56,
    "SAE PMK continuation identity ABI size");
ITL_SAE_PMK_CONTINUATION_STATIC_ASSERT(
    sizeof(struct ItlSaePmkContinuationV1) == 112,
    "SAE PMK continuation ABI size");
ITL_SAE_PMK_CONTINUATION_STATIC_ASSERT(
    offsetof(struct ItlSaePmkContinuationIdentityV1, bssid) == 40,
    "SAE PMK continuation identity BSSID offset");
ITL_SAE_PMK_CONTINUATION_STATIC_ASSERT(
    offsetof(struct ItlSaePmkContinuationV1, identity) == 0,
    "SAE PMK continuation identity offset");
ITL_SAE_PMK_CONTINUATION_STATIC_ASSERT(
    offsetof(struct ItlSaePmkContinuationV1, identity) +
        offsetof(struct ItlSaePmkContinuationIdentityV1, bssid) == 40,
    "SAE PMK continuation BSSID offset");
ITL_SAE_PMK_CONTINUATION_STATIC_ASSERT(
    offsetof(struct ItlSaePmkContinuationV1, pmk) == 56,
    "SAE PMK continuation PMK offset");
ITL_SAE_PMK_CONTINUATION_STATIC_ASSERT(
    offsetof(struct ItlSaePmkContinuationV1, pmkid) == 88,
    "SAE PMK continuation PMKID offset");

static inline bool
itl_sae_pmk_continuation_bytes_all_zero(const uint8_t *bytes, size_t length)
{
    size_t index;

    if (bytes == NULL)
        return true;
    for (index = 0; index < length; index++) {
        if (bytes[index] != 0)
            return false;
    }
    return true;
}

static inline bool
itl_sae_pmk_continuation_mac_is_unicast_nonzero(const uint8_t mac[
    kItlSaePmkContinuationV1MacLength])
{
    return mac != NULL && (mac[0] & 0x01u) == 0 &&
        !itl_sae_pmk_continuation_bytes_all_zero(mac,
            kItlSaePmkContinuationV1MacLength);
}

static inline bool
itl_sae_pmk_continuation_identity_is_well_formed(
    const struct ItlSaePmkContinuationIdentityV1 *identity)
{
    return identity != NULL &&
        identity->version == kItlSaePmkContinuationV1Version &&
        identity->size == sizeof(*identity) &&
        identity->request_generation != 0 &&
        identity->association_epoch != 0 &&
        identity->relay_generation != 0 &&
        identity->event_sequence != 0 &&
        itl_sae_pmk_continuation_mac_is_unicast_nonzero(identity->bssid) &&
        itl_sae_pmk_continuation_mac_is_unicast_nonzero(identity->sta) &&
        itl_sae_pmk_continuation_bytes_all_zero(identity->reserved,
            sizeof(identity->reserved));
}

static inline bool
itl_sae_pmk_continuation_is_well_formed(
    const struct ItlSaePmkContinuationV1 *continuation)
{
    return continuation != NULL &&
        itl_sae_pmk_continuation_identity_is_well_formed(
            &continuation->identity) &&
        !itl_sae_pmk_continuation_bytes_all_zero(continuation->pmk,
            sizeof(continuation->pmk)) &&
        itl_sae_pmk_continuation_bytes_all_zero(continuation->reserved,
            sizeof(continuation->reserved));
}

static inline bool
itl_sae_pmk_continuation_matches_identity(
    const struct ItlSaePmkContinuationV1 *continuation,
    const struct ItlSaePmkContinuationIdentityV1 *identity)
{
    size_t index;

    if (continuation == NULL || identity == NULL ||
        continuation->identity.request_generation != identity->request_generation ||
        continuation->identity.association_epoch != identity->association_epoch ||
        continuation->identity.relay_generation != identity->relay_generation ||
        continuation->identity.event_sequence != identity->event_sequence)
        return false;
    for (index = 0; index < kItlSaePmkContinuationV1MacLength; index++) {
        if (continuation->identity.bssid[index] != identity->bssid[index] ||
            continuation->identity.sta[index] != identity->sta[index])
            return false;
    }
    return true;
}

#endif /* _HAL_ITL_SAE_PMK_CONTINUATION_V1_H_ */
