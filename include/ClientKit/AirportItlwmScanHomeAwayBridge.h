#ifndef AirportItlwmScanHomeAwayBridge_h
#define AirportItlwmScanHomeAwayBridge_h

#include <stdint.h>
#ifndef __cplusplus
#include <stdbool.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * System-wide Tahoe WCL associated-scan policy bridge.
 *
 * Apple exposes scan_home_away_time as persistent adapter policy.  Intel
 * places the corresponding values in each scan command, so the public setter
 * records one scalar here and IWN/IWM/IWX query it when they build a command.
 * This deliberately avoids changing the shared ieee80211com ABI layout.
 */
void airportItlwmSetScanHomeAwayTime(uint32_t milliseconds);
bool airportItlwmGetScanHomeAwayTime(uint32_t *milliseconds);

#ifdef __cplusplus
}
#endif

#endif /* AirportItlwmScanHomeAwayBridge_h */
