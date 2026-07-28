#include <assert.h>
#include <stdint.h>

#include "ieee80211_assoc_comeback.h"

int
main(void)
{
	const uint8_t valid[] = {
	    1, 1, 0x82,
	    IEEE80211_ASSOC_COMEBACK_ELEMID, 5,
	    IEEE80211_ASSOC_COMEBACK_TIMEOUT_TYPE, 0xe8, 0x03, 0, 0,
	};
	const uint8_t valid_one_second[] = {
	    IEEE80211_ASSOC_COMEBACK_ELEMID, 5,
	    IEEE80211_ASSOC_COMEBACK_TIMEOUT_TYPE, 1, 0, 0, 0,
	};
	const uint8_t wrong_type[] = {
	    IEEE80211_ASSOC_COMEBACK_ELEMID, 5, 2, 1, 0, 0, 0,
	};
	const uint8_t zero[] = {
	    IEEE80211_ASSOC_COMEBACK_ELEMID, 5,
	    IEEE80211_ASSOC_COMEBACK_TIMEOUT_TYPE, 0, 0, 0, 0,
	};
	const uint8_t too_long[] = {
	    IEEE80211_ASSOC_COMEBACK_ELEMID, 5,
	    IEEE80211_ASSOC_COMEBACK_TIMEOUT_TYPE, 0x71, 0x72, 0, 0,
	};
	const uint8_t bad_length[] = {
	    IEEE80211_ASSOC_COMEBACK_ELEMID, 4,
	    IEEE80211_ASSOC_COMEBACK_TIMEOUT_TYPE, 1, 0, 0,
	};
	const uint8_t truncated[] = {
	    IEEE80211_ASSOC_COMEBACK_ELEMID, 5,
	    IEEE80211_ASSOC_COMEBACK_TIMEOUT_TYPE, 1, 0,
	};
	const uint8_t trailing[] = {
	    IEEE80211_ASSOC_COMEBACK_ELEMID, 5,
	    IEEE80211_ASSOC_COMEBACK_TIMEOUT_TYPE, 1, 0, 0, 0, 1,
	};
	const uint8_t duplicate[] = {
	    IEEE80211_ASSOC_COMEBACK_ELEMID, 5,
	    IEEE80211_ASSOC_COMEBACK_TIMEOUT_TYPE, 1, 0, 0, 0,
	    IEEE80211_ASSOC_COMEBACK_ELEMID, 5,
	    IEEE80211_ASSOC_COMEBACK_TIMEOUT_TYPE, 1, 0, 0, 0,
	};
	struct ieee80211_assoc_comeback_plan plan = { 9, 9 };

	assert(ieee80211_assoc_comeback_parse(valid, sizeof(valid), &plan));
	assert(plan.timeout_tu == 1000);
	assert(plan.timeout_seconds == 2);
	assert(ieee80211_assoc_comeback_parse(valid_one_second,
	    sizeof(valid_one_second), &plan));
	assert(plan.timeout_tu == 1);
	assert(plan.timeout_seconds == 1);
	assert(!ieee80211_assoc_comeback_parse(NULL, 0, &plan));
	assert(!ieee80211_assoc_comeback_parse(valid, sizeof(valid), NULL));
	assert(!ieee80211_assoc_comeback_parse(wrong_type,
	    sizeof(wrong_type), &plan));
	assert(!ieee80211_assoc_comeback_parse(zero, sizeof(zero), &plan));
	assert(!ieee80211_assoc_comeback_parse(too_long,
	    sizeof(too_long), &plan));
	assert(!ieee80211_assoc_comeback_parse(bad_length,
	    sizeof(bad_length), &plan));
	assert(!ieee80211_assoc_comeback_parse(truncated,
	    sizeof(truncated), &plan));
	assert(!ieee80211_assoc_comeback_parse(trailing,
	    sizeof(trailing), &plan));
	assert(!ieee80211_assoc_comeback_parse(duplicate,
	    sizeof(duplicate), &plan));

	return 0;
}
