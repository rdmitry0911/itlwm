/*
 * Bounded STA PMF liveness procedure. All times are monotonic microseconds.
 * This value owns no node, timer, packet, credential, or callback. The caller
 * serializes it with the selected-BSS leaf and validates the current peer.
 * Policy follows hostap wpa_supplicant/sme.c: 201 TU retries, 1000 TU maximum,
 * and at most one procedure per ten seconds. A response to ANY submitted
 * transaction is valid; AP-initiated query IDs have a separate owner.
 */
#ifndef _NET80211_IEEE80211_STA_SA_QUERY_H_
#define _NET80211_IEEE80211_STA_SA_QUERY_H_

#include <stdint.h>
#include <string.h>

#define IEEE80211_STA_SA_QUERY_RETRY_US (201ULL * 1024)
#define IEEE80211_STA_SA_QUERY_MAX_US (1000ULL * 1024)
#define IEEE80211_STA_SA_QUERY_RATE_US 10000000ULL
#define IEEE80211_STA_SA_QUERY_IDS 5

enum ieee80211_sta_sa_query_phase {
    IEEE80211_STA_SA_QUERY_IDLE,
    IEEE80211_STA_SA_QUERY_WAIT,
    IEEE80211_STA_SA_QUERY_EXPIRED,
    IEEE80211_STA_SA_QUERY_PUBLISHED
};

struct ieee80211_sta_sa_query_token {
    uint64_t generation;
    uint64_t epoch;
    uint8_t bssid[6];
    uint8_t sta[6];
    uint16_t transaction;
    uint8_t slot;
    uint8_t reserved;
};

struct ieee80211_sta_sa_query {
    uint64_t next_generation;
    uint64_t last_start_us;
    uint64_t started_us;
    uint64_t next_tx_us;
    struct ieee80211_sta_sa_query_token owner;
    uint16_t transactions[IEEE80211_STA_SA_QUERY_IDS];
    uint8_t phase;
    uint8_t count;
    uint8_t submitted;
    uint8_t rate_valid;
};

static inline void
ieee80211_sta_sa_query_cancel_value(struct ieee80211_sta_sa_query *query)
{
    const uint64_t next = query->next_generation;
    const uint64_t last = query->last_start_us;
    const uint8_t rate_valid = query->rate_valid;
    memset(query, 0, sizeof(*query));
    query->next_generation = next;
    query->last_start_us = last;
    query->rate_valid = rate_valid;
}

static inline int
ieee80211_sta_sa_query_token_equal(
    const struct ieee80211_sta_sa_query_token *a,
    const struct ieee80211_sta_sa_query_token *b)
{
    return a->generation != 0 && a->generation == b->generation &&
        a->epoch != 0 && a->epoch == b->epoch &&
        memcmp(a->bssid, b->bssid, sizeof(a->bssid)) == 0 &&
        memcmp(a->sta, b->sta, sizeof(a->sta)) == 0;
}

static inline int
ieee80211_sta_sa_query_begin_value(struct ieee80211_sta_sa_query *query,
    uint64_t now, uint64_t epoch, const uint8_t *bssid, const uint8_t *sta)
{
    if (query->phase != IEEE80211_STA_SA_QUERY_IDLE || epoch == 0 ||
        query->next_generation == UINT64_MAX || bssid == NULL || sta == NULL ||
        (bssid[0] & 1) != 0 || (sta[0] & 1) != 0 ||
        (query->rate_valid && (now < query->last_start_us ||
        now - query->last_start_us < IEEE80211_STA_SA_QUERY_RATE_US)))
        return 0;
    query->owner.generation = ++query->next_generation;
    query->owner.epoch = epoch;
    memcpy(query->owner.bssid, bssid, sizeof(query->owner.bssid));
    memcpy(query->owner.sta, sta, sizeof(query->owner.sta));
    query->phase = IEEE80211_STA_SA_QUERY_WAIT;
    query->last_start_us = now;
    query->rate_valid = 1;
    query->next_tx_us = now;
    return 1;
}

/* Return 1 for one prepared request, -1 for a qualified timeout, and zero
 * for no action. next_us is a delay, not an absolute clock deadline. An old
 * timer can only advance the CURRENT procedure when its deadline is due. */
static inline int
ieee80211_sta_sa_query_poll_value(struct ieee80211_sta_sa_query *query,
    uint64_t now, uint16_t random_id,
    struct ieee80211_sta_sa_query_token *token, uint64_t *next_us)
{
    unsigned int i;
    uint64_t elapsed;
    memset(token, 0, sizeof(*token));
    *next_us = 0;
    if (query->phase != IEEE80211_STA_SA_QUERY_WAIT)
        return 0;
    if (query->count != 0) {
        if (now < query->started_us) {
            ieee80211_sta_sa_query_cancel_value(query);
            return 0;
        }
        elapsed = now - query->started_us;
        if (elapsed >= IEEE80211_STA_SA_QUERY_MAX_US) {
            if (query->submitted == 0) {
                /* A wholly local allocation/admission failure is not proof
                 * that the protected association failed a wire challenge. */
                ieee80211_sta_sa_query_cancel_value(query);
                return 0;
            }
            query->phase = IEEE80211_STA_SA_QUERY_EXPIRED;
            *token = query->owner;
            return -1;
        }
        *next_us = IEEE80211_STA_SA_QUERY_MAX_US - elapsed;
    }
    if (now < query->next_tx_us) {
        const uint64_t until_tx = query->next_tx_us - now;
        if (*next_us == 0 || until_tx < *next_us)
            *next_us = until_tx;
        return 0;
    }
    if (query->count >= IEEE80211_STA_SA_QUERY_IDS)
        return 0;
    if (query->count == 0) {
        query->started_us = now;
        *next_us = IEEE80211_STA_SA_QUERY_MAX_US;
    }
    /* Keep all five IDs distinct without a retrying random-source loop. */
    for (i = 0; i < query->count; ++i) {
        if (query->transactions[i] == random_id) {
            ++random_id;
            i = (unsigned int)-1;
        }
    }
    *token = query->owner;
    token->slot = query->count;
    token->transaction = random_id;
    query->transactions[query->count++] = random_id;
    if (now > UINT64_MAX - IEEE80211_STA_SA_QUERY_RETRY_US) {
        ieee80211_sta_sa_query_cancel_value(query);
        memset(token, 0, sizeof(*token));
        *next_us = 0;
        return 0;
    }
    query->next_tx_us = now + IEEE80211_STA_SA_QUERY_RETRY_US;
    if (IEEE80211_STA_SA_QUERY_RETRY_US < *next_us)
        *next_us = IEEE80211_STA_SA_QUERY_RETRY_US;
    return 1;
}

static inline int
ieee80211_sta_sa_query_tx_value(struct ieee80211_sta_sa_query *query,
    const struct ieee80211_sta_sa_query_token *token, int commit)
{
    if (query->phase != IEEE80211_STA_SA_QUERY_WAIT ||
        !ieee80211_sta_sa_query_token_equal(&query->owner, token) ||
        token->slot >= query->count ||
        query->transactions[token->slot] != token->transaction ||
        (query->submitted & (1U << token->slot)) != 0)
        return 0;
    if (commit)
        query->submitted |= (uint8_t)(1U << token->slot);
    return 1;
}

static inline int
ieee80211_sta_sa_query_response_value(struct ieee80211_sta_sa_query *query,
    uint64_t epoch, const uint8_t *bssid, const uint8_t *sta, uint16_t transaction)
{
    unsigned int i;
    if (query->phase != IEEE80211_STA_SA_QUERY_WAIT ||
        query->owner.epoch != epoch ||
        memcmp(query->owner.bssid, bssid, sizeof(query->owner.bssid)) != 0 ||
        memcmp(query->owner.sta, sta, sizeof(query->owner.sta)) != 0)
        return 0;
    for (i = 0; i < query->count; ++i) {
        if ((query->submitted & (1U << i)) != 0 &&
            query->transactions[i] == transaction) {
            ieee80211_sta_sa_query_cancel_value(query);
            return 1;
        }
    }
    return 0;
}

static inline int
ieee80211_sta_sa_query_failure_value(struct ieee80211_sta_sa_query *query,
    const struct ieee80211_sta_sa_query_token *token)
{
    if (query->phase != IEEE80211_STA_SA_QUERY_EXPIRED ||
        !ieee80211_sta_sa_query_token_equal(&query->owner, token))
        return 0;
    query->phase = IEEE80211_STA_SA_QUERY_PUBLISHED;
    return 1;
}

#endif
