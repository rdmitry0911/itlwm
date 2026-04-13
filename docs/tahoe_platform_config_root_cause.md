# Tahoe PLATFORM_CONFIG Root Cause

Date: 2026-04-12

## Bootstrap POWER Root Cause

The live reboot on build `5da9d59` narrowed the remaining Tahoe failure to the
bootstrap `APPLE80211_IOC_POWER` path.

Observed sequence:

- `CoreWiFiDriverReadyKey="true"` is visible in `ioreg`
- hidden `setInterfaceEnable(true)` runs
- `APPLE80211_M_DRIVER_AVAILABLE` is posted
- but an early Tahoe `APPLE80211_IOC_POWER` request with `req=0` is applied
  immediately by the local port
- that immediate OFF edge drives `disableAdapter(...)`
- the local port then posts `DRIVER_UNAVAILABLE`
- `WCLSystemStateManager` moves back to
  `SSM_STATE_DEFER_SENDING_NOTIFICATION`
- `isDriverAvailable` stays `0`

Recovered Apple code shows a different contract:

- `AppleBCMWLANCore::setPOWER(...)` caches the requested state into `+0x289c`
- it sets sticky deferred-power bit `0x1000`
- it does **not** call `handlePowerStateChange(...)` directly on this path
- `AppleBCMWLANCore::setupDriver()` later consumes that cached state and only
  then calls `handlePowerStateChange(cachedState)`

So the local bug is executing bootstrap `setPOWER(...)` too early.

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

## Runtime Status After The Two Latest Fixes

Two important live conclusions are now established and should not be re-opened
later as if they were still hypotheses:

1. After the payload-less `VIRTUAL_IF_*` fix (`b4e9aa8`), the early
   `APPLE80211_IOC_VIRTUAL_IF_ROLE/PARENT -> 6` failures stopped appearing in
   Tahoe bring-up logs.  So that specific `_initInterface` contract mismatch is
   considered closed.

2. The next committed fix (`3325ce7`) moved `APPLE80211_M_DRIVER_AVAILABLE`
   onto the same controller/PostOffice producer route that Apple uses for its
   other notifications and that our own Tahoe `SCAN_DONE` path already uses.

That leaves one clear runtime validation target after `3325ce7`:

- whether `isDriverAvailable` finally leaves `0`
- whether `APPLE80211_IOC_SCAN_RESULT` stops failing with `0xe0820445`
  immediately after `fakeScanDone`

If those two symptoms persist on `3325ce7`, then the remaining Tahoe blocker is
no longer "wrong transport path for DRIVER_AVAILABLE".  At that point the next
root cause must be in the remaining availability contract itself or in a
separate WCL-facing producer path that still diverges from the reference stack.

## Runtime Status After `3325ce7`

Live logs on the first reboot with `AirportItlwm build=3325ce7` closed one
question and opened the next one:

- `APPLE80211_M_DRIVER_AVAILABLE` no longer stands out as the first unresolved
  producer delta worth fixing blindly
- the first new mandatory Tahoe IOC failure is now
  `APPLE80211_IOC_WCL_TRIGGER_CC -> 0xe00002c7`

The relevant live sequence is:

- internal `APPLE80211_IOC_WCL_SCAN_ABORT -> 0x0`
- internal `APPLE80211_IOC_WCL_TRIGGER_CC -> 0xe00002c7`
- later external `APPLE80211_IOC_SCAN_REQ -> 0xe00002bc`
- when a scan does launch, `fakeScanDone` posts successfully but
  `APPLE80211_IOC_SCAN_RESULT` still comes back `0xe0820445`

So at this stage Tahoe is no longer blocked by an obviously missing scan start.
It is blocked by a WCL-side contract mismatch that happens immediately after the
framework enters its internal scan-control path.

## Tahoe `WCL_TRIGGER_CC` Contract

Reference recovery from
`/Volumes/macos-750/Users/bob/Projects/Декомпилы/ghidra_output/AppleBCMWLAN_Core_decompiled.c`
shows the vendor producer path:

- `AppleBCMWLANCore::setWCL_TRIGGER_CC(triggerCC*)`
- `AppleBCMWLANJoinAdapter::triggerCC(triggerCC*)`

The recovered Apple behavior is:

- the producer reads a 32-bit mode from offset `+0x8`
- for mode `1`, it delegates to `JoinAdapter::triggerCC(...)`
- for mode `0`, it copies the first four qwords (`0x20` bytes) of the request
  into scan-adapter-owned state and continues through the scan-adapter helper
  path
- only any other mode returns `0xe00002bc`

Important shape detail:

- both Apple branches cache the same first four qwords before doing any
  adapter-specific work
- this IOC is therefore not optional on Tahoe and must not return
  `kIOReturnUnsupported`

## `WCL_TRIGGER_CC` Root Cause

Our Tahoe skywalk vtable still implemented slot `[599]` as:

- `return kIOReturnUnsupported`

That maps exactly to the live failure:

- `APPLE80211_IOC_WCL_TRIGGER_CC -> 0xe00002c7`

So the current root cause is a direct producer-side contract violation: the
driver rejects a mandatory internal WCL IOC that the Apple reference accepts
for the valid mode values used during Tahoe bring-up.

## `WCL_TRIGGER_CC` Fix Direction

The minimal compatible behavior is:

- accept a non-null `triggerCC` request
- cache the first `0x20` bytes of the request, matching the first Apple action
- return success for mode `0` and mode `1`
- return `0xe00002bc` for any other mode

This reproduces the reference request-shape contract closely enough to remove
the current `unsupported` failure without inventing a fabricated Tahoe-only
policy.

## Runtime Status After `5097f30`

Live logs on `AirportItlwm build=5097f30` close the previous `WCL_TRIGGER_CC`
issue:

- internal `APPLE80211_IOC_WCL_TRIGGER_CC -> 0x0`
- our own driver log confirms `setWCL_TRIGGER_CC mode=0 ...`

So `WCL_TRIGGER_CC` is no longer the open blocker.

The next root cause appears immediately after that fix:

- `WCLScanManager` enters `SCAN_MANAGER_STATE_IN_PROGRESS`
- internal `APPLE80211_IOC_WCL_SCAN_ABORT -> 0x0`
- internal `APPLE80211_IOC_WCL_TRIGGER_CC -> 0x0`
- then WCL moves to `SCAN_MANAGER_STATE_ABORTED`
- every following external `APPLE80211_IOC_SCAN_REQ` fails with
  `0xe00002bc`

That is the exact family ignore-path signature for a scan FSM that never got
its completion edge after an abort.

## Tahoe `SCAN_ABORT_REQ` Contract

The recovered WCL docs already describe the required scan-plane invariant:

- `IN_PROGRESS + SCAN_ABORT_REQ -> ABORTED`
- `ABORTED + SCAN_COMPLETE -> IDLE`

Reference material:

- `83_driver_porting_profiles.yaml`
- `69_WCLScanManager_fully_symbolic_FSM_corrected.yaml`

The practical implication is explicit in the porting profile:

- the backend must support abort with stable completion semantics
- otherwise scan gets stuck in `IN_PROGRESS` or `ABORTED`

Apple's vendor producer matches that contract:

- `AppleBCMWLANCore::setWCL_SCAN_ABORT(void*)` is not a no-op
- it dispatches abort work into scan-adapter-owned machinery instead of merely
  acknowledging the IOC

## `WCL_SCAN_ABORT` Root Cause

Our Tahoe implementation still did only this:

- clear `IEEE80211_F_BGSCAN`
- clear `IEEE80211_F_ASCAN`
- return success

That means the IOC itself succeeded, but our backend never emitted the scan
completion edge that WCL needs after entering `ABORTED`.

This matches the live logs exactly:

- WCL transitions into `SCAN_MANAGER_STATE_ABORTED`
- external `SCAN_REQ` then hits the family ignore path and returns
  `0xe00002bc`

So the bug is no longer "scan request rejected too early".  The bug is
"abort acknowledged without the completion semantics that release the scan FSM
back to IDLE".

## `WCL_SCAN_ABORT` Fix Direction

The minimal compatible behavior for our fake-scan backend is:

- cancel any pending local scan timer
- clear the cached scan iteration state
- after accepting `WCL_SCAN_ABORT`, emit the same single `SCAN_DONE` edge that
  the family uses to drive `ABORTED -> IDLE`

That is not a Tahoe-specific hack.  It is the minimum required completion
semantic implied by the recovered Apple scan-plane FSM and by the fact that the
real Apple abort path is asynchronous producer work rather than a pure no-op.

## Runtime Status After `1537d1b`

Live logs on `AirportItlwm build=1537d1b` close the previous abort-stuck issue:

- `SCAN_MANAGER_STATE_ABORTED` now returns to `IDLE`
- the timeout/abort wedge is no longer the earliest open blocker

But the scan-complete path is still not `1:1` with Apple during a normal scan:

- `SCAN_MANAGER_STATE_IDLE -> IN_PROGRESS` on every scan request
- `fakeScanDone` fires with real nodes present
- immediately after that, external `APPLE80211_IOC_SCAN_RESULT` fails with
  `0xe0820445`
- WCL does not log `SCAN_MANAGER_EVENT_SCAN_COMPLETE` for that normal
  completion

So the next root cause is no longer "scan never finishes".  The next root cause
is "our normal completion bulletin does not match the Apple scan-adapter
producer contract".

## Tahoe Normal Scan-Complete Producer Contract

Reference recovery from
`ghidra_output/AppleBCMWLAN_Core_decompiled.c` shows two distinct Apple
producer paths:

- `AppleBCMWLANCore::scanComplete(int)` posts generic
  `APPLE80211_M_SCAN_DONE (0x0A)` with a 4-byte status
- `AppleBCMWLANScanAdapter::scanComplete(wl_event_msg_t*)` posts
  `APPLE80211_M_WCL_SCAN_DONE (0xED)` with a 4-byte status

The second path is the Tahoe-relevant one for the WCL-owned scan adapter.  The
decompile is explicit:

- `FUN_ffffff8002219ffe(..., 0xed, &status, 4, 1)`

Additional negative finding:

- our old synthetic `APPLE80211_M_BGSCAN_CACHED_NETWORK_AVAILABLE (0x3F)` does
  not match this producer path
- reverse docs classify `0x3F` as a variable-payload BG-scan cache event, not
  as a zero-length active-scan completion bulletin

## Normal Scan-Complete Root Cause

Our Tahoe fake completion path still synthesized:

- `APPLE80211_M_SCAN_DONE (0x0A)`
- followed by zero-length `APPLE80211_M_BGSCAN_CACHED_NETWORK_AVAILABLE (0x3F)`

That diverges from the Apple scan-adapter producer in two ways:

1. it uses the generic core bulletin instead of the scan-adapter-owned
   `APPLE80211_M_WCL_SCAN_DONE (0xED)`
2. it emits an extra fabricated `0x3F` bulletin with the wrong shape

This explains the live behavior:

- the timer callback proves the scan result set exists
- but WCL never consumes a normal `SCAN_COMPLETE` edge from that producer path
- external `SCAN_RESULT` keeps failing with `0xe0820445` until the later
  timeout/abort recovery path fires

## Fix Direction After `1537d1b`

The `1:1` fix direction is to make the fake Tahoe scan completion follow the
Apple scan-adapter contract:

- post `APPLE80211_M_WCL_SCAN_DONE (0xED)` with a 4-byte status payload
- stop fabricating zero-length `APPLE80211_M_BGSCAN_CACHED_NETWORK_AVAILABLE`
  from the active scan-complete path

If this is correct, Tahoe should finally synthesize the normal
`SCAN_MANAGER_EVENT_SCAN_COMPLETE` edge during the first completion, instead of
reaching `IDLE` only after watchdog-driven abort cleanup.

## Next Confirmed Root Cause After `5e32ebb`

Live boot `5e32ebb` finally proved that WCL scan completion is no longer the
blocking stage:

- `fakeScanDone ... posting WCL_SCAN_DONE (0xED)`
- `SCAN_MANAGER_STATE_IN_PROGRESS got event: SCAN_MANAGER_EVENT_SCAN_COMPLETE`
- `SCAN_MANAGER_STATE_IDLE`

But the interface still never leaves the unavailable bring-up path:

- `airportd` logs `_initInterface: en0 is down`
- immediately afterwards `_initInterface: Failed to query current SSID`
- later auto-join attempts abort with `error=(37 'driver not available')`
- kernel IOC DEBUG keeps showing `isDriverAvailable=<0>`

That combination means the next real blocker is no longer scan completion or
candidate delivery.  The blocker is that Tahoe still never observes a usable
post-attach `DRIVER_AVAILABLE` edge.

The live ordering makes the timing issue explicit:

1. The driver publishes `APPLE80211_M_DRIVER_AVAILABLE` from
   `AirportItlwm::start()` right after `enableAdapter()`.
2. Only later does the system create and announce the Apple-visible interface:
   `KEV_DL_IF_ATTACHED`, `IOServiceMatched`, `Apple80211GetIfListCopy ifCount=1`,
   then `airportd` begins `_initInterface` on `en0`.
3. No later `DRIVER_AVAILABLE` replay happens after the BSD/Apple80211 attach,
   so `_initInterface` still runs with `isDriverAvailable=0`.

This matches the observed failure mode exactly: the interface exists and scan
machinery already works, but the notification broker never sees a usable
availability transition at the point when the interface becomes consumable by
CoreWiFi/airportd.

## Fix Direction After `5e32ebb`

Do not invent new availability payloads again.  The payload shape and
controller/PostOffice route are already fixed.

The remaining delta is timing:

- keep the existing early `DRIVER_AVAILABLE` publish for parity with initial
  bring-up;
- replay the same Apple-shaped `APPLE80211_M_DRIVER_AVAILABLE` after BSD
  interface attach / `configureInterface()`, i.e. when `en0` actually becomes
  visible to `airportd`.

If this is the real root cause, the next boot should stop showing:

- `_initInterface: Failed to query current SSID`
- `AUTO-JOIN: ... error=(37 'driver not available')`
- IOC DEBUG `isDriverAvailable=<0>` during early external SSID/BSSID queries

## Corrected Tahoe IOC Routing For `SSID/BSSID/CHANNEL`

The earlier assumption that Tahoe was routing the visible pre-association
failures through selectors `0x31` / `0x6a` was wrong.

Recovered `IO80211Family` decompile from the remote Ghidra host shows the real
external path:

- `APPLE80211_IOC_SSID (1)`:
  `sendIOUCToWcl(..., selector=1, len=0x28)` then fallback to
  `apple80211getSSID(...)`
- `APPLE80211_IOC_CHANNEL (4)`:
  `sendIOUCToWcl(..., selector=4, len=0x10)` then fallback to
  `apple80211getCHANNEL(...)`
- `APPLE80211_IOC_BSSID (9)`:
  `sendIOUCToWcl(..., selector=9, len=0x10)` then fallback to
  `apple80211getBSSID(...)`

The fallback is reached only when the WCL/IOUC route returns
`0xe082280f` / "not implemented":

- `IO80211SkywalkInterface::routeIoctlToWcl(...)`
  returns `0xe082280f` when there is no handled IOUC result.
- `apple80211getSSID/BSSID/CHANNEL(...)` then dispatch into the protocol vtable
  (`IO80211InfraProtocol` / `IO80211NoneProtocol` / `IO80211AWDLProtocol`)
  where our zero-success pre-association handlers live.

So the real architectural mismatch was never "our protocol handlers are still
wrong". The active IOUC-facing path was remaining visible to late consumers
before they received a usable sticky `DRIVER_AVAILABLE` replay.

## Next Confirmed Root Cause After Live `471f6f1`

Live state on `471f6f1` disproved the older attach/discovery theories:

- `kmutil showloaded` confirms the running kext is `AirportItlwm build=471f6f1`
- `SIOCIFTYPE` on `en0` reports `if_type=6`, `if_subfamily=3`, which matches
  the Tahoe Path-B contract in the debug playbooks
- `getInterfaceSubFamily: returning 3` appears in the kernel log
- `airportd` already sees `en0`, starts scans, and `WCLScanManager` reaches
  `SCAN_MANAGER_STATE_IDLE`

But the same boot still shows:

- external `APPLE80211_IOC_SSID/BSSID -> 0xe0822403`
- external `APPLE80211_IOC_SCAN_RESULT -> 5`
- IOC debug still printing `isDriverAvailable=<0>`

The new decompile pass pinpoints the missing architectural detail: our Tahoe
BSD bridge still intercepts several external IOCTLs too early.

Recovered `IO80211Family` wrappers show that Apple does **not** call the
protocol methods for these selectors first.  It first routes them through
Skywalk/WCL IOUC and only falls back when the WCL route is absent:

- `SSID`: `sendIOUCToWcl(..., selector=1, len=0x28)`
- `CHANNEL`: `sendIOUCToWcl(..., selector=4, len=0x10)`
- `BSSID`: `sendIOUCToWcl(..., selector=9, len=0x0c)`
- `SCAN_RESULT`: `sendIOUCToWcl(..., selector=0x16, len=0x0c)`

Our local Tahoe bridge in `AirportItlwmSkywalkInterface::processBSDCommand()`
still forwards all four directly into `getSSID/getBSSID/getCHANNEL/getSCAN_RESULT`
from `processApple80211Ioctl()`, which bypasses the Apple `IO80211SkywalkInterface`
route entirely.  That is a true producer/consumer mismatch, not a cache issue.

## Fix Direction After `471f6f1`

For strict parity, the Tahoe local BSD bridge must stop short-circuiting these
four selectors:

- `APPLE80211_IOC_SSID`
- `APPLE80211_IOC_CHANNEL`
- `APPLE80211_IOC_BSSID`
- `APPLE80211_IOC_SCAN_RESULT`

They must be left `kIOReturnUnsupported` in the local bridge so that
`super::processBSDCommand()` keeps the Apple Skywalk path alive:

1. WCL/IOUC route first
2. protocol fallback only if the Apple route reports "not implemented"

If this is the real root cause, the next boot should stop showing the direct
external `SCAN_RESULT -> 5` and the driver-available state should be observed
through the same late IOUC-facing path as the Apple reference.

## Root Cause After `6ac8ac0`

Live boot state after the earlier availability replays made the remaining gap
clearer:

- `ioreg` already shows `AirportItlwmSkywalkInterface`, BSD `en0`, and multiple
  `IO80211APIUserClient` instances owned by `airportd`
- external `POWER`, `OP_MODE`, `SUPPORTED_CHANNELS` already succeed
- external `SSID` still fails with `0xe0822403`
- kernel IOC DEBUG still reports `isDriverAvailable=<0>`

This means:

1. the interface/BSD attach stage is no longer the last missing milestone;
2. the active IOUC query path is alive;
3. but the sticky availability notification still does not reach the late
   IOUC consumers that appear after driver `start()`.

Recovered `IO80211Family` transport details explain why timing at
`createEventPipe` matters:

- `IO80211SkywalkInterface::postMessageInternal(...)` only reaches userspace
  when the Glue object exists and async delivery is used;
- `IO80211SkywalkInterface::postMessageIOUC(...)` fans out only to currently
  opened IOUC event pipes;
- `createEventPipe(...)` is the point where those consumers become real.

So an early `DRIVER_AVAILABLE` publish from `start()` or even from
`ether_ifattach()` can still be too early for Tahoe's actual IOUC consumers.

## Fix Direction After `6ac8ac0`

Do not change the `DRIVER_AVAILABLE` payload shape again.

The remaining delta is replay timing:

- keep the existing early publish for initial Apple-style bring-up;
- keep the existing attach-time replay;
- replay the same sticky `APPLE80211_M_DRIVER_AVAILABLE` once more after
  `IO80211SkywalkInterface::createEventPipe(...)`, i.e. when the IOUC event
  pipe is opened and late userspace consumers can actually observe the event.

## Runtime Correction After `2820901`

Live reboot on `AirportItlwm build=2820901` proved that the remaining
availability mismatch is not explained by `CoreWiFiDriverReadyKey` alone.

Observed live state:

- `AirportItlwmSkywalkInterface` is present in `ioreg`
- `CoreWiFiDriverReadyKey = "true"` is visible on the interface
- multiple `IO80211APIUserClient` instances are attached (`airportd`,
  `wifip2pd`, `sharingd`)
- kernel IOC DEBUG still shows `isDriverAvailable=<0>`

The new Tahoe decompile closes the gap, but not in the way the first
availability fix assumed. `WCLSystemStateManager` does have an explicit
`driverAvailableEventHandler(...)` for `APPLE80211_M_DRIVER_AVAILABLE (0x37)`,
and it only accepts the event when:

- bulletin payload length is exactly `0xf8`
- dword at payload `+0x8` is zero for the available edge

But the producer-side Apple call chain is different from "core emits the
bulletin directly".  New `AppleBCMWLANCoreMac` decompile shows:

1. hidden interface-side object `+0x1510 -> +0x930` (`setInterfaceEnable(true)`)
2. `AppleBCMWLANCore::signalDriverReady()` publishes
   `CoreWiFiDriverReadyKey = "true"/"false"`

and on down/error paths:

1. hidden interface-side object `+0x1510 -> +0x920`
   (`interfaceAdvisoryEnable(...)`)
2. `AppleBCMWLANCore::signalDriverReady()`

So the real local drift was not just "missing property" or "missing bulletin".
It was that the Tahoe port skipped the interface lifecycle edge that Apple
fires before `signalDriverReady()`.  That explains the contradictory live state
"ready key present, scan path alive, but isDriverAvailable still 0".

Live reboot on `d2953c9` then narrowed the remaining delta one level deeper:
the local port did call hidden `setInterfaceEnable(true)`, and IO80211Family
logged that call, but `isDriverAvailable` still stayed `0`.  The missing piece
was the recovered Apple subclass body itself.

`AppleBCMWLANLowLatencyInterface::setInterfaceEnable(bool)` does not stop at
the base `IO80211InfraInterface::setInterfaceEnable(bool)`.  On the enable
edge it also does:

1. `reportLinkStatus(3, 0x80)`
2. `setLinkState(kIO80211NetworkLinkUp, 1, false, 0, 0)`

So the local hidden-object fix has to reproduce that exact subclass-side effect
chain, not merely the caller's `+0x930` dispatch.

Live runtime on build `43bf34f` then narrowed the remaining gap one step
further:

- the hidden subclass body now runs
- `CoreWiFiDriverReadyKey = "true"` is visible in `ioreg`
- the interface is attached as `en0`
- scan reaches `fakeScanDone` and posts `WCL_SCAN_DONE`
- but `isDriverAvailable` still remains `0`

The missing producer edge is explicit in the current-boot kernel log: there
are no `APPLE80211_M_DRIVER_AVAILABLE (0x37)` posts at all.

That matches the recovered family-side consumer contract in
`WCLSystemStateManager::driverAvailableEventHandler(...)`, which still
requires:

- message code `0x37`
- payload length `0xf8`
- dword at payload `+0x8` equal to zero for the available edge

So the current Tahoe ready chain must be treated as the full sequence:

1. hidden `setInterfaceEnable(true)` lifecycle edge
2. `CoreWiFiDriverReadyKey = "true"/"false"`
3. `APPLE80211_M_DRIVER_AVAILABLE` through controller/PostOffice delivery

Without step 3, the port reaches the contradictory state "interface alive and
scanning, but driver unavailable to external CoreWiFi consumers".
