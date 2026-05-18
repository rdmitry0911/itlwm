/*
 * AirportItlwmAgent — shared kext/helper ABI for the PLTI
 * association-target carrier.
 *
 * This struct is the structureOutput of the
 * kAirportItlwmUserClientMethod_WaitAssociationTarget external
 * method. Its layout MUST match the kext-side definition in
 * AirportItlwm/AirportItlwmV2.hpp; both ends carry a
 * compile-time size assertion to detect drift.
 *
 * Carries no key material — only the (generation, ssid_bytes,
 * bssid, authtype) tuple that names the target the kext just
 * decided to associate with on the PSK external-supplicant path.
 * The helper uses the tuple to (a) look up the matching
 * passphrase in the project-owned keychain at
 * /Library/Keychains/AirportItlwm.keychain (service
 * "AirportItlwm WiFi PSK", account = SSID), (b) derive the
 * 32-byte WPA2 PMK via PBKDF2-SHA1, and (c) deliver it back
 * through kAirportItlwmUserClientMethod_DeliverPMK with the
 * generation echoed as the scalar argument (target-identity
 * replay guard).
 */
#ifndef AIRPORTITLWMAGENT_ASSOC_TARGET_H
#define AIRPORTITLWMAGENT_ASSOC_TARGET_H

#include <stdint.h>

#define kAirportItlwmAssocTargetVersion 1u

#define kAirportItlwmUserClientType                        ('PLTI')

enum {
    kAirportItlwmUserClientMethod_DeliverPMK              = 0,
    kAirportItlwmUserClientMethod_WaitAssociationTarget   = 1,
};

struct AirportItlwmAssociationTarget {
    uint32_t version;            /* = kAirportItlwmAssocTargetVersion */
    uint32_t reserved0;
    uint64_t generation;         /* monotonic; non-zero == pending */
    uint32_t ssid_len;
    uint32_t authtype_lower;
    uint32_t authtype_upper;
    uint32_t reserved1;
    uint8_t  ssid[32];           /* == IEEE80211_NWID_LEN */
    uint8_t  bssid[6];           /* == IEEE80211_ADDR_LEN */
    uint8_t  pad1[2];
} __attribute__((packed));

_Static_assert(sizeof(struct AirportItlwmAssociationTarget) == 72,
               "AirportItlwmAssociationTarget ABI layout must match "
               "AirportItlwm/AirportItlwmV2.hpp");

#endif /* AIRPORTITLWMAGENT_ASSOC_TARGET_H */
