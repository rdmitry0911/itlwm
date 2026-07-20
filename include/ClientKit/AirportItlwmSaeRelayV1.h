/*
 * AirportItlwm product SAE relay ABI, version 1.
 *
 * This is a declaration-only contract.  It reserves no password, PWE, KCK,
 * or raw RSNXE carrier in the kernel.  When wired, the kext owns 802.11
 * Authentication algorithm 3, addresses, transaction/status fields, session
 * epochs, and TX-completion fencing.  The Agent may only return a bounded
 * semantic Commit or Confirm body and a PMK/PMKID after verified Confirm.
 */
#ifndef AIRPORT_ITLWM_SAE_RELAY_V1_H
#define AIRPORT_ITLWM_SAE_RELAY_V1_H

#include <stddef.h>
#include <stdint.h>

#define kAirportItlwmSaeRelayV1Version          1u
#define kAirportItlwmSaeRelayV1NonceLength      16u
#define kAirportItlwmSaeRelayV1SsidMaxLength    32u
#define kAirportItlwmSaeRelayV1MacLength        6u
#define kAirportItlwmSaeRelayV1PmkLength        32u
#define kAirportItlwmSaeRelayV1PmkidLength      16u
#define kAirportItlwmSaeRelayV1MaxAuthBodyLength 768u

/* Append-only Tahoe PLTI selectors; legacy DeliverPMK/WaitTarget stay 0/1. */
enum AirportItlwmSaeRelaySelectorV1 {
    kAirportItlwmSaeRelayWaitTargetSelector = 2,
    kAirportItlwmSaeRelaySubmitReplySelector = 3,
    kAirportItlwmSaeRelayWaitAuthEventSelector = 4,
    kAirportItlwmSaeRelayCompleteSelector = 5,
    kAirportItlwmSaeRelayAbortSelector = 6,
    kAirportItlwmSaeRelaySelectorCount = 7,
};

enum AirportItlwmSaeRelayMethodV1 {
    kAirportItlwmSaeRelayMethodHuntingAndPecking = 1,
    kAirportItlwmSaeRelayMethodHashToElement = 2,
};

enum AirportItlwmSaeRelayReplyKindV1 {
    kAirportItlwmSaeRelayReplyCommit = 1,
    kAirportItlwmSaeRelayReplyConfirm = 2,
};

/* Canonical product facts, not bit positions copied from a raw RSNXE IE. */
enum AirportItlwmSaeRelayRsnxeCapabilityV1 {
    kAirportItlwmSaeRelayRsnxeH2e = 1u << 0,
};

/* Target flags are normalized BSS facts, never raw information elements. */
enum AirportItlwmSaeRelayTargetFlagV1 {
    kAirportItlwmSaeRelayTargetPureSae = 1u << 0,
    kAirportItlwmSaeRelayTargetPmfCapable = 1u << 1,
    kAirportItlwmSaeRelayTargetPmfRequired = 1u << 2,
    kAirportItlwmSaeRelayTargetRsnxePresent = 1u << 3,
};

/*
 * Selected-BSS snapshot published by the kext.  controller_nonce is per kext
 * lifetime; client_cookie is generated for the owning UserClient.  Both, plus
 * generation/epoch and BSSID/STA, bind every non-wait reply to this exact
 * association attempt.
 */
struct AirportItlwmSaeTargetV1 {
    uint32_t version;
    uint32_t size;
    uint32_t flags;
    uint16_t sae_group;
    uint16_t sae_method;
    uint32_t rsnxe_capabilities; /* AirportItlwmSaeRelayRsnxeCapabilityV1 */
    uint32_t authtype_lower;
    uint32_t authtype_upper;
    uint32_t ssid_len;
    uint64_t generation;
    uint64_t association_epoch;
    uint8_t controller_nonce[kAirportItlwmSaeRelayV1NonceLength];
    uint8_t client_cookie[kAirportItlwmSaeRelayV1NonceLength];
    uint8_t ssid[kAirportItlwmSaeRelayV1SsidMaxLength];
    uint8_t bssid[kAirportItlwmSaeRelayV1MacLength];
    uint8_t sta[kAirportItlwmSaeRelayV1MacLength];
    uint8_t reserved[4];
};

/* Kext-validated peer Algorithm-3 event.  body is bounded public wire data. */
struct AirportItlwmSaeAuthEventV1 {
    uint32_t version;
    uint32_t size;
    uint32_t phase;
    uint16_t transaction;
    uint16_t status;
    uint32_t body_len;
    uint32_t reserved0;
    uint64_t generation;
    uint64_t association_epoch;
    uint64_t event_sequence;
    uint8_t controller_nonce[kAirportItlwmSaeRelayV1NonceLength];
    uint8_t client_cookie[kAirportItlwmSaeRelayV1NonceLength];
    uint8_t bssid[kAirportItlwmSaeRelayV1MacLength];
    uint8_t sta[kAirportItlwmSaeRelayV1MacLength];
    uint8_t reserved1[4];
    uint8_t body[kAirportItlwmSaeRelayV1MaxAuthBodyLength];
};

/* Agent semantic Commit/Confirm reply; kext supplies all 802.11 headers. */
struct AirportItlwmSaeAuthReplyV1 {
    uint32_t version;
    uint32_t size;
    uint32_t kind;
    uint32_t body_len;
    uint64_t generation;
    uint64_t association_epoch;
    uint64_t event_sequence;
    uint8_t controller_nonce[kAirportItlwmSaeRelayV1NonceLength];
    uint8_t client_cookie[kAirportItlwmSaeRelayV1NonceLength];
    uint8_t bssid[kAirportItlwmSaeRelayV1MacLength];
    uint8_t sta[kAirportItlwmSaeRelayV1MacLength];
    uint8_t reserved[4];
    uint8_t body[kAirportItlwmSaeRelayV1MaxAuthBodyLength];
};

/* Valid only after the kext observes a matching verified Confirm. */
struct AirportItlwmSaeCompletionV1 {
    uint32_t version;
    uint32_t size;
    uint64_t generation;
    uint64_t association_epoch;
    uint64_t event_sequence;
    uint8_t controller_nonce[kAirportItlwmSaeRelayV1NonceLength];
    uint8_t client_cookie[kAirportItlwmSaeRelayV1NonceLength];
    uint8_t bssid[kAirportItlwmSaeRelayV1MacLength];
    uint8_t sta[kAirportItlwmSaeRelayV1MacLength];
    uint8_t reserved0[4];
    uint8_t pmk[kAirportItlwmSaeRelayV1PmkLength];
    uint8_t pmkid[kAirportItlwmSaeRelayV1PmkidLength];
    uint8_t reserved[16];
};

/* Bounded terminal error with no credential-bearing fields. */
struct AirportItlwmSaeAbortV1 {
    uint32_t version;
    uint32_t size;
    uint32_t reason;
    uint32_t reserved0;
    uint64_t generation;
    uint64_t association_epoch;
    uint64_t event_sequence;
    uint8_t controller_nonce[kAirportItlwmSaeRelayV1NonceLength];
    uint8_t client_cookie[kAirportItlwmSaeRelayV1NonceLength];
    uint8_t bssid[kAirportItlwmSaeRelayV1MacLength];
    uint8_t sta[kAirportItlwmSaeRelayV1MacLength];
    uint8_t reserved[12];
};

#if defined(__cplusplus)
#define AIRPORT_ITLWM_SAE_RELAY_STATIC_ASSERT(condition, message) \
    static_assert((condition), message)
#define AIRPORT_ITLWM_SAE_RELAY_ALIGNOF(type) alignof(type)
#else
#define AIRPORT_ITLWM_SAE_RELAY_STATIC_ASSERT(condition, message) \
    _Static_assert((condition), message)
#define AIRPORT_ITLWM_SAE_RELAY_ALIGNOF(type) _Alignof(type)
#endif

#define AIRPORT_ITLWM_SAE_RELAY_ASSERT_OFFSET(type, field, expected) \
    AIRPORT_ITLWM_SAE_RELAY_STATIC_ASSERT(offsetof(struct type, field) == \
        (expected), "SAE relay ABI field offset")
#define AIRPORT_ITLWM_SAE_RELAY_ASSERT_TERMINAL(type, field) \
    AIRPORT_ITLWM_SAE_RELAY_STATIC_ASSERT(offsetof(struct type, field) + \
        sizeof(((struct type *)0)->field) == sizeof(struct type), \
        "SAE relay ABI terminal field")

AIRPORT_ITLWM_SAE_RELAY_STATIC_ASSERT(sizeof(struct AirportItlwmSaeTargetV1) == 128,
    "SAE target ABI size");
AIRPORT_ITLWM_SAE_RELAY_ASSERT_OFFSET(AirportItlwmSaeTargetV1, version, 0);
AIRPORT_ITLWM_SAE_RELAY_ASSERT_OFFSET(AirportItlwmSaeTargetV1, size, 4);
AIRPORT_ITLWM_SAE_RELAY_ASSERT_OFFSET(AirportItlwmSaeTargetV1, flags, 8);
AIRPORT_ITLWM_SAE_RELAY_ASSERT_OFFSET(AirportItlwmSaeTargetV1, sae_group, 12);
AIRPORT_ITLWM_SAE_RELAY_ASSERT_OFFSET(AirportItlwmSaeTargetV1, sae_method, 14);
AIRPORT_ITLWM_SAE_RELAY_ASSERT_OFFSET(AirportItlwmSaeTargetV1,
    rsnxe_capabilities, 16);
AIRPORT_ITLWM_SAE_RELAY_ASSERT_OFFSET(AirportItlwmSaeTargetV1,
    authtype_lower, 20);
AIRPORT_ITLWM_SAE_RELAY_ASSERT_OFFSET(AirportItlwmSaeTargetV1,
    authtype_upper, 24);
AIRPORT_ITLWM_SAE_RELAY_ASSERT_OFFSET(AirportItlwmSaeTargetV1, ssid_len, 28);
AIRPORT_ITLWM_SAE_RELAY_ASSERT_OFFSET(AirportItlwmSaeTargetV1, generation, 32);
AIRPORT_ITLWM_SAE_RELAY_ASSERT_OFFSET(AirportItlwmSaeTargetV1,
    association_epoch, 40);
AIRPORT_ITLWM_SAE_RELAY_ASSERT_OFFSET(AirportItlwmSaeTargetV1,
    controller_nonce, 48);
AIRPORT_ITLWM_SAE_RELAY_ASSERT_OFFSET(AirportItlwmSaeTargetV1,
    client_cookie, 64);
AIRPORT_ITLWM_SAE_RELAY_ASSERT_OFFSET(AirportItlwmSaeTargetV1, ssid, 80);
AIRPORT_ITLWM_SAE_RELAY_ASSERT_OFFSET(AirportItlwmSaeTargetV1, bssid, 112);
AIRPORT_ITLWM_SAE_RELAY_ASSERT_OFFSET(AirportItlwmSaeTargetV1, sta, 118);
AIRPORT_ITLWM_SAE_RELAY_ASSERT_OFFSET(AirportItlwmSaeTargetV1, reserved, 124);
AIRPORT_ITLWM_SAE_RELAY_ASSERT_TERMINAL(AirportItlwmSaeTargetV1, reserved);
AIRPORT_ITLWM_SAE_RELAY_STATIC_ASSERT(AIRPORT_ITLWM_SAE_RELAY_ALIGNOF(
    struct AirportItlwmSaeTargetV1) == 8, "SAE target ABI alignment");

AIRPORT_ITLWM_SAE_RELAY_STATIC_ASSERT(sizeof(struct AirportItlwmSaeAuthEventV1) == 864,
    "SAE event ABI size");
AIRPORT_ITLWM_SAE_RELAY_ASSERT_OFFSET(AirportItlwmSaeAuthEventV1, version, 0);
AIRPORT_ITLWM_SAE_RELAY_ASSERT_OFFSET(AirportItlwmSaeAuthEventV1, size, 4);
AIRPORT_ITLWM_SAE_RELAY_ASSERT_OFFSET(AirportItlwmSaeAuthEventV1, phase, 8);
AIRPORT_ITLWM_SAE_RELAY_ASSERT_OFFSET(AirportItlwmSaeAuthEventV1,
    transaction, 12);
AIRPORT_ITLWM_SAE_RELAY_ASSERT_OFFSET(AirportItlwmSaeAuthEventV1, status, 14);
AIRPORT_ITLWM_SAE_RELAY_ASSERT_OFFSET(AirportItlwmSaeAuthEventV1,
    body_len, 16);
AIRPORT_ITLWM_SAE_RELAY_ASSERT_OFFSET(AirportItlwmSaeAuthEventV1,
    reserved0, 20);
AIRPORT_ITLWM_SAE_RELAY_ASSERT_OFFSET(AirportItlwmSaeAuthEventV1,
    generation, 24);
AIRPORT_ITLWM_SAE_RELAY_ASSERT_OFFSET(AirportItlwmSaeAuthEventV1,
    association_epoch, 32);
AIRPORT_ITLWM_SAE_RELAY_ASSERT_OFFSET(AirportItlwmSaeAuthEventV1,
    event_sequence, 40);
AIRPORT_ITLWM_SAE_RELAY_ASSERT_OFFSET(AirportItlwmSaeAuthEventV1,
    controller_nonce, 48);
AIRPORT_ITLWM_SAE_RELAY_ASSERT_OFFSET(AirportItlwmSaeAuthEventV1,
    client_cookie, 64);
AIRPORT_ITLWM_SAE_RELAY_ASSERT_OFFSET(AirportItlwmSaeAuthEventV1, bssid, 80);
AIRPORT_ITLWM_SAE_RELAY_ASSERT_OFFSET(AirportItlwmSaeAuthEventV1, sta, 86);
AIRPORT_ITLWM_SAE_RELAY_ASSERT_OFFSET(AirportItlwmSaeAuthEventV1,
    reserved1, 92);
AIRPORT_ITLWM_SAE_RELAY_ASSERT_OFFSET(AirportItlwmSaeAuthEventV1, body, 96);
AIRPORT_ITLWM_SAE_RELAY_ASSERT_TERMINAL(AirportItlwmSaeAuthEventV1, body);
AIRPORT_ITLWM_SAE_RELAY_STATIC_ASSERT(AIRPORT_ITLWM_SAE_RELAY_ALIGNOF(
    struct AirportItlwmSaeAuthEventV1) == 8, "SAE event ABI alignment");

AIRPORT_ITLWM_SAE_RELAY_STATIC_ASSERT(sizeof(struct AirportItlwmSaeAuthReplyV1) == 856,
    "SAE reply ABI size");
AIRPORT_ITLWM_SAE_RELAY_ASSERT_OFFSET(AirportItlwmSaeAuthReplyV1, version, 0);
AIRPORT_ITLWM_SAE_RELAY_ASSERT_OFFSET(AirportItlwmSaeAuthReplyV1, size, 4);
AIRPORT_ITLWM_SAE_RELAY_ASSERT_OFFSET(AirportItlwmSaeAuthReplyV1, kind, 8);
AIRPORT_ITLWM_SAE_RELAY_ASSERT_OFFSET(AirportItlwmSaeAuthReplyV1, body_len, 12);
AIRPORT_ITLWM_SAE_RELAY_ASSERT_OFFSET(AirportItlwmSaeAuthReplyV1,
    generation, 16);
AIRPORT_ITLWM_SAE_RELAY_ASSERT_OFFSET(AirportItlwmSaeAuthReplyV1,
    association_epoch, 24);
AIRPORT_ITLWM_SAE_RELAY_ASSERT_OFFSET(AirportItlwmSaeAuthReplyV1,
    event_sequence, 32);
AIRPORT_ITLWM_SAE_RELAY_ASSERT_OFFSET(AirportItlwmSaeAuthReplyV1,
    controller_nonce, 40);
AIRPORT_ITLWM_SAE_RELAY_ASSERT_OFFSET(AirportItlwmSaeAuthReplyV1,
    client_cookie, 56);
AIRPORT_ITLWM_SAE_RELAY_ASSERT_OFFSET(AirportItlwmSaeAuthReplyV1, bssid, 72);
AIRPORT_ITLWM_SAE_RELAY_ASSERT_OFFSET(AirportItlwmSaeAuthReplyV1, sta, 78);
AIRPORT_ITLWM_SAE_RELAY_ASSERT_OFFSET(AirportItlwmSaeAuthReplyV1, reserved, 84);
AIRPORT_ITLWM_SAE_RELAY_ASSERT_OFFSET(AirportItlwmSaeAuthReplyV1, body, 88);
AIRPORT_ITLWM_SAE_RELAY_ASSERT_TERMINAL(AirportItlwmSaeAuthReplyV1, body);
AIRPORT_ITLWM_SAE_RELAY_STATIC_ASSERT(AIRPORT_ITLWM_SAE_RELAY_ALIGNOF(
    struct AirportItlwmSaeAuthReplyV1) == 8, "SAE reply ABI alignment");

AIRPORT_ITLWM_SAE_RELAY_STATIC_ASSERT(sizeof(struct AirportItlwmSaeCompletionV1) == 144,
    "SAE completion ABI size");
AIRPORT_ITLWM_SAE_RELAY_ASSERT_OFFSET(AirportItlwmSaeCompletionV1, version, 0);
AIRPORT_ITLWM_SAE_RELAY_ASSERT_OFFSET(AirportItlwmSaeCompletionV1, size, 4);
AIRPORT_ITLWM_SAE_RELAY_ASSERT_OFFSET(AirportItlwmSaeCompletionV1,
    generation, 8);
AIRPORT_ITLWM_SAE_RELAY_ASSERT_OFFSET(AirportItlwmSaeCompletionV1,
    association_epoch, 16);
AIRPORT_ITLWM_SAE_RELAY_ASSERT_OFFSET(AirportItlwmSaeCompletionV1,
    event_sequence, 24);
AIRPORT_ITLWM_SAE_RELAY_ASSERT_OFFSET(AirportItlwmSaeCompletionV1,
    controller_nonce, 32);
AIRPORT_ITLWM_SAE_RELAY_ASSERT_OFFSET(AirportItlwmSaeCompletionV1,
    client_cookie, 48);
AIRPORT_ITLWM_SAE_RELAY_ASSERT_OFFSET(AirportItlwmSaeCompletionV1,
    bssid, 64);
AIRPORT_ITLWM_SAE_RELAY_ASSERT_OFFSET(AirportItlwmSaeCompletionV1, sta, 70);
AIRPORT_ITLWM_SAE_RELAY_ASSERT_OFFSET(AirportItlwmSaeCompletionV1,
    reserved0, 76);
AIRPORT_ITLWM_SAE_RELAY_ASSERT_OFFSET(AirportItlwmSaeCompletionV1, pmk, 80);
AIRPORT_ITLWM_SAE_RELAY_ASSERT_OFFSET(AirportItlwmSaeCompletionV1, pmkid, 112);
AIRPORT_ITLWM_SAE_RELAY_ASSERT_OFFSET(AirportItlwmSaeCompletionV1,
    reserved, 128);
AIRPORT_ITLWM_SAE_RELAY_ASSERT_TERMINAL(AirportItlwmSaeCompletionV1, reserved);
AIRPORT_ITLWM_SAE_RELAY_STATIC_ASSERT(AIRPORT_ITLWM_SAE_RELAY_ALIGNOF(
    struct AirportItlwmSaeCompletionV1) == 8, "SAE completion ABI alignment");

AIRPORT_ITLWM_SAE_RELAY_STATIC_ASSERT(sizeof(struct AirportItlwmSaeAbortV1) == 96,
    "SAE abort ABI size");
AIRPORT_ITLWM_SAE_RELAY_ASSERT_OFFSET(AirportItlwmSaeAbortV1, version, 0);
AIRPORT_ITLWM_SAE_RELAY_ASSERT_OFFSET(AirportItlwmSaeAbortV1, size, 4);
AIRPORT_ITLWM_SAE_RELAY_ASSERT_OFFSET(AirportItlwmSaeAbortV1, reason, 8);
AIRPORT_ITLWM_SAE_RELAY_ASSERT_OFFSET(AirportItlwmSaeAbortV1, reserved0, 12);
AIRPORT_ITLWM_SAE_RELAY_ASSERT_OFFSET(AirportItlwmSaeAbortV1, generation, 16);
AIRPORT_ITLWM_SAE_RELAY_ASSERT_OFFSET(AirportItlwmSaeAbortV1,
    association_epoch, 24);
AIRPORT_ITLWM_SAE_RELAY_ASSERT_OFFSET(AirportItlwmSaeAbortV1,
    event_sequence, 32);
AIRPORT_ITLWM_SAE_RELAY_ASSERT_OFFSET(AirportItlwmSaeAbortV1,
    controller_nonce, 40);
AIRPORT_ITLWM_SAE_RELAY_ASSERT_OFFSET(AirportItlwmSaeAbortV1,
    client_cookie, 56);
AIRPORT_ITLWM_SAE_RELAY_ASSERT_OFFSET(AirportItlwmSaeAbortV1, bssid, 72);
AIRPORT_ITLWM_SAE_RELAY_ASSERT_OFFSET(AirportItlwmSaeAbortV1, sta, 78);
AIRPORT_ITLWM_SAE_RELAY_ASSERT_OFFSET(AirportItlwmSaeAbortV1, reserved, 84);
AIRPORT_ITLWM_SAE_RELAY_ASSERT_TERMINAL(AirportItlwmSaeAbortV1, reserved);
AIRPORT_ITLWM_SAE_RELAY_STATIC_ASSERT(AIRPORT_ITLWM_SAE_RELAY_ALIGNOF(
    struct AirportItlwmSaeAbortV1) == 8, "SAE abort ABI alignment");

#undef AIRPORT_ITLWM_SAE_RELAY_ASSERT_TERMINAL
#undef AIRPORT_ITLWM_SAE_RELAY_ASSERT_OFFSET
#undef AIRPORT_ITLWM_SAE_RELAY_STATIC_ASSERT
#undef AIRPORT_ITLWM_SAE_RELAY_ALIGNOF

#endif /* AIRPORT_ITLWM_SAE_RELAY_V1_H */
