/*
 * Bounded station-side Association Comeback Time parsing.
 *
 * IEEE 802.11 status 30 asks a station to retry association after the
 * Timeout Interval element's Association Comeback Time.  Keep the wire
 * parser independent from net80211 state so malformed or hostile responses
 * can be unit-tested without a kernel environment.
 */
#ifndef _NET80211_IEEE80211_ASSOC_COMEBACK_H_
#define _NET80211_IEEE80211_ASSOC_COMEBACK_H_

#include <stddef.h>
#include <stdint.h>

#define IEEE80211_ASSOC_COMEBACK_ELEMID			56u
#define IEEE80211_ASSOC_COMEBACK_TIMEOUT_TYPE		3u
#define IEEE80211_ASSOC_COMEBACK_TIMEOUT_PAYLOAD_LEN	5u
#define IEEE80211_ASSOC_COMEBACK_MAX_RETRIES		3u
#define IEEE80211_ASSOC_COMEBACK_MAX_WAIT_SECONDS	30u

struct ieee80211_assoc_comeback_plan {
	uint32_t timeout_tu;
	uint32_t timeout_seconds;
};

static inline int
ieee80211_assoc_comeback_parse(const uint8_t *ies, size_t ies_len,
    struct ieee80211_assoc_comeback_plan *plan)
{
	uint64_t timeout_us;
	uint32_t timeout_tu;
	uint32_t timeout_seconds;
	size_t offset = 0;
	int found = 0;

	if (ies == NULL || plan == NULL)
		return 0;

	plan->timeout_tu = 0;
	plan->timeout_seconds = 0;
	while (offset + 2 <= ies_len) {
		const uint8_t elemid = ies[offset];
		const size_t payload_len = ies[offset + 1];
		const uint8_t *payload;

		offset += 2;
		if (payload_len > ies_len - offset)
			return 0;
		payload = ies + offset;
		offset += payload_len;

		if (elemid != IEEE80211_ASSOC_COMEBACK_ELEMID)
			continue;
		if (payload_len != IEEE80211_ASSOC_COMEBACK_TIMEOUT_PAYLOAD_LEN)
			return 0;
		if (payload[0] != IEEE80211_ASSOC_COMEBACK_TIMEOUT_TYPE)
			continue;
		if (found)
			return 0;

		timeout_tu = (uint32_t)payload[1] |
		    ((uint32_t)payload[2] << 8) |
		    ((uint32_t)payload[3] << 16) |
		    ((uint32_t)payload[4] << 24);
		if (timeout_tu == 0)
			return 0;
		timeout_us = (uint64_t)timeout_tu * 1024u;
		timeout_seconds = (uint32_t)((timeout_us + 999999u) / 1000000u);
		if (timeout_seconds == 0 ||
		    timeout_seconds > IEEE80211_ASSOC_COMEBACK_MAX_WAIT_SECONDS)
			return 0;

		plan->timeout_tu = timeout_tu;
		plan->timeout_seconds = timeout_seconds;
		found = 1;
	}

	/* A trailing byte is a truncated IE header. */
	if (offset != ies_len)
		return 0;
	return found;
}

#endif /* _NET80211_IEEE80211_ASSOC_COMEBACK_H_ */
