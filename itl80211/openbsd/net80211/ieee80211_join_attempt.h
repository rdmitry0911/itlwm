/*
 * Request-scoped join results. Callers serialize this value with the selected
 * BSS leaf lock; it never owns a node, packet, callback, or credential.
 */
#ifndef _NET80211_IEEE80211_JOIN_ATTEMPT_H_
#define _NET80211_IEEE80211_JOIN_ATTEMPT_H_

#include <stdint.h>
#include <string.h>

enum ieee80211_join_phase {
    IEEE80211_JOIN_IDLE,
    IEEE80211_JOIN_DISCOVERY,
    IEEE80211_JOIN_AUTH,
    IEEE80211_JOIN_ASSOC,
    IEEE80211_JOIN_KEYS,
    IEEE80211_JOIN_FAILING,
    IEEE80211_JOIN_FAILURE_READY,
    IEEE80211_JOIN_COMPLETE
};

enum ieee80211_join_failure_cause {
    IEEE80211_JOIN_FAILURE_NONE,
    IEEE80211_JOIN_FAILURE_TIMEOUT,
    IEEE80211_JOIN_FAILURE_PEER_STATUS,
    IEEE80211_JOIN_FAILURE_NO_ACK,
    IEEE80211_JOIN_FAILURE_LOCAL,
    IEEE80211_JOIN_FAILURE_NO_NETWORKS,
    IEEE80211_JOIN_FAILURE_PEER_REASON
};

/* Distinct retirement owners, not a timeout or reference count. A bit may
 * only be cleared by the matching request's real cleanup terminal. */
#define IEEE80211_JOIN_CLEANUP_PRODUCER 0x01U
#define IEEE80211_JOIN_CLEANUP_LOWER    0x02U
#define IEEE80211_JOIN_CLEANUP_SAE      0x04U
#define IEEE80211_JOIN_CLEANUP_ALL      0x07U

struct ieee80211_join_phase_result {
    uint8_t seen;
    uint8_t cause;
    uint16_t peer_status;
    uint16_t peer_reason;
    uint32_t local_error;
};

struct ieee80211_join_failure {
    uint64_t generation;
    uint64_t association_epoch;
    uint8_t bssid[6];
    uint8_t ssid[32];
    uint8_t ssid_len;
    uint8_t phase;
    struct ieee80211_join_phase_result auth;
    struct ieee80211_join_phase_result assoc;
    struct ieee80211_join_phase_result terminal;
};

struct ieee80211_join_attempt {
    /* This counter survives cancellation and is never allowed to wrap. */
    uint64_t next_generation;
    struct ieee80211_join_failure result;
    uint8_t phase;
    uint8_t cleanup_pending;
};

static inline int
ieee80211_join_attempt_bssid_valid(const uint8_t *bssid)
{
    unsigned int i;
    uint8_t nonzero = 0;

    if (bssid == NULL || (bssid[0] & 1) != 0)
        return 0;
    for (i = 0; i < 6; i++)
        nonzero |= bssid[i];
    return nonzero != 0;
}

/* Only accepted fresh ingress calls begin. Rejected/duplicate requests must
 * retain their old ledger instead of calling begin and undoing it later. */
static inline uint64_t
ieee80211_join_attempt_begin(struct ieee80211_join_attempt *attempt,
    const uint8_t *bssid, const uint8_t *ssid, unsigned int ssid_len)
{
    struct ieee80211_join_failure result;

    if (attempt == NULL || !ieee80211_join_attempt_bssid_valid(bssid) ||
        ssid == NULL || ssid_len == 0 || ssid_len > sizeof(result.ssid) ||
        attempt->next_generation == UINT64_MAX)
        return 0;
    /* Inputs may point into the old value. Snapshot before replacing it. */
    memset(&result, 0, sizeof(result));
    memcpy(result.bssid, bssid, sizeof(result.bssid));
    memcpy(result.ssid, ssid, ssid_len);
    result.ssid_len = (uint8_t)ssid_len;
    result.generation = ++attempt->next_generation;
    attempt->result = result;
    attempt->phase = IEEE80211_JOIN_DISCOVERY;
    attempt->cleanup_pending = 0;
    return result.generation;
}

static inline int
ieee80211_join_attempt_is_current(const struct ieee80211_join_attempt *attempt,
    uint64_t generation)
{
    return attempt != NULL && generation != 0 &&
        attempt->phase != IEEE80211_JOIN_IDLE &&
        attempt->result.generation == generation;
}

static inline int
ieee80211_join_attempt_bind(struct ieee80211_join_attempt *attempt,
    uint64_t generation, uint64_t epoch, const uint8_t *bssid,
    const uint8_t *ssid, unsigned int ssid_len)
{
    if (!ieee80211_join_attempt_is_current(attempt, generation) ||
        attempt->phase != IEEE80211_JOIN_DISCOVERY || epoch == 0 ||
        bssid == NULL || ssid == NULL ||
        ssid_len != attempt->result.ssid_len ||
        memcmp(bssid, attempt->result.bssid, 6) != 0 ||
        memcmp(ssid, attempt->result.ssid, ssid_len) != 0)
        return 0;
    attempt->result.association_epoch = epoch;
    attempt->phase = IEEE80211_JOIN_AUTH;
    return 1;
}

static inline int
ieee80211_join_attempt_note_success(struct ieee80211_join_attempt *attempt,
    uint64_t generation, uint64_t epoch, enum ieee80211_join_phase phase)
{
    struct ieee80211_join_phase_result *result;

    if (!ieee80211_join_attempt_is_current(attempt, generation) ||
        epoch == 0 || epoch != attempt->result.association_epoch ||
        attempt->phase != phase ||
        (phase != IEEE80211_JOIN_AUTH && phase != IEEE80211_JOIN_ASSOC &&
        phase != IEEE80211_JOIN_KEYS))
        return 0;
    if (phase == IEEE80211_JOIN_KEYS) {
        attempt->phase = IEEE80211_JOIN_COMPLETE;
        return 1;
    }
    result = phase == IEEE80211_JOIN_AUTH ?
        &attempt->result.auth : &attempt->result.assoc;
    memset(result, 0, sizeof(*result));
    result->seen = 1;
    attempt->phase = (uint8_t)(phase + 1);
    return 1;
}

static inline int
ieee80211_join_attempt_fail(struct ieee80211_join_attempt *attempt,
    uint64_t generation, uint64_t epoch, enum ieee80211_join_phase phase,
    enum ieee80211_join_failure_cause cause, uint16_t peer_status,
    uint16_t peer_reason, uint32_t local_error, unsigned int cleanup)
{
    struct ieee80211_join_phase_result result;

    if (!ieee80211_join_attempt_is_current(attempt, generation) ||
        attempt->phase != phase || phase < IEEE80211_JOIN_DISCOVERY ||
        phase > IEEE80211_JOIN_KEYS ||
        epoch != attempt->result.association_epoch ||
        (phase == IEEE80211_JOIN_DISCOVERY ? epoch != 0 : epoch == 0) ||
        cause <= IEEE80211_JOIN_FAILURE_NONE ||
        cause > IEEE80211_JOIN_FAILURE_PEER_REASON || cleanup == 0 ||
        (cleanup & ~IEEE80211_JOIN_CLEANUP_ALL) != 0 ||
        (cause == IEEE80211_JOIN_FAILURE_NO_NETWORKS &&
        phase != IEEE80211_JOIN_DISCOVERY) ||
        (cause == IEEE80211_JOIN_FAILURE_PEER_STATUS &&
        (phase == IEEE80211_JOIN_DISCOVERY || peer_status == 0)) ||
        (cause != IEEE80211_JOIN_FAILURE_PEER_STATUS && peer_status != 0) ||
        (cause == IEEE80211_JOIN_FAILURE_PEER_REASON &&
        phase == IEEE80211_JOIN_DISCOVERY) ||
        (cause != IEEE80211_JOIN_FAILURE_PEER_REASON && peer_reason != 0) ||
        (cause == IEEE80211_JOIN_FAILURE_LOCAL ? local_error == 0 :
        local_error != 0))
        return 0;
    memset(&result, 0, sizeof(result));
    result.seen = 1;
    result.cause = (uint8_t)cause;
    result.peer_status = peer_status;
    result.peer_reason = peer_reason;
    result.local_error = local_error;
    /* A deauth/disassoc indication terminates the join independently of
     * AUTH/ASSOC responses. Preserve only facts those phases really saw. */
    if (phase == IEEE80211_JOIN_AUTH && cause != IEEE80211_JOIN_FAILURE_PEER_REASON)
        attempt->result.auth = result;
    else if (phase == IEEE80211_JOIN_ASSOC && cause != IEEE80211_JOIN_FAILURE_PEER_REASON)
        attempt->result.assoc = result;
    attempt->result.phase = (uint8_t)phase;
    attempt->result.terminal = result;
    attempt->cleanup_pending = (uint8_t)cleanup;
    attempt->phase = IEEE80211_JOIN_FAILING;
    return 1;
}

static inline int
ieee80211_join_attempt_cleanup_done(struct ieee80211_join_attempt *attempt,
    uint64_t generation, unsigned int participant)
{
    if (!ieee80211_join_attempt_is_current(attempt, generation) ||
        attempt->phase != IEEE80211_JOIN_FAILING || participant == 0 ||
        (participant & (participant - 1)) != 0 ||
        (participant & attempt->cleanup_pending) == 0)
        return 0;
    attempt->cleanup_pending &= (uint8_t)~participant;
    if (attempt->cleanup_pending == 0)
        attempt->phase = IEEE80211_JOIN_FAILURE_READY;
    return 1;
}

/* Claim before dispatch. A new accepted request invalidates this immutable
 * snapshot's generation even when it names the same BSSID and SSID. */
static inline int
ieee80211_join_attempt_take_failure(struct ieee80211_join_attempt *attempt,
    uint64_t generation, struct ieee80211_join_failure *failure)
{
    if (!ieee80211_join_attempt_is_current(attempt, generation) ||
        failure == NULL || attempt->phase != IEEE80211_JOIN_FAILURE_READY)
        return 0;
    *failure = attempt->result;
    attempt->phase = IEEE80211_JOIN_COMPLETE;
    return 1;
}

static inline int
ieee80211_join_attempt_cancel(struct ieee80211_join_attempt *attempt,
    uint64_t generation)
{
    if (attempt == NULL || (generation != 0 &&
        !ieee80211_join_attempt_is_current(attempt, generation)))
        return 0;
    memset(&attempt->result, 0, sizeof(attempt->result));
    attempt->phase = IEEE80211_JOIN_IDLE;
    attempt->cleanup_pending = 0;
    return 1;
}

#endif /* _NET80211_IEEE80211_JOIN_ATTEMPT_H_ */
