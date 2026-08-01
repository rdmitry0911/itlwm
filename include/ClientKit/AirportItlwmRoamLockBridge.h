#ifndef AirportItlwmRoamLockBridge_h
#define AirportItlwmRoamLockBridge_h

#ifndef __cplusplus
#include <stdbool.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Layout-neutral Tahoe WCL roam-lock policy.
 *
 * Broadcom programs roam_off in firmware.  Intel's equivalent autonomous
 * roam owner lives in shared net80211, where low RSSI schedules a background
 * candidate scan.  Keep the policy outside ieee80211com so IWN/IWM/IWX and
 * every build phase retain one ABI layout.
 */
void airportItlwmSetRoamLocked(bool locked);
bool airportItlwmIsRoamLocked(void);

#ifdef __cplusplus
}
#endif

#endif /* AirportItlwmRoamLockBridge_h */
