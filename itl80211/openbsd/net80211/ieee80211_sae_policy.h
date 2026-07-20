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
#define IEEE80211_SAE_SCAN_RSNXE_PRESENT		0x00000001u
#define IEEE80211_SAE_SCAN_RSNXE_H2E			0x00000002u
#define IEEE80211_SAE_SCAN_UNMODELED			0x00000004u
#define IEEE80211_SAE_SCAN_MALFORMED			0x00000008u
#define IEEE80211_SAE_SCAN_AKM_AMBIGUOUS		0x00000010u
#define IEEE80211_SAE_SCAN_EXTCAP_PRESENT		0x00000020u
#define IEEE80211_SAE_SCAN_EXTCAP_PASSWORD_ID	0x00000040u
#define IEEE80211_SAE_SCAN_EXTCAP_PASSWORD_ID_EXCLUSIVE	0x00000080u
#define IEEE80211_SAE_SCAN_EXTCAP_SAE_PK_EXCLUSIVE	0x00000100u
#define IEEE80211_SAE_SCAN_RSNXE_SAE_PK		0x00000200u
#define IEEE80211_SAE_SCAN_H2E_ONLY_SELECTOR	0x00000400u
#define IEEE80211_SAE_SCAN_SAE_EXT_KEY		0x00000800u
#define IEEE80211_SAE_SCAN_UNSUPPORTED		0x00001000u
#define IEEE80211_SAE_SCAN_PROFILE_INCONSISTENT	0x00002000u

#define IEEE80211_SAE_RSNXE_FIELD_LEN_MASK	0x0fu
#define IEEE80211_SAE_RSNXE_H2E		(1u << 5)
#define IEEE80211_SAE_RSNXE_SAE_PK		(1u << 6)
#define IEEE80211_SAE_RSNXE_KNOWN_FIRST_OCTET_MASK	\
	(IEEE80211_SAE_RSNXE_FIELD_LEN_MASK | IEEE80211_SAE_RSNXE_H2E | \
	 IEEE80211_SAE_RSNXE_SAE_PK)

/* ExtCap bits are LSB-first payload bit positions, never a host-endian u64. */
#define IEEE80211_SAE_EXTCAP_PASSWORD_ID_BIT		81u
#define IEEE80211_SAE_EXTCAP_PASSWORD_ID_EXCLUSIVE_BIT	82u
#define IEEE80211_SAE_EXTCAP_SAE_PK_EXCLUSIVE_BIT	88u

/* Rate membership selector 123 is advertised as basic rate octet 0xfb. */
#define IEEE80211_SAE_RATE_BASIC		0x80u
#define IEEE80211_SAE_RATE_VALUE_MASK	0x7fu
#define IEEE80211_SAE_H2E_ONLY_SELECTOR	123u

/* RSN AKM suite types that require an extended-SAE owner. */
#define IEEE80211_SAE_AKM_EXT_KEY		24u
#define IEEE80211_SAE_AKM_FT_EXT_KEY		25u

/*
 * Convert an RSNXE payload into a fixed, non-wire-format fact set.  The
 * capability-field length is encoded as low-nibble + 1.  A strict exact-size
 * check deliberately rejects extensions this implementation has not modeled;
 * future SAE admission must reject MALFORMED and UNMODELED rather than
 * silently choosing HnP.
 */
static inline uint32_t
ieee80211_sae_scan_rsnxe_flags(const uint8_t *payload, size_t payload_len)
{
	uint32_t flags = 0;
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
	if ((payload[0] & IEEE80211_SAE_RSNXE_SAE_PK) != 0)
		flags |= IEEE80211_SAE_SCAN_RSNXE_SAE_PK |
		    IEEE80211_SAE_SCAN_UNSUPPORTED;

	unknown = payload[0] &
	    (uint8_t)~IEEE80211_SAE_RSNXE_KNOWN_FIRST_OCTET_MASK;
	for (index = 1; index < payload_len; index++)
		unknown |= payload[index];
	if (unknown != 0)
		flags |= IEEE80211_SAE_SCAN_UNMODELED;

	return flags;
}

static inline int
ieee80211_sae_scan_extcap_bit_is_set(const uint8_t *payload,
    size_t payload_len, size_t bit)
{
	size_t byte = bit / 8;

	return payload != NULL && payload_len > byte &&
	    (payload[byte] & (uint8_t)(1u << (bit % 8))) != 0;
}

/*
 * Preserve only the three SAE-related ExtCap facts.  Other ExtCap bits are
 * independent capabilities, so their presence must not make this narrow SAE
 * census reject a BSS.  A zero-length ExtCap is syntactically valid and means
 * that none of these three facts was advertised.
 */
static inline uint32_t
ieee80211_sae_scan_extcap_flags(const uint8_t *payload, size_t payload_len)
{
	uint32_t flags;

	if (payload == NULL)
		return 0;
	flags = IEEE80211_SAE_SCAN_EXTCAP_PRESENT;
	if (ieee80211_sae_scan_extcap_bit_is_set(payload, payload_len,
	    IEEE80211_SAE_EXTCAP_PASSWORD_ID_BIT))
		flags |= IEEE80211_SAE_SCAN_EXTCAP_PASSWORD_ID;
	if (ieee80211_sae_scan_extcap_bit_is_set(payload, payload_len,
	    IEEE80211_SAE_EXTCAP_PASSWORD_ID_EXCLUSIVE_BIT))
		flags |= IEEE80211_SAE_SCAN_EXTCAP_PASSWORD_ID_EXCLUSIVE;
	if (ieee80211_sae_scan_extcap_bit_is_set(payload, payload_len,
	    IEEE80211_SAE_EXTCAP_SAE_PK_EXCLUSIVE_BIT))
		flags |= IEEE80211_SAE_SCAN_EXTCAP_SAE_PK_EXCLUSIVE;
	return flags;
}

/* Read the wire rate octets before ieee80211_setup_rates() sorts/mutates them. */
static inline int
ieee80211_sae_scan_has_h2e_only_selector(const uint8_t *rates,
    size_t rates_len)
{
	size_t index;

	if (rates == NULL)
		return 0;
	for (index = 0; index < rates_len; index++) {
		if ((rates[index] & IEEE80211_SAE_RATE_BASIC) != 0 &&
		    (rates[index] & IEEE80211_SAE_RATE_VALUE_MASK) ==
		    IEEE80211_SAE_H2E_ONLY_SELECTOR)
			return 1;
	}
	return 0;
}

/* Neither extended-SAE suite is SAE-PK or an ordinary SAE credential path. */
static inline int
ieee80211_sae_scan_is_extended_key_akm(uint8_t suite_type)
{
	return suite_type == IEEE80211_SAE_AKM_EXT_KEY ||
	    suite_type == IEEE80211_SAE_AKM_FT_EXT_KEY;
}

/*
 * Resolve cross-IE claims only after RSNXE, ExtCap, and Rates/XRates have
 * been normalized.  These flags are evidence for a future owner; they never
 * select HnP/H2E, enable SAE-PK, or alter legacy association behavior.
 */
static inline uint32_t
ieee80211_sae_scan_finalize_flags(uint32_t flags)
{
	if ((flags & IEEE80211_SAE_SCAN_EXTCAP_PASSWORD_ID_EXCLUSIVE) != 0 &&
	    (flags & IEEE80211_SAE_SCAN_EXTCAP_PASSWORD_ID) == 0)
		flags |= IEEE80211_SAE_SCAN_PROFILE_INCONSISTENT;
	if ((flags & IEEE80211_SAE_SCAN_EXTCAP_SAE_PK_EXCLUSIVE) != 0 &&
	    (flags & IEEE80211_SAE_SCAN_RSNXE_SAE_PK) == 0)
		flags |= IEEE80211_SAE_SCAN_PROFILE_INCONSISTENT;
	if ((flags & IEEE80211_SAE_SCAN_H2E_ONLY_SELECTOR) != 0 &&
	    (flags & IEEE80211_SAE_SCAN_RSNXE_H2E) == 0)
		flags |= IEEE80211_SAE_SCAN_PROFILE_INCONSISTENT;
	if ((flags & IEEE80211_SAE_SCAN_SAE_EXT_KEY) != 0)
		flags |= IEEE80211_SAE_SCAN_UNSUPPORTED;
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
