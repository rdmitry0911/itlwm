/*
 * Narrow, scan-time SAE facts for a future RSN/SAE admission owner.
 *
 * This header deliberately accepts no credentials, stores no raw information
 * element bytes, and does not enable an AKM.  It only normalizes the RSNXE
 * capability field observed in a beacon or probe response so a later
 * association owner cannot accidentally treat an unparsed IE as HnP.
 */
#ifndef _NET80211_IEEE80211_SAE_POLICY_H_
#define _NET80211_IEEE80211_SAE_POLICY_H_

#include <stddef.h>
#include <stdint.h>

/* Normalized scan facts; none of these flags authorizes SAE association. */
#define IEEE80211_SAE_SCAN_RSNXE_PRESENT	0x01u
#define IEEE80211_SAE_SCAN_RSNXE_H2E		0x02u
#define IEEE80211_SAE_SCAN_UNMODELED		0x04u
#define IEEE80211_SAE_SCAN_MALFORMED		0x08u
#define IEEE80211_SAE_SCAN_AKM_AMBIGUOUS	0x10u

#define IEEE80211_SAE_RSNXE_FIELD_LEN_MASK	0x0fu
#define IEEE80211_SAE_RSNXE_H2E		(1u << 5)
#define IEEE80211_SAE_RSNXE_KNOWN_FIRST_OCTET_MASK	\
	(IEEE80211_SAE_RSNXE_FIELD_LEN_MASK | IEEE80211_SAE_RSNXE_H2E)

/*
 * Convert an RSNXE payload into a fixed, non-wire-format fact set.  The
 * capability-field length is encoded as low-nibble + 1.  A strict exact-size
 * check deliberately rejects extensions this implementation has not modeled;
 * future SAE admission must reject MALFORMED and UNMODELED rather than
 * silently choosing HnP.
 */
static inline uint8_t
ieee80211_sae_scan_rsnxe_flags(const uint8_t *payload, size_t payload_len)
{
	uint8_t flags = 0;
	uint8_t unknown = 0;
	size_t index;
	size_t field_len;

	if (payload == NULL || payload_len == 0 || payload_len > 16)
		return IEEE80211_SAE_SCAN_MALFORMED;

	field_len = (size_t)(payload[0] & IEEE80211_SAE_RSNXE_FIELD_LEN_MASK) + 1;
	if (field_len != payload_len)
		return IEEE80211_SAE_SCAN_MALFORMED;

	flags = IEEE80211_SAE_SCAN_RSNXE_PRESENT;
	if ((payload[0] & IEEE80211_SAE_RSNXE_H2E) != 0)
		flags |= IEEE80211_SAE_SCAN_RSNXE_H2E;

	unknown = payload[0] &
	    (uint8_t)~IEEE80211_SAE_RSNXE_KNOWN_FIRST_OCTET_MASK;
	for (index = 1; index < payload_len; index++)
		unknown |= payload[index];
	if (unknown != 0)
		flags |= IEEE80211_SAE_SCAN_UNMODELED;

	return flags;
}

/*
 * The RSN parser deliberately exposes both the advertised suite count and
 * the count it could classify.  A later pure-SAE owner may admit only one
 * known suite; this helper makes SAE plus an unknown/duplicate suite fail
 * closed without retaining any suite bytes in the node.
 */
static inline int
ieee80211_sae_scan_akm_is_ambiguous(uint16_t advertised_count,
    uint16_t known_count, uint16_t unknown_count)
{
	return advertised_count != 1 || known_count != 1 || unknown_count != 0;
}

#endif /* _NET80211_IEEE80211_SAE_POLICY_H_ */
