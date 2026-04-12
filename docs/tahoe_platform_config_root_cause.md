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

## Next Confirmed Divergence After PLATFORM_CONFIG

After fixing `APPLE80211_IOC_PLATFORM_CONFIG`, Tahoe bring-up moved forward:

- live logs switched to `APPLE80211_IOC_PLATFORM_CONFIG -> 0x0`
- WCL started issuing scan requests
- the next early mandatory IOC mismatch became `APPLE80211_IOC_POWERSAVE`

Observed live sequence on `AirportItlwm build=0dd2d3c`:

- `[wcl] setPOWERSAVE@1551: arg->powersave_level = 7 not supported`
- `APPLE80211_IOC_POWERSAVE -> 0xe00002c7`

This matters because the failure happens before the later watchdog-driven
`APPLE80211_IOC_WCL_TRIGGER_CC` path, so `setWCL_TRIGGER_CC` is not the first
bring-up divergence after the platform-config fix.

## Tahoe POWERSAVE Contract

Recovered Apple behavior from the reference decompiles:

- `AppleBCMWLANCore::getPOWERSAVE(apple80211_powersave_data *)` returns success
- it reads back a cached 32-bit power-save level from core state
- Tahoe WCL sets power-save level `7`
  (`APPLE80211_POWERSAVE_MODE_MAX_THROUGHPUT`) during early bring-up and
  expects this IOC to succeed

That means Tahoe does not treat `POWERSAVE` as an optional or unsupported IOC
in the minimal boot path.

## POWERSAVE Root Cause

Our Tahoe Skywalk vtable still returned `kIOReturnUnsupported` from
`setPOWERSAVE(...)`, while `getPOWERSAVE(...)` always synthesized
`APPLE80211_POWERSAVE_MODE_DISABLED`.

That diverged from the Apple contract in two ways:

1. upper layers could not apply the requested WCL startup policy
2. subsequent reads could never reflect the last accepted level

## POWERSAVE Fix Direction

The minimal `1:1`-compatible behavior here is:

- accept `setPOWERSAVE(...)` instead of returning unsupported
- cache the requested level
- return the same cached level from `getPOWERSAVE(...)`

The current code now does exactly that, with comments tied back to the Tahoe
live logs and the recovered Apple `getPOWERSAVE` behavior.

## Next Confirmed Divergence After POWERSAVE

Once the Tahoe `POWERSAVE` contract was fixed in source, the next early
Apple80211 mismatch remained in the Tahoe BSD ioctl bridge:

- `APPLE80211_IOC_VIRTUAL_IF_ROLE (96)`
- `APPLE80211_IOC_VIRTUAL_IF_PARENT (97)`

Live logs on the older loaded build still showed:

- `APPLE80211_IOC_VIRTUAL_IF_ROLE -> 6`
- `APPLE80211_IOC_VIRTUAL_IF_PARENT -> 6`

Those requests arrive during `_initInterface`, before the user-visible scan
path settles, so they must follow the Apple contract even on a plain
infrastructure interface.

## Tahoe VIRTUAL_IF_* Contract

The reverse docs in `84_debugging_playbooks.yaml` and
`85_interface_lifecycle_and_bsd_attach.yaml` record the Apple expectation for a
non-virtual interface:

- `APPLE80211_IOC_VIRTUAL_IF_ROLE (96)` -> `-3903` / `0xe082280e` is expected
- `APPLE80211_IOC_VIRTUAL_IF_PARENT (97)` -> `-3903` / `0xe082280e` is expected

That is important because these are explicitly *safe-to-fail* Apple80211 IOCTLs
with a specific Apple failure code.  Returning a raw POSIX-style `6` diverges
from the framework contract and is called out in the reverse docs as the wrong
shape of failure.

## VIRTUAL_IF_* Root Cause

Our Tahoe-specific `processApple80211Ioctl()` bridge did not cover either IOC
at all.  As a result, the requests escaped our reconstructed Tahoe cache/fallback
layer and fell through to a different path that surfaced `6` instead of the
Apple-specific `0xe082280e`.

So the bug here is not "missing virtual interface support".  The bug is failing
to reproduce the correct non-virtual-interface failure contract.

## VIRTUAL_IF_* Fix Direction

The Tahoe BSD bridge must intercept both requests and return:

- `0xe082280e` for `APPLE80211_IOC_VIRTUAL_IF_ROLE`
- `0xe082280e` for `APPLE80211_IOC_VIRTUAL_IF_PARENT`

That matches the documented Apple behavior for a regular infrastructure
interface and keeps `_initInterface` on the same contract as the reference
stack.

## VIRTUAL_IF_* Live Root Cause Correction

The earlier fix direction above was still incomplete in one important way.

Live logs on build `853f68e` continued to show:

- `APPLE80211_IOC_VIRTUAL_IF_ROLE -> 6`
- `APPLE80211_IOC_VIRTUAL_IF_PARENT -> 6`

even though the Tahoe BSD bridge source already contained explicit cases for
both IOCs.

That meant the bug was no longer "missing switch cases".  The bug was that the
bridge rejected these requests before it ever reached those cases.

## Why The Earlier Bridge Fix Never Fired

Our Tahoe helper started with:

- `if (req == NULL || req->req_data == NULL) return kIOReturnUnsupported;`

But the reference contract for `VIRTUAL_IF_ROLE` / `VIRTUAL_IF_PARENT` does not
depend on a caller-supplied payload.  The Apple wrapper for a normal
infrastructure interface returns the constant Apple-specific "not a virtual
interface" code directly, which is consistent with the reverse docs that record
these as safe-to-fail IOCs.

So on Tahoe, these requests can legitimately arrive with no useful payload
buffer.  Our early `req_data == NULL` guard therefore forced them back into the
framework fallback path, which is exactly where the live logs showed the raw
POSIX-style `6`.

## Correct Tahoe Fix Direction

For these two payload-less IOCs, the Tahoe BSD bridge must:

1. intercept them before any generic `req_data == NULL` rejection
2. return `0xe082280e` directly

Only after those explicit payload-less cases are handled is it correct to apply
the generic `req_data != NULL` requirement for the remaining IOCs that do carry
typed request buffers.

## Next Confirmed Divergence After VIRTUAL_IF_*

On live build `b4e9aa8`, the early `VIRTUAL_IF_ROLE/PARENT -> 6` failures no
longer appear.  But Tahoe still does not leave the unavailable state:

- `isDriverAvailable` remains `0`
- external `SSID/BSSID` still fail with `0xe0822403`
- external `SCAN_RESULT` now fails immediately after every `fakeScanDone`
  with `0xe0820445`

At the same time, our own driver logs prove that the underlying scan path is
alive:

- `setWCL_SCAN_REQ -> scan triggered OK`
- `fakeScanDone ... nodes=28 posting SCAN_DONE + BGSCAN_CACHED`

So the next root cause is not "scan never ran".  The next root cause is that
the framework still does not accept the driver as available after scan
completion.

## DRIVER_AVAILABLE Transport Root Cause

The remaining producer-side delta is in how we publish
`APPLE80211_M_DRIVER_AVAILABLE`.

Reference material in `92_postoffice_event_delivery_chain.yaml` and
`86_concrete_event_payload_maps_checked.yaml` shows the Apple event path using
the controller-level producer call:

- `IO80211Controller::postMessage(controller, iface, code, payload, len, true)`

That route goes through controller `PostOffice` and the same event-delivery
machinery used by Apple scan-complete delivery.

Our Tahoe code was still different in exactly one place:

- `SCAN_DONE`, `BGSCAN_CACHED`, `POWER_CHANGED`, and other working events use
  controller `postMessage(...)`
- `DRIVER_AVAILABLE` alone used direct
  `IO80211SkywalkInterface::postMessage(...)`

That means the availability bulletin was bypassing the same producer route used
by the reference stack and by our already-working scan-complete path.

Given the live state (`isDriverAvailable` never flips despite repeated scans)
and the recovered controller/PostOffice producer contract, the correct next
fix is to publish `APPLE80211_M_DRIVER_AVAILABLE` through controller
`postMessage(fNetIf, ...)`, not through direct interface `postMessage(...)`.
