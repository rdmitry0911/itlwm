#ifndef AirportItlwmRegDiagBridge_h
#define AirportItlwmRegDiagBridge_h

#include <stdint.h>

struct ieee80211com;

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Passive bridge from net80211's actual link-state edge into the Tahoe
 * owner-context census.  It must never publish a link state, enter a gate,
 * retain an object, or inspect identifiers/packet data.
 */
void AirportItlwmRegDiagNet80211LinkContext(struct ieee80211com *ic,
                                            uint32_t linkState,
                                            uint64_t assocEpoch);

#ifdef __cplusplus
}
#endif

#endif /* AirportItlwmRegDiagBridge_h */
