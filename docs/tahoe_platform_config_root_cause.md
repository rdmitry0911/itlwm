# Tahoe PLATFORM_CONFIG Root Cause

Date: 2026-04-12

## Symptom

On Tahoe/26.x the driver loaded and appeared in the system, but WCL stayed in
`DRIVER_UNAVAILABLE`, `isDriverAvailable` remained `0`, and Wi-Fi networks did
not appear in the UI.

The earliest reproducible bring-up failure in live logs was:

- `APPLE80211_IOC_PLATFORM_CONFIG -> 0xe00002c7`

That failure happened before the scan/UI path mattered.

## Consumer Side

`IO80211Family` calls `WCLConfigManager::loadPlatformConfig()` during Tahoe
bring-up. If the IOC fails, WCL logs:

- `[wcl] loadPlatformConfig: APPLE80211_IOC_PLATFORM_CONFIG failed`

`WCLDeviceConfiguration::setPlatformConfig(apple80211_platform_config *)`
consumes only the first packed 7 bytes of the payload:

- `u32 @ +0`
- `u16 @ +4`
- `u8  @ +6`

So the Tahoe ABI here is a strict packed 7-byte producer contract.

## Producer Side

The Apple producer is not a generic base-class no-op.

Remote Ghidra recovery on `dima@192.168.40.116` showed the real Apple vendor
path populates the 7-byte payload from platform properties:

- byte 0: `wlan.6GHz.supported`
- byte 1: `wlan.ant-inefficiency-mitigation.enabled`
- byte 2: `wlan.externallypowered`
- byte 3: `wlan.adaptiveroaming.enabled`
- byte 4: `wlan.dfrts` present
- byte 5: `bcom.feature.pmmcast` after loading `wlan.ignore.mcast`
- byte 6: `wlan.ocl.enabled`

Important negative finding:

- the family-side `apple80211getPLATFORM_CONFIG` wrapper is not the producer;
  on the recovered Tahoe path it simply returns `0xe082280e`
- `IO80211Controller::getPLATFORM_CONFIG` base behavior is also not the vendor
  producer we need for WCL bring-up

## Root Cause

Our Tahoe port treated `APPLE80211_IOC_PLATFORM_CONFIG` as if it could be left
unsupported or safely replaced with a zero-only stub.

That was wrong in two different ways:

1. `unsupported` breaks bring-up immediately and keeps WCL in
   `DRIVER_UNAVAILABLE`
2. a blind zeroed payload is still not `1:1` with the Apple producer path,
   because Apple derives the bitmap from IOService properties / cached config

## Fix Direction

`AirportItlwm::getPLATFORM_CONFIG()` must follow the Apple producer contract:

- return a packed 7-byte payload
- populate the same feature bits from controller/provider properties
- leave only genuinely absent properties as zero

This is why the current fix reads Tahoe platform bits from `IOService`
properties instead of returning a fabricated zero-only blob.
