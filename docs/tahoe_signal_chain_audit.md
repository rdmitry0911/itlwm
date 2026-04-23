# Tahoe Signal Chain Audit

Date: 2026-04-12

## Tahoe External Bootstrap Path Uses `getPrimarySkywalkInterface()`

Live build `0707196` corrects one earlier assumption in the audit.

The loaded Tahoe binary already contains:

- `AirportItlwmSkywalkInterface::processBSDCommand(...)`
- `AirportItlwmSkywalkInterface::processApple80211Ioctl(...)`
- `AirportItlwmSkywalkInterface::getSSID(...)`
- `AirportItlwmSkywalkInterface::getBSSID(...)`
- `AirportItlwmSkywalkInterface::getCHANNEL(...)`

Yet the live external bootstrap path still returns:

- `SSID/BSSID -> 0xe0822403`
- `VIRTUAL_IF_ROLE/PARENT -> 6`

So the active bootstrap getters are not using the already-lifted Skywalk
helpers directly.

The earlier "only `getPrimarySkywalkInterface()` matters" conclusion was too
narrow. Tahoe does use controller slot `+0xc80`
(`IO80211Controller::getPrimarySkywalkInterface()`) as one bootstrap seam, and
family getter bodies then consult:

- controller primary-interface cache `controller+0x120+0x188`
- peer-manager interface refs `+0x550/+0x558`

That explains the early `0707196` state better than a missing helper-body
theory: the interface already exists (`en0`, `CoreWiFiDriverReadyKey="true"`,
`fakeScanDone` posted), but the family bootstrap getters still see an empty
primary-interface source and return `0xe0822403` / raw `6`.

Later decomp and binary checks showed this is not the whole story: public
`SSID/BSSID/CHANNEL/CURRENT_NETWORK/ROAM_PROFILE` fallback also consults
interface slot `[411] isCommandProhibited(int)` with those same request
numbers after IOUC/WCL miss, so Tahoe needs both the primary-interface seam and
the public request gate to be correctly bound.

## Tahoe Bootstrap POWER Contract Correction

Live runtime on build `5da9d59` disproves the earlier local assumption about
Tahoe `APPLE80211_IOC_POWER`.

The local port was doing this on every request:

1. call `handlePowerStateChange(...)` immediately
2. drive `disableAdapter(...)` on `req=0`
3. publish `DRIVER_UNAVAILABLE`

The recovered Apple producer does not do that during bootstrap.

`AppleBCMWLANCore::setPOWER(...)` caches the requested state into core field
`+0x289c` and sets sticky bit `0x1000`; it does **not** call
`handlePowerStateChange(...)` directly on this path. Later
`AppleBCMWLANCore::setupDriver()` consumes that cached state and only then calls
`handlePowerStateChange(cachedState)`.

That exact difference matches the live failure:

- early Tahoe power traffic briefly requested `0`, then returned to `1`
- the local port treated that as a real OFF edge
- `WCLSystemStateManager` saw `DRIVER_UNAVAILABLE`
- external `SSID/BSSID/CURRENT_NETWORK` stayed at `0xe0822403`

So the remaining bug is not "missing another ready bulletin". The remaining bug
is applying bootstrap `setPOWER(...)` transitions too early.

Live runtime on build `36e4cc3` then exposed the next exact producer drift on
the same path:

- the transient bootstrap OFF edge is gone
- `power_state` stays at `1`
- but `WCLSystemStateManager` still receives
  `SSM_EVENT_SYSTEM_POWER_OFF` / `SSM_EVENT_SYSTEM_POWER_ON` before the
  availability decision
- then immediately receives `SSM_EVENT_DRIVER_UNAVAILABLE`

The local cause is that Tahoe still posts `APPLE80211_M_POWER_CHANGED` in two
places where Apple does not:

1. unconditionally from `start()`
2. from `handlePowerStateChange(...)` even on no-op `req == cur`

The reverse event map marks `POWER_CHANGED` as mandatory only for real
`setPowerState transitions`, not as a sticky bring-up bulletin. So the local
port must stop publishing it on bootstrap and on no-op `1 -> 1` requests.

## Tahoe Driver-Available Producer Correction

Live runtime on build `2820901` and the newer Tahoe decompile establish one
producer-side fact that overrides the earlier "property + synthetic bulletin"
theory:

- `CoreWiFiDriverReadyKey = "true"` can already be present in `ioreg`
- yet kernel IOC DEBUG still reports `isDriverAvailable=<0>`

Recovered `WCLSystemStateManager::driverAvailableEventHandler(...)` explains
why the consumer can stay at `isDriverAvailable=0`, but the producer-side
Apple chain matters just as much. The consumer accepts
`APPLE80211_M_DRIVER_AVAILABLE (0x37)` only when the bulletin payload:

- has exact length `0xf8`
- carries the "available" edge as zero at payload `+0x8`

The new `AppleBCMWLANCoreMac` decompile then corrects the producer theory:

1. Apple calls hidden interface-side `+0x930` (`setInterfaceEnable(true)`)
   first
2. only then does `AppleBCMWLANCore::signalDriverReady()` publish
   `CoreWiFiDriverReadyKey = "true"/"false"`
3. on down/error paths Apple uses hidden `+0x920`
   (`interfaceAdvisoryEnable(...)`) before repeating `signalDriverReady()`

So the bug is no longer modeled as "core must fabricate the bulletin itself".
The Apple-shaped producer edge is an interface lifecycle callback first, then
the ready property publication.  Skipping that hidden interface-enable edge can
leave the system in the contradictory state "ready key visible, interface
attached, scan runs, but isDriverAvailable=0".

Live reboot on `d2953c9` proved that the caller-order fix alone is still not
enough on the local port. IO80211Family logs show hidden
`setInterfaceEnable(true)` running on `AirportItlwmSkywalkInterface`, yet Tahoe
still leaves `isDriverAvailable=0`.

The remaining delta is the subclass body behind that hidden slot.
Recovered `AppleBCMWLANLowLatencyInterface::setInterfaceEnable(bool)` does:

1. base `IO80211InfraInterface::setInterfaceEnable(bool)`
2. `reportLinkStatus(3, 0x80)`
3. `setLinkState(kIO80211NetworkLinkUp, 1, false, 0, 0)`

So the exact producer lift must happen on the hidden interface object itself,
not by adding more controller-side ready-state publication.

## Driver-Available Bulletin Regression Correction

Live runtime on build `43bf34f` adds one more producer-side fact that narrows
the remaining gap further:

- the hidden `setInterfaceEnable(true)` subclass body now runs
- `CoreWiFiDriverReadyKey = "true"` is visible in `ioreg`
- but there are no current-boot kernel log entries for
  `APPLE80211_M_DRIVER_AVAILABLE (0x37)`
- and `WCLSystemStateManager` keeps `isDriverAvailable=0`

That is enough to reject the newer local implementation which only published
the ready property after the hidden interface-enable edge.  The family-side
consumer still requires the `0x37` bulletin with the exact Tahoe payload ABI:

- message code `APPLE80211_M_DRIVER_AVAILABLE`
- payload length `0xf8`
- dword at payload `+0x8` equal to zero for the available edge

So the remaining local producer contract is not "property only".  It is the
combination:

1. hidden `setInterfaceEnable(true)` lifecycle edge
2. `CoreWiFiDriverReadyKey = \"true\"/\"false\"`
3. `APPLE80211_M_DRIVER_AVAILABLE` delivered through controller/PostOffice

Without step 3, the live system reaches the contradictory state:

- interface attached as `en0`
- link state raised
- scan runs and posts `WCL_SCAN_DONE`
- but `isDriverAvailable` never leaves `0`
- external `SSID/BSSID/CURRENT_NETWORK` stay at `0xe0822403`

## Driver-Available Payload Polarity Correction

Live runtime on build `eea599b` proves the restored `0x37` bulletin is still
locally wrong even though the message now appears in the kernel log:

- `setInterfaceEnable(true)` runs
- `CoreWiFiDriverReadyKey = "true"` is visible in `ioreg`
- `postTahoeDriverAvailableBulletin ready=1 len=0xf8 available=0` is logged
- but external `SSID/BSSID/CURRENT_NETWORK` remain at `0xe0822403`
- and `isDriverAvailable` stays `0`

The recovered family-side consumer makes the polarity mistake explicit.
`WCLSystemStateManager::driverAvailableEventHandler(...)` does:

1. validate payload size `0xf8`
2. read `reason` from payload `+0x10`
3. if `*(int *)(payload + 8) == 0` call `processEvent(..., 4, ...)`
4. else, unless `reason == -0x1f7dd7fd`, call `processEvent(..., 5, ...)`

The recovered SSM matrix gives those event ids concrete meaning:

- `4 = DRIVER_UNAVAILABLE`
- `5 = DRIVER_AVAILABLE`

So the local Tahoe bulletin polarity was inverted. Posting `available=0` on
the ready edge does not mean "driver available"; it explicitly feeds the
`DRIVER_UNAVAILABLE` event into SSM. The strict Apple-visible contract is:

- ready edge: non-zero dword at payload `+0x8`
- unavailable edge: zero dword at payload `+0x8`

The local port must therefore stop publishing `available=0` when `ready=true`.

## Tahoe Bootstrap Query Cache Correction

Live runtime on build `db546d2` narrows the still-active bring-up blocker to
the external bootstrap query path, not to scan completion.

Observed order:

1. Tahoe interface attaches as Wi-Fi `en0`
2. `airportd` binds with `useIOUCWhenPossible TRUE`
3. `_initInterface` queries `APPLE80211_IOC_SSID`
4. the external query returns `0xe0822403`
5. `_initInterface` aborts with `Failed to query current SSID`

This matters because the earlier "keep SSID/BSSID/CHANNEL unsupported locally
so super::processBSDCommand() preserves IOUC-first routing" conclusion is too
strong for the visible bootstrap contract. The reverse docs are explicit that
Apple's framework cache layer pre-zeroes SSID/BSSID/CHANNEL and still returns
success before association. Our local port cannot leak the low-level
`0xe0822403` to airportd during `_initInterface`, regardless of whether the
internal IOUC/WCL path exists.

So the exact visible Tahoe contract for third-party bootstrap is:

- `getSSID()` -> success + zeroed SSID pre-association
- `getBSSID()` -> success + zeroed BSSID pre-association
- `getCHANNEL()` -> success + zeroed channel pre-association

The active compiled Tahoe bridge must therefore route those three external GETs
to the local cache-shaped helpers until the internal WCL route itself reaches
the same visible semantics.

## Owner-Family Backend Batch

The remaining pre-`Q12` owner-family setters now route through the local
`TahoeCommander` compatibility layer instead of hand-written per-selector cache
updates in `AirportItlwmSkywalkInterface`.

This batch lifts the Apple-visible hidden-owner bodies for:

- `setIE`
- `setOFFLOAD_NDP`
- `setBTCOEX_PROFILE`
- `setWCL_ACTION_FRAME`
- `setRANGING_AUTHENTICATE`

The important architectural change is not "more cache writes". It is that the
owner-targeted state now lives in `TahoeOwnerRegistry`, is built through
`TahoePayloadBuilders`, and is entered through a single commander path with
selector/owner tagging. That matches the recovered Apple shape much more
closely than letting each Skywalk setter carry its own ad hoc reconstruction.

The local port still lacks Broadcom firmware/commander internals, so this is
not a firmware-faithful `runIOVarSet` backend. But the Apple-visible body shape
for these selectors is now centralized:

- exact public gate
- exact carrier split
- owner-targeted state block
- selector-tagged command context

## Commander V2 First Engineering Batch

The first implementation batch for the remaining backend layer now replaces the
single monolithic commander header with a split internal architecture:

- `TahoeOwnerBase`
- `TahoeCommandRouter`
- `TahoeOwners`
- `TahoeCommanderV2`

This batch only moves the first four owner families into explicit owner
objects:

- `USB_HOST_NOTIFICATION`
- `BTCOEX_PROFILE_ACTIVE`
- `BTCOEX_2G_CHAIN_DISABLE`
- `BYPASS_TX_POWER_CAP`

`setIE`, `setOFFLOAD_NDP`, `setBTCOEX_PROFILE`, `setWCL_ACTION_FRAME`, and
`setRANGING_AUTHENTICATE` still enter `TahoeCommanderV2`, but their owner
bodies remain inline in the commander until the async/virtual-target batches
arrive. The important engineering change is that the layer boundary is now in
place: future lifts can move family-by-family into owners without rewriting the
Skywalk entrypoints again.

## Commander V2 Completion / Transport Batch

The remaining families now also live in dedicated owner classes:

- `TahoeJoinIeOwner`
- `TahoeNdpOwner`
- `TahoeActionFrameOwner`
- `TahoeRangingOwner`

And `TahoeCommanderV2` now carries an explicit internal dispatch fabric:

- `dispatchIOVarSet`
- `dispatchVirtualIOVarSet`
- `dispatchVirtualIOCtlSet`
- `dispatchIssueCommand`
- `dispatchHiddenCallback`

`TahoeAsyncCommandContext` was extended with:

- token
- transport kind
- request/response sizes
- step count
- timeout/completion state

This still does not speak real Broadcom firmware, but it removes the last
monolithic "cache then success" shape from the remaining pre-`Q12` backend
families. The unresolved part after this batch is runtime parity on a live
system, not another missing internal split.

## Goal

Stop treating Tahoe bring-up failures as isolated symptom fixes. Audit each
init/runtime signal chain against the Apple reference and remove local
architectural shortcuts only when the producer/consumer contract is recovered
well enough to implement it 1:1.

## Bring-up / Runtime Planes

The reverse bundle in `../Декомпилы/wifi_reverse_yaml_bundle_FULL_FIXED_v15`
and the local/remote Ghidra outputs converge on this Tahoe order:

1. system-state plane
2. scan plane
3. join/assoc plane
4. net-link plane
5. LQM plane
6. roam plane
7. GAS/ANQP plane
8. background offload plane

So a Tahoe port must first satisfy producer contracts that feed:

- `WCLConfigManager`
- `WCLSystemStateManager`
- `WCLScanManager`
- `airportd _initInterface`

before later UI/user-visible scan behavior can be trusted.

## Already Confirmed Earlier Root Causes

The following mismatches were already confirmed and fixed in earlier commits:

- `APPLE80211_IOC_PLATFORM_CONFIG` was left unsupported / zero-stubbed instead
  of following the Apple 7-byte packed producer contract.
- `APPLE80211_IOC_POWERSAVE` returned unsupported instead of accepting and
  caching the requested level.
- payload-less `VIRTUAL_IF_ROLE/PARENT` initially fell through to raw POSIX
  `6` instead of returning Apple `0xe082280e`.
  Live build `5cb2a53` later proved the remaining raw `6` came from the
  controller-side `AirportItlwm::apple80211Request(...)` dispatcher still
  lacking explicit cases for request numbers `96/97`, not from the Tahoe
  interface-side BSD bridge anymore.
- `APPLE80211_IOC_WCL_TRIGGER_CC` returned unsupported instead of accepting the
  Apple mode contract and caching the first `0x20` bytes.
- `SCAN_ABORT` and scan-complete delivery diverged from the Tahoe WCL path.
- `DRIVER_AVAILABLE` routing/timing diverged from the controller/PostOffice
  path and from the real BSD attach ordering.

This document covers the next layer: architectural stubs still present after
those visible blockers.

## Current Structural Divergences

`AirportItlwmSkywalkInterface.hpp` still contains two classes of non-Apple
behavior:

1. `kIOReturnUnsupported` in vtable slots where Apple ships real producer code.
2. inline `if (!data) return error; return success;` stubs where Apple carries
   state, calls an adapter, or both.

The first batch recovered well enough to implement directly from decompile is:

- `setOS_FEATURE_FLAGS`
- `setDHCP_RENEWAL_DATA`
- `setBATTERY_POWERSAVE_CONFIG`
- `setPOWER_PROFILE`
- `setIPV4_PARAMS`
- `setIPV6_PARAMS`
- `setINFRA_ENUMERATED`

## Reference Producer Contracts Recovered

### `setOS_FEATURE_FLAGS`

Apple path:

- stores the 64-bit flag word in core state
- derives several single-bit cached booleans from that word
- runs follow-up configuration (`DynSAR`, `6G`, `KVR`, link-loss suppression,
  optional scan-forward/AOP, optional adaptive 11r)

What matters immediately for Tahoe parity:

- this is not a no-op
- null returns `0xe00002bc`
- the incoming flag word becomes persistent driver state

### `setDHCP_RENEWAL_DATA`

Apple path:

- null returns `0xe00002bc`
- stores the first byte as a persistent bool in core state

### `setBATTERY_POWERSAVE_CONFIG`

Apple path:

- null returns `0xe00002bc`
- stores the first 32-bit mode
- passes it into the battery save configuration path

### `setPOWER_PROFILE`

Apple path:

- null returns `0xe00002bc`
- stores the first 32-bit profile in core state at `+0x29f0`
- then dispatches through the power-policy vtable

### `setIPV4_PARAMS`

Apple path:

- null returns `0xe00002bc`
- optionally forwards to the infra-interface side
- stores IPv4/mask/router/tail fields in persistent core state
- triggers IPv4 notification handling
- triggers keepalive notification if both address and mask are non-zero

The important architectural point is that Tahoe expects this IOC to update
driver-owned IPv4 state, not merely acknowledge the request.

### `setIPV6_PARAMS`

Apple path:

- forwards to infra-interface if present
- stores up to 10 IPv6 entries
- clears the companion cached area
- stores count
- seeds a link-local `fe80::` address prefix in dedicated core state
- schedules IPv6 notification handling

Again, this is a state-carrier IOC, not an ack-only slot.

### `setINFRA_ENUMERATED`

Apple path:

- null returns `0xe00002bc`
- non-null returns success

This one is a true minimal producer contract; our previous generic
`kIOReturnError` null handling was still wrong.

## Batch 1 Fix Direction

For the methods above, strict parity means:

- remove inline ack-only stubs from the Tahoe vtable
- move them into explicit out-of-line implementations
- return Apple `0xe00002bc` for null requests
- persist the incoming state in driver-owned cached fields
- leave comments tying each field back to the recovered Apple contract

This is not the full end state for every plane, but it removes a concrete class
of architectural mismatches: IOC handlers that Apple uses as state carriers but
our port previously treated as disposable success stubs.

## Remaining Gaps After Batch 1

The next unresolved cluster is adapter-owned behavior, not simple cache
carriers:

- `setWCL_REASSOC`
- `setWCL_LEGACY_ROAM_PROFILE_CONFIG`
- `setWCL_ROAM_PROFILE_CONFIG`
- `setWCL_CONFIG_BG_MOTIONPROFILE`
- `setWCL_CONFIG_BG_NETWORK`
- `setWCL_CONFIG_BGSCAN`
- `setWCL_CONFIG_BG_PARAMS`

## Q9 Closure: JOIN_ABORT now follows the Apple abort-complete contract

`setWCL_JOIN_ABORT` was the first join-plane WCL producer that could be closed
without guessing at a deeper roam/net/bgscan owner.

Recovered Apple path:

- `AppleBCMWLANCore::setWCL_JOIN_ABORT(apple80211_wcl_abort_join*)`
- delegates into `AppleBCMWLANJoinAdapter::abortFirmwareJoinSync(bool)`
- treats `NULL` as boolean `false` rather than returning an error
- when the incoming bool is true, publishes `APPLE80211_M_WCL_JOIN_ABORT_COMPLETE`
  (`0xD6`)

The checked `WCLJoinManager` symbolic FSM confirms the consumer side:

- `JOIN_ABORT_REQ` moves active states into `ABORTED`
- the manager only returns `ABORTED -> IDLE` after `JOIN_ABORT_COMPLETE`

So the real architectural bug in our Tahoe port was not just "slot [598] is a
stub"; it was that the join plane never emitted the completion edge the Apple
consumer waits for. The local fix therefore had two parts:

- remove the inline ack-only stub
- abort the same in-flight net80211 auth/assoc owner state that our
  `setASSOCIATE(...)` path drives, then publish `APPLE80211_M_WCL_JOIN_ABORT_COMPLETE`

This closes the standalone `Q9` join-abort discrepancy. Remaining reassoc and
roam-driven join hidden-owner exactness now stays under `Q13`, because those
paths still delegate into unrecovered Apple roam/net adapter owners rather than
the simple join-abort owner.

## Q10 Closure: net-link adjunct producers no longer sit on blind stubs

The remaining open `Q10` note was never about the whole link/IP plane. It was
about a narrow set of WCL-owned adjunct producers that Apple routes through
NetAdapter / PowerManager owners:

- `setWCL_REAL_TIME_MODE`
- `setWCL_QOS_PARAMS`
- `setWCL_LINK_UP_DONE`
- `setOFFLOAD_ARP`
- `setOFFLOAD_TCPKA_ENABLE`

Recovered Apple contracts show these are not generic unsupported slots:

- `setWCL_REAL_TIME_MODE`: NULL -> `0xe00002bc`, otherwise select real-time vs
  default mode from the first byte
- `setWCL_QOS_PARAMS`: decode a flagged carrier covering long retry limit, RTS
  threshold, two lifetime buckets, and powersave mode
- `setWCL_LINK_UP_DONE`: call `PowerManager::handleLinkUpConfiguration()`
- `setOFFLOAD_TCPKA_ENABLE`: default `0xe00002c7`, flip the keepalive enable
  byte only when the feature gate and owner object exist
- `setOFFLOAD_ARP`: reject NULL / no infra owner with raw `0x16`, then carry
  IPv4 + keepalive fields into core-owned state

The local Tahoe port still lacks Apple's hidden PowerManager / KeepAlive owner
objects, but these slots no longer disappear into inline success stubs. They
now preserve the recovered carrier fields and apply the local owner actions
that do exist today:

- powersave re-entry through the lifted Tahoe `setPOWERSAVE(...)` path
- RTS threshold through `ieee80211com::ic_rtsthreshold`
- post-link MAC-context refresh through `ic_updateedca`
- persistent keepalive / IPv4 carrier state for the already-lifted IPv4/TCPKA
  getters

That closes `Q10` as a standalone queue: the net-link plane no longer has
blind adjunct stubs of its own. The remaining exact helper choreography for
those adapter-owned producers now stays under `Q13`.

## Root Cause After Live `4973c4d`

The next architectural mismatch was not another bad payload or wrong timer edge.
It was a missing reachability layer in the Tahoe Skywalk BSD bridge.

Live proof from the rebooted `4973c4d` system:

- WCL logged `setPOWERSAVE@1551: arg->powersave_level = 7 not supported`
- the source tree already contained real Tahoe `getPOWERSAVE` /
  `setPOWERSAVE` handlers in `AirportItlwmSkywalkInterface`
- therefore the failure could not be "handler absent" or "handler still
  returns unsupported"

The real divergence was that `AirportItlwmSkywalkInterface::processApple80211Ioctl`
forwarded only a small subset of Apple80211 BSD IOCTLs, while the legacy
`AirportSTAIOCTL.cpp` dispatcher still routed a much larger set of already
implemented handlers.

So on Tahoe we had a class of false-negative failures:

- the handler existed
- the semantics were already recovered and implemented
- but the Skywalk BSD bridge never made that handler reachable
- the framework therefore fell back to `unsupported` before our code could run

`APPLE80211_IOC_POWERSAVE` was the first live-confirmed member of that missing
bridge cluster, but the audit showed it was not alone.

## Missing Skywalk Bridge Cluster

Comparing the legacy dispatcher against the Tahoe Skywalk bridge showed that
the Skywalk path was missing routes for already implemented or controller-backed
handlers, including:

- `APPLE80211_IOC_POWER`
- `APPLE80211_IOC_POWERSAVE`
- `APPLE80211_IOC_RSSI`
- `APPLE80211_IOC_RATE`
- `APPLE80211_IOC_TXPOWER`
- `APPLE80211_IOC_OP_MODE`
- `APPLE80211_IOC_SUPPORTED_CHANNELS`
- `APPLE80211_IOC_HW_SUPPORTED_CHANNELS`
- `APPLE80211_IOC_CIPHER_KEY`
- `APPLE80211_IOC_COUNTRY_CODE`
- `APPLE80211_IOC_DRIVER_VERSION`
- `APPLE80211_IOC_HARDWARE_VERSION`
- `APPLE80211_IOC_MCS`
- `APPLE80211_IOC_MCS_VHT`
- `APPLE80211_IOC_NSS`

This is an architectural defect, not a cosmetic cleanup item.  Apple/Tahoe's
Skywalk path still depends on the BSD IOCTL bridge reaching the same producer
logic that the older STA path already exposed.

## Fix Direction After `4973c4d`

Do not patch around each resulting runtime symptom separately.

The correct fix is to restore the missing BSD-bridge reachability:

- if Tahoe already has a real Skywalk handler, route the IOCTL to it
- if the implementation is controller-owned in `AirportItlwm`, route the IOCTL
  there through `instance`
- keep comments tying the bridge restoration to the live `setPOWERSAVE ...
  not supported` proof, so this does not regress into another "handler exists
  but Tahoe can never reach it" failure

## Root Cause After Live `d7318b6`

The next architectural mismatch was not another WCL timer bug or another BSD
bridge omission. It was that the Tahoe port still lacked the real Apple
driver-ready producer.

Recovered Apple producer path:

- `AppleBCMWLANCore::signalDriverReady()`
- creates `OSSymbol("CoreWiFiDriverReadyKey")`
- creates `OSString("true")` or `OSString("false")`
- publishes that key/value pair through the hidden interface-side object stored
  at core-state `+0x1510` via vtable slot `+0x9f8`

Live proof on rebooted `d7318b6` matched that missing producer exactly:

- our synthetic `DRIVER_AVAILABLE/55` bulletins were visible in kernel logs
- `airportd` and WCL still kept `isDriverAvailable=0`
- `ioreg -l -r -n AirportItlwmSkywalkInterface` exposed no
  `CoreWiFiDriverReadyKey`

So the older theory that Tahoe only needed a family-accepted `0xf8`
`APPLE80211_M_DRIVER_AVAILABLE` payload is disproven. That was a consumer-side
guess, not the reference producer path.

## Correct Ready-State Carrier

For Tahoe parity, readiness must be carried through the same producer state that
Apple uses:

- publish `CoreWiFiDriverReadyKey=true/false` on the interface-side object
- update it at the same readiness boundaries (`enable`, `disable`, power
  transitions, teardown/failure)
- keep the decompile references in code so the port does not regress back to
  treating readiness as an arbitrary bulletin-payload problem

## Root Cause After Live `5e5d8da`

The first implementation of that recovered producer still diverged from Apple
in one important detail: the value type.

Live proof on rebooted `5e5d8da`:

- `ioreg -l -r -n AirportItlwmSkywalkInterface` finally showed
  `CoreWiFiDriverReadyKey`
- but it appeared as `Yes`, meaning our port had published an `OSBoolean`
- kernel IOC debug still printed `isDriverAvailable=<0>` for the same boot
- `APPLE80211_IOC_SSID/BSSID` continued to fail through the WCL path with
  `0xe0822403`

That matches the decompile exactly:

- `AppleBCMWLANCore::signalDriverReady()` creates
  `OSString("true")` or `OSString("false")`
- the hidden interface-side object at `+0x1510` receives that string object,
  not an `OSBoolean`

So the bug was no longer "missing CoreWiFiDriverReadyKey".  The bug was that
the Tahoe port published the right key with the wrong value type, which is not
architecturally equivalent to the Apple producer.

## Correct Ready-State Value Contract

For strict parity, the ready-state producer must publish:

- key: `OSSymbol("CoreWiFiDriverReadyKey")`
- value: `OSString("true")` or `OSString("false")`
- target: the same interface-side object used by the reference producer

Anything else, including `OSBoolean true/false`, is only a lookalike ioreg
surface and does not satisfy the recovered Apple contract.

## Panic Root Cause After Live `573356c`

The previous conclusion about Tahoe interface construction was wrong and was
disproven by a direct boot panic on the rebuilt driver.

Live panic from `/Library/Extensions/panic16.txt`:

- `IO80211InfraInterface::linkState() + 0xb`
- page fault at `CR2=0x18`
- call chain:
  `IO80211PeerManager::initWithInterface()`
  → `IO80211SkywalkInterface::start()`
  → `IO80211InfraInterface::start()`
  → `AirportItlwm::start()`

New decompile from
`/Volumes/macos-750/Users/bob/Projects/Декомпилы/ghidra_output/IO80211Family_decompiled.c`
shows why that panic is definitive:

- `IO80211InfraInterface::linkState()` returns
  `*(uint32_t *)(*(long *)(this + 0x128) + 0x18)`
- so a null `this+0x128` produces exactly the observed `CR2=0x18` fault
- `IO80211InfraInterface::start()` immediately enters
  `IO80211SkywalkInterface::start()`, where `PeerManager::initWithInterface()`
  logs through `linkState()` before the interface can survive with a null
  infra-state block

That means our switch to `IO80211InfraInterface::init(provider, addr)` was not
just unverified. It was wrong for the current port state: the object reached
`start()` without a valid infra ivar block at `this+0x128`.

The same new decompile also materially weakens the earlier assumption that the
2-argument Tahoe overload was the authoritative init entry:

- the recovered `IO80211Family_decompiled.c` includes
  `IO80211SkywalkInterface::init()` and `IO80211InfraInterface::init()`
- it does **not** recover a concrete `IO80211InfraInterface::init(IOService*,
  ether_addr*)` body
- so the earlier xref doc was not enough to justify overriding the live,
  crash-backed constructor contract

Strict-parity correction from this point:

- treat the live panic + decompile-backed `linkState()` dependency as the hard
  contract
- restore the no-arg `IO80211InfraInterface::init()` path that leaves the
  object in the non-panicking state for `start()`
- mark the earlier 2-arg-init conclusion as superseded, so the port does not
  regress back into the same `linkState()+0xb / CR2=0x18` panic

## Q1 Constructor Path Closure: APSTA-style no-arg init plus parameter binding

The remaining constructor mismatch was not inside `start()` itself, but in how
the Tahoe port entered interface construction.

New evidence now makes the Apple pattern clear enough to close `Q1`:

- `AppleBCMWLANCoreMac` imports both `IO80211InfraInterface::init()` and
  `IO80211SkywalkInterface::init(IOService*, ether_addr*)`
- Apple infra subclasses do **not** expose the two-argument skywalk init as
  their public constructor surface
- recovered Apple subclass bodies show a split pattern:
  - `AppleBCMWLANIO80211APSTAInterface::init()` is the subclass init entry
  - separate hidden wrapper paths perform provider/parameter binding afterwards
  - Apple proximity/NAN interfaces follow the same overall pattern

That means the Tahoe port still had one constructor-level drift left:

- controller code called `fNetIf->init(this, addr)` directly on Tahoe
- the local override then ignored the two arguments and internally fell back to
  no-arg `IO80211InfraInterface::init()`
- this was crash-free, but it was not architecturally 1:1 with the recovered
  Apple APSTA construction model

Strict-parity correction:

- make Tahoe interface construction enter through no-arg `init()`
- move provider/local-parameter binding into a separate local wrapper, mirroring
  Apple’s split between subclass init and parameterized follow-up init paths
- keep `attach(this)` as the provider exposure step after init/binding, not as
  an implicit side effect of a misleading two-argument init override

With that split, the port no longer advertises a fake Tahoe "2-arg infra init"
contract. The constructor/start sequence now matches the recovered Apple shape
as closely as the current decompile allows:

- subclass no-arg init
- local controller/role binding
- attach(provider)
- registerEthernetInterface
- `fNetIf->start(provider)`
- `deferBSDAttach(false)`

Implementation closure for `Q1`:

- Tahoe `AirportItlwmSkywalkInterface` now overrides no-arg `init()` instead
  of exposing a fake local `init(provider, addr)` surface
- controller start now calls `fNetIf->init()` followed by a dedicated
  `bindController(this)` helper
- provider-owned state (`instance`, `fHalService`, `scanSource`) and interface
  identity (`role/id`) now move through that explicit follow-up binding path,
  which keeps the constructor contract aligned with recovered Apple APSTA
  structure
- the secondary 2-arg Tahoe vtable slot is still shadowed locally only because
  the kernel does not export `IO80211InfraInterface::init(provider, addr)`;
  the port no longer uses that slot as its controller construction entry

## Ready-State Replay Audit After `78f162d`

Once the Tahoe init path was restored, the next audit target was the pile of
ready-state replay hooks that had accumulated while the port was still on the
wrong interface-construction path.

Recovered Apple producer evidence does **not** support those replays:

- `AppleBCMWLANCore::signalDriverReady()` is called from core boot/load paths
  (for example around `loadAndSetup` in the decompile at
  `AppleBCMWLANCoreMac_decompiled.c:41741` / `:41771`)
- the reference attach-chain doc says `deferBSDAttach(false)` itself performs
  the interface `registerService()` exposure step on Tahoe
- there is no corresponding Apple call site that republishes readiness from
  `createEventPipe()`, from `ether_ifattach()`, or from an extra post-defer
  manual `fNetIf->registerService()`

That means the following local paths were architectural drift, not reference
behavior:

- replaying `CoreWiFiDriverReadyKey` inside `createEventPipe()`
- replaying `CoreWiFiDriverReadyKey` after `ether_ifattach()`
- manually calling `fNetIf->registerService()` again after
  `deferBSDAttach(false)` already exposed the interface

Those changes were symptom-driven attempts to compensate for the previously
wrong Tahoe init path. Once the init contract is corrected, keeping those extra
replay/exposure hooks would preserve non-Apple producer timing and duplicate
service exposure that the reference attach chain does not perform.

So the strict-parity cleanup is:

- keep readiness publication on real producer boundaries (`enableAdapter`,
  disable/power transitions)
- remove consumer-side replay hooks (`createEventPipe`, `ether_ifattach`)
- remove the duplicate `fNetIf->registerService()` after
  `deferBSDAttach(false)`

## Q5 Getter Audit: First Strictly-Recovered Subset

The next queue after the panic-path cleanup was the raw-`6` getter cluster.
This could not be treated as one blob because the Apple evidence recovered so
far is uneven across the getters.

Recovered contracts from the new `AppleBCMWLAN_Core_decompiled.c` and
`IO80211Family_decompiled.c` are strong enough to close only this subset:

- `AppleBCMWLANCore::getRATE()`:
  returns `0xe0822403` when not associated, success otherwise
- `IO80211BssManager::getCurrentRateSet()`:
  no current BSS → `0xe0822403`
  empty cached set → `0xe00002f0`
  else copies the cached rate-set blob and returns success
- `IO80211BssManager::getCurrentRSSI(int&)`:
  no current BSS → `0xe0822403`
  else returns the cached RSSI

That is enough to eliminate raw POSIX `6` from:

- `getRATE`
- `getRATE_SET`
- `getRSSI`

both in the Tahoe Skywalk path and in the legacy STA dispatcher, because those
paths expose the same producer semantics to Apple80211 consumers.

It is **not** enough yet to claim parity for the remaining raw-`6` getters:

- `getMCS_INDEX_SET`
- `getNOISE`
- `getTXPOWER`
- `getMCS`

For those, Apple still depends on additional helper or config-manager state
that is not mapped 1:1 in the current port:

- `getMCS_INDEX_SET` uses a dedicated validity flag before returning the cached
  MCS blob; our current local state does not yet expose that carrier cleanly
- `getNOISE` returns `0x66` when the current-noise sample-valid bit is absent;
  our HAL currently exposes only a scalar noise value, not the corresponding
  validity bit
- `getTXPOWER` uses the `"qtxpower"` config query path, not the local
  `ic_txpower` field
- `getMCS` still needs its exact Apple helper/body lifted before its fallback
  can be corrected without guessing

So the strict-parity rule for this batch is:

- close only the getter subset with complete Apple helper evidence
- explicitly leave the rest open in the discrepancy inventory
- do not replace remaining raw `6` sites with lookalike zeros or generic
  success paths

## Q13 First Confirmed Mini-Batch: `getHW_ADDR`

The next unsupported slot with enough evidence to lift cleanly is
`getHW_ADDR`.

Recovered Apple producer contract:

- `AppleBCMWLANCore::getHW_ADDR(apple80211_hw_mac_address*)`
  writes `version=1`
- then copies six bytes from core state offsets `+0x1614..+0x1619`
- and returns success

Recovered family-side consumer contract:

- `WCLDeviceConfiguration::setHwMacAddr(apple80211_hw_mac_address&)`
  copies `*(u32 *)(arg+4)` plus `*(u16 *)(arg+8)` into its cached state
- so the ABI is confirmed as:
  `u32 version` + `u8 hw_addr[6]`

That is enough to make one narrow Q13 correction without guessing:

- add the missing `apple80211_hw_mac_address` ABI locally
- implement `getHW_ADDR` as `version=1` plus the controller hardware address
- remove generic `kIOReturnUnsupported` from slot `[511]`

This batch does **not** generalize to nearby slots like
`getTHERMAL_INDEX`, `getPOWER_BUDGET`, or `getOFFLOAD_TCPKA_ENABLE`.
Those still depend on unrecovered core-state/config-manager carriers and remain
open in the discrepancy inventory.

## Q13 First Confirmed Apple-Unsupported Getter Batch

The next safe way to reduce the Tahoe unsupported surface is not to invent
producer paths, but to classify the slots where Apple already returns
`0xe00002c7` from `AppleBCMWLANInfraProtocol` itself.

Recovered explicit unsupported getters:

- `AppleBCMWLANInfraProtocol::getRANGING_ENABLE(...)`
- `AppleBCMWLANInfraProtocol::getRANGING_START(...)`
- `AppleBCMWLANInfraProtocol::getRANGING_CAPS(...)`
- `AppleBCMWLANInfraProtocol::getCOUNTRY_CHANNELS_INFO(...)`
- `AppleBCMWLANInfraProtocol::getWCL_WNM_OFFLOAD(...)`
- `AppleBCMWLANInfraProtocol::getFW_CLOCK_INFO(...)`
- `AppleBCMWLANInfraProtocol::getTIMESYNC_STATS(...)`
- `AppleBCMWLANInfraProtocol::getHE_COUNTERS(...)`
- `AppleBCMWLANInfraProtocol::getSMARTCCA_OPMODE(...)`
- `AppleBCMWLANInfraProtocol::getLQM_STATISTICS(...)`

Each decompiles to a direct `return 0xe00002c7;` stub in the Apple vendor-side
infra path, with no hidden helper, carrier, or stateful producer behind it.

That matters for the audit because these slots are no longer evidence of
"missing implementation" in the port. They should be removed from the open
unsupported discrepancy census and tracked as "Apple also unsupported".

This classification does **not** generalize to adjacent slots. For example:

- `getRANGING_ENABLE` and `getRANGING_START` are explicit unsupported
  in Apple, but `setRANGING_ENABLE` still dispatches through a separate
  producer path
- `getCOUNTRY_CHANNELS_INFO` is explicit unsupported, but
  `getCOUNTRY_CHANNELS` is still a real Apple core path
- `getWCL_WNM_OFFLOAD` is explicit unsupported, but nearby WCL slots are not
  automatically safe to classify the same way

## Q13 Getter Fail-Contract Zone: Tahoe fixed-fail selectors stay out of the open tail

The next safe `Q13` reduction pass is another getter-only zone, but this time
it is about fixed fail contracts rather than hidden producers.

Recovered Tahoe fail-contract selectors:

- `AppleBCMWLANInfraProtocol::getCOUNTRY_CHANNELS_INFO(...)`
- `AppleBCMWLANInfraProtocol::getRANGING_ENABLE(...)`
- `AppleBCMWLANInfraProtocol::getRANGING_START(...)`
- `AppleBCMWLANInfraProtocol::getRANGING_CAPS(...)`
- `AppleBCMWLANInfraProtocol::getWCL_WNM_OFFLOAD(...)`
- `AppleBCMWLANInfraProtocol::getFW_CLOCK_INFO(...)`
- `AppleBCMWLANInfraProtocol::getTIMESYNC_STATS(...)`
- `AppleBCMWLANInfraProtocol::getHE_COUNTERS(...)`
- `AppleBCMWLANInfraProtocol::getSMARTCCA_OPMODE(...)`
- `AppleBCMWLANCore::getWIFI_NOISE_PER_ANT(...)`
- `AppleBCMWLANCore::getCHIP_COUNTER_STATS(...)`

The first nine selectors stay on direct Apple fail paths, so they belong in
slot-level classification comments, not in the open "missing producer" tail.
`getCHIP_COUNTER_STATS(...)` is slightly different: Tahoe does not expose a
normal producer there either, but its visible public contract is the fixed
Apple error `0xe00002e6`, not generic `kIOReturnUnsupported`.

That closes this zone as follows:

- the direct-`0xe00002c7` selectors are now documented at slot level so they
  do not drift back into the unsupported census
- `getWIFI_NOISE_PER_ANT(...)` leaves the open discrepancy queue because Apple
  itself is a direct unsupported stub there
- `getCHIP_COUNTER_STATS(...)` leaves the open queue because the port now
  returns the same fixed Tahoe fail code that the reference driver exposes

## Q13 Setter Carrier Zone: `MWS_*_WIFI_ENH` plus `NDD_REQ`

The next `Q13` zone closes ten Tahoe sideband setters at once:

- `setMWS_WIFI_TYPE_7_BITMAP_WIFI_ENH(...)`
- `setMWS_COEX_BITMAP_WIFI_ENH(...)`
- `setMWS_DISABLE_OCL_BITMAP_WIFI_ENH(...)`
- `setMWS_RFEM_CONFIG_WIFI_ENH(...)`
- `setMWS_ASSOC_PROTECTION_BITMAP_WIFI_ENH(...)`
- `setMWS_SCAN_FREQ_WIFI_ENH(...)`
- `setMWS_SCAN_FREQ_MODE_WIFI_ENH(...)`
- `setMWS_CONDITION_ID_BITMAP_WIFI_ENH(...)`
- `setMWS_ANTENNA_SELECTION_WIFI_ENH(...)`
- `setNDD_REQ(...)`

Recovered Apple behavior is split but still clean at the public contract layer:

- the nine `MWS_*_WIFI_ENH` selectors copy raw caller carriers into cached
  core-state fields and only then dispatch into Broadcom-private notifiers
- `setNDD_REQ(...)` is feature-gated and returns `0xe00002c7` whenever the
  `NearbyDeviceDiscoveryAdapter` owner is absent

That is enough to close the system-facing zone without guessing the private
notifier graph:

- the port now preserves the full caller-visible `MWS_*` carriers instead of
  dropping the selectors on generic unsupported
- `setNDD_REQ(...)` now exposes the recovered Tahoe fail path rather than a
  generic placeholder

## Q13 Minimal Setter-Contract Zone: mixed opaque carriers and fixed fail shapes

The next `Q13` zone closes fifteen setter slots that all share the same
pragmatic boundary: Tahoe already exposes a stable public contract for them,
but the hidden owner choreography is either feature-gated or still private.

Closed in this zone:

- `setAP_MODE(...)`
- `setDBG_GUARD_TIME_PARAMS(...)`
- `setPRIVATE_MAC(...)`
- `setTHERMAL_INDEX(...)`
- `setDYNAMIC_RSSI_WINDOW_CONFIG(...)`
- `setBSS_BLACKLIST(...)`
- `setREALTIME_QOS_MSCS(...)`
- `setRSN_XE(...)`
- `setGAS_ABORT(...)`
- `setWCL_LIMITED_AGGREGATION(...)`
- `setWCL_BCN_MUTE_CONFIG(...)`
- `setEAP_FILTER_CONFIG(...)`
- `setWCL_ASSOCIATED_SLEEP(...)`
- `setWCL_SOI_CONFIG(...)`
- `setOS_ELIGIBILITY(...)`

Recovered Apple behavior is consistent enough to lift this as one zone:

- several selectors are pure minimal contracts:
  `setGAS_ABORT`, `setWCL_LIMITED_AGGREGATION`
- several are opaque state carriers with only a null gate or a small public
  field split:
  `setDBG_GUARD_TIME_PARAMS`, `setDYNAMIC_RSSI_WINDOW_CONFIG`,
  `setBSS_BLACKLIST`, `setWCL_BCN_MUTE_CONFIG`, `setEAP_FILTER_CONFIG`,
  `setWCL_ASSOCIATED_SLEEP`, `setWCL_SOI_CONFIG`, `setRSN_XE`,
  `setOS_ELIGIBILITY`
- several expose fixed Tahoe fail shapes rather than generic unsupported:
  `setAP_MODE -> 0xe00002c7`, `setPRIVATE_MAC -> 0x16`,
  `setTHERMAL_INDEX -> 0xe00002bc`
- `setOFFLOAD_TCPKA_ENABLE(...)` remains feature-gated and uses the same
  visible fail/success split as the getter-side path

That closes the zone at the public Apple80211 surface:

- generic unsupported is removed from these fifteen setters
- caller-visible carriers are preserved locally where Apple preserves them in
  core state
- fixed Tahoe fail codes are now explicit where Apple exposes them

## Q13 Telemetry/Cache Getter Zone: public carriers without hidden owner lift

The next `Q13` zone closes fifteen getter slots that already expose a stable
Tahoe public contract even when the deeper Broadcom owner is still hidden.

Closed in this zone:

- `getDBG_GUARD_TIME_PARAMS(...)`
- `getAWDL_RSDB_CAPS(...)`
- `getTKO_PARAMS(...)`
- `getTKO_DUMP(...)`
- `getBTCOEX_PROFILE(...)`
- `getBTCOEX_PROFILE_ACTIVE(...)`
- `getMAX_NSS_FOR_AP(...)`
- `getBTCOEX_2G_CHAIN_DISABLE(...)`
- `getBSS_BLACKLIST(...)`
- `getTXRX_CHAIN_INFO(...)`
- `getMIMO_STATUS(...)`
- `getWCL_FW_HOT_CHANNELS(...)`
- `getWCL_TRAFFIC_COUNTERS(...)`
- `getRSN_XE(...)`
- `getWCL_LOW_LATENCY_INFO_STATS(...)`

Recovered Apple behavior splits into three public buckets:

- fixed-fail selectors:
  `getBTCOEX_PROFILE -> 0xe00002c2`,
  `getTKO_PARAMS/getTKO_DUMP -> 0xe00002bc` when the keepalive owner is absent
- compact cache-backed carriers:
  `getDBG_GUARD_TIME_PARAMS`, `getRSN_XE`, `getBSS_BLACKLIST`
- state-backed telemetry carriers:
  `getAWDL_RSDB_CAPS`, `getBTCOEX_PROFILE_ACTIVE`,
  `getMAX_NSS_FOR_AP`, `getBTCOEX_2G_CHAIN_DISABLE`,
  `getTXRX_CHAIN_INFO`, `getMIMO_STATUS`, `getWCL_FW_HOT_CHANNELS`,
  `getWCL_TRAFFIC_COUNTERS`, `getWCL_LOW_LATENCY_INFO_STATS`

This batch intentionally stops at the public Apple80211 boundary:

- unsupported headers are removed for these fifteen selectors
- exact Tahoe fail codes are preserved where Apple exposes failure directly
- caller-visible carriers are preserved from local cache/runtime state where
  Apple reads them from hidden owners or core-state fields

## Q13 First Confirmed Apple-Unsupported Setter Batch

The same pattern exists on a narrow setter subset: Apple does not expose a
producer there either, and the vendor-side infra method itself returns
`0xe00002c7`.

Recovered explicit unsupported setters:

- `AppleBCMWLANInfraProtocol::setROAM_PROFILE(...)`
- `AppleBCMWLANInfraProtocol::setROAM_CACHE_UPDATE(...)`
- `AppleBCMWLANInfraProtocol::setSET_WIFI_ASSERTION_STATE(...)`
- `AppleBCMWLANInfraProtocol::setMWS_ACCESSORY_POWER_LIMIT_WIFI_ENH(...)`
- `AppleBCMWLANInfraProtocol::setWOW_LOW_POWER_MODE(...)`
- `AppleBCMWLANInfraProtocol::setSTAND_ALONE_MODE_STATE(...)`
- `AppleBCMWLANInfraProtocol::setTIMESYNC_GPIO(...)`
- `AppleBCMWLANInfraProtocol::setFW_CLOCK_SOURCE(...)`
- `AppleBCMWLANInfraProtocol::setTIMESYNC_TX_POLICY(...)`
- `AppleBCMWLANInfraProtocol::setTIMESYNC_RX_POLICY(...)`
- `AppleBCMWLANInfraProtocol::setTIMESTAMPING_EN(...)`
- `AppleBCMWLANInfraProtocol::setMWS_TIME_SHARING_WIFI_ENH(...)`
- `AppleBCMWLANInfraProtocol::setSDB_ENABLE(...)`
- `AppleBCMWLANInfraProtocol::setBTCOEX_EXT_PROFILE(...)`
- `AppleBCMWLANInfraProtocol::setTX_MODE_CONFIG(...)`

One adjacent slot was a real port bug, not just a classification issue:

- `AppleBCMWLANInfraProtocol::setVOICE_IND_STATE(...)` is also a direct
  `return 0xe00002c7;`
- the Tahoe port had kept `[605] setVOICE_IND_STATE` as a validate+ack success
  stub
- that falsely advertised a producer path Apple does not provide, so the slot
  must be moved back to `kIOReturnUnsupported`

This setter zone is now closed as a zone-sized classification batch. The
following 15 setter slots are no longer carried as open producer debt inside
`Q13`:

- `[561] setROAM_PROFILE`
- `[583] setROAM_CACHE_UPDATE`
- `[585] setSET_WIFI_ASSERTION_STATE`
- `[607] setMWS_ACCESSORY_POWER_LIMIT_WIFI_ENH`
- `[630] setWOW_LOW_POWER_MODE`
- `[635] setSTAND_ALONE_MODE_STATE`
- `[641] setTIMESYNC_GPIO`
- `[643] setFW_CLOCK_SOURCE`
- `[644] setTIMESYNC_TX_POLICY`
- `[645] setTIMESYNC_RX_POLICY`
- `[646] setTIMESTAMPING_EN`
- `[648] setMWS_TIME_SHARING_WIFI_ENH`
- `[660] setSDB_ENABLE`
- `[661] setBTCOEX_EXT_PROFILE`
- `[663] setTX_MODE_CONFIG`

They remain `kIOReturnUnsupported` in the local header, but that is now an
intentional 1:1 Apple match with explicit slot comments rather than an
unclassified unsupported tail.

As with the getter batch, this does **not** generalize to neighboring WCL or
timesync setters. For example `setWCL_WNM_OFFLOAD(...)` and
`setWCL_WNM_OPS(...)` are real Apple producer paths and must not be collapsed
into unsupported just because nearby slots are.

## Q13 Confirmed Producer Mini-Batch: `getVHT_CAPABILITY`

`getVHT_CAPABILITY` is the next unsupported Tahoe getter with a complete
recoverable producer body.

Recovered Apple producer contract:

- `AppleBCMWLANCore::getVHT_CAPABILITY(apple80211_vht_capability*)`
  returns `0x2d` while the PHY capability gate at core-state `+0x400`
  is below `0x80`
- otherwise it copies a contiguous 14-byte payload from core-state
  `+0x428..+0x435` into the caller buffer starting at offset `+4`
- that means the local ABI must model a 14-byte VHT capability IE body after
  `version`, not a 16-byte synthetic layout:
  `u8 ie`, `u8 len`, `u32 vht_cap_info`,
  `u16 rx_mcs_map`, `u16 rx_highest`, `u16 tx_mcs_map`, `u16 tx_highest`

This is strong enough to lift the slot without guessing the whole adjacent
cluster:

- remove generic `kIOReturnUnsupported` from slot `[484]`
- keep the Apple-style unsupported return `0x2d` when VHT capability is not
  present
- fill the struct from locally available VHT capability state instead of
  leaving the IOC on the generic unsupported path

This batch does **not** automatically justify lifting `getHT_CAPABILITY`.
For HT, the currently recovered evidence is still only on the family-side
`IO80211PeerManager::getHtCapabilityIE(...)` consumer/helper path, not a clean
Apple vendor-side producer body.

## Q13 Confirmed Producer Mini-Batch: `getTHERMAL_INDEX` / `getPOWER_BUDGET`

`getTHERMAL_INDEX` and `getPOWER_BUDGET` are the next Tahoe getters with
recoverable Apple producer bodies that do not depend on the unresolved hidden
object path.

Recovered Apple producer contract:

- `AppleBCMWLANCore::getTHERMAL_INDEX(apple80211_thermal_index_t*)`
  writes a 32-bit scalar at caller offset `+4` from core-state base
  `*(param_1 + 0x128) + 0x0`
- `AppleBCMWLANCore::getPOWER_BUDGET(apple80211_power_budget_t*)`
  writes a 32-bit scalar at caller offset `+4` from core-state base
  `*(param_1 + 0x128) + 0x4`
- both producers return success directly; there is no hidden helper, WCL
  bulletin, or additional transport layer in between

That gives a sufficiently strong ABI recovery for the getter side:

- both payloads are 8-byte carriers
- offset `+0` is the standard `version`
- offset `+4` is the 32-bit payload value

This batch still does **not** justify lifting the setter side:

- `setTHERMAL_INDEX(...)` exists, but the recovered body is validation-heavy
  and ends on the Apple bad-argument path rather than a simple carrier write
- `getOFFLOAD_TCPKA_ENABLE(...)` remains unresolved because only the setter
  body is currently present in the vendor decompile

## Q13 Confirmed Producer Mini-Batch: `getGUARD_INTERVAL`

`getGUARD_INTERVAL` is also a recoverable Tahoe getter.

Recovered Apple producer contract:

- `AppleBCMWLANCore::getGUARD_INTERVAL(apple80211_guard_interval_data*)`
  rejects `NULL` with `0xe00002c2`
- otherwise it queries `"nrate"` from the commander/config path
- if that query returns success or `0xe00002e3`, Apple still completes the IOC
  and derives the output interval from the cached rate word
- when the cached rate word does not describe a recognized short-GI mode, the
  producer falls back to `800`

The important architectural point for Tahoe is that slot `[478]` is not a
generic unsupported IOC. It is a real producer with a deterministic fallback to
the long-guard interval when no cached short-GI encoding is available.

This still does **not** justify lifting `HT_CAPABILITY`: `getGUARD_INTERVAL`
only proves the current-rate-to-interval policy, not the full HT capability IE
producer path.

## Q13 Confirmed Producer Mini-Batch: `getHT_CAPABILITY` / `getPRIVATE_MAC` / `getOFFLOAD_TCPKA_ENABLE`

The next three Tahoe getters now have enough recovered producer-side evidence
to stop treating them as a generic unsupported bucket.

Recovered Apple producer contract for `getHT_CAPABILITY`:

- remote `otool -tvV` on `AppleBCMWLANCoreMac` shows
  `AppleBCMWLANCore::getHT_CAPABILITY(apple80211_ht_capability*)` first calls
  `updateHTAndVHTCapBasedOnHWSupport()`
- it then copies a contiguous `0x1c`-byte HT capability IE body from core-state
  offsets `+0x40c..+0x427` into caller offsets `+0x4..+0x1f`
- this matches the already recovered family-side
  `IO80211PeerManager::getHtCapabilityIE(...)` carrier layout, so slot `[481]`
  is a real producer and not a placeholder

Recovered Apple producer contract for `getPRIVATE_MAC`:

- `AppleBCMWLANCore::getPRIVATE_MAC(apple80211_private_mac_data*)` rejects
  `NULL` with raw `0x16`
- otherwise it writes an offset-accurate packed carrier:
  `+0x4` enable flag, `+0x8` 16-bit/32-bit scan state field,
  `+0xc` cached timeout, `+0x10..+0x15` first six-byte MAC,
  `+0x16..+0x1b` second six-byte MAC
- the exact semantic names of the two six-byte blobs are still not fully proven,
  so the safe 1:1 recovery here is the packed ABI and offset coverage, not
  invented field semantics

Recovered Apple producer contract for `getOFFLOAD_TCPKA_ENABLE`:

- remote `otool -tvV` shows
  `AppleBCMWLANCore::getOFFLOAD_TCPKA_ENABLE(apple80211_offload_tcpka_enable_t*)`
  rejects `NULL` with `0xe00002c2`
- it then applies the same feature/config gate already seen in the setter:
  feature bit `0x31`, config bit `+0xbe`
- if the keepalive object at core-state `+0x15a8` exists, Apple writes the
  cached enable bit from object offset `+0x1e1` into caller offset `+0x4`
- otherwise the producer returns `0xe00002c7`

The important Tahoe consequences are:

- `HT_CAPABILITY` can now be lifted as a real producer using the same local HT
  capability state that already feeds our 802.11 HT IE generator
- `PRIVATE_MAC` must stop being an opaque forward declaration and become a
  packed `0x1c` ABI carrier
- `OFFLOAD_TCPKA_ENABLE` cannot remain `typedef UInt`; the recovered getter and
  setter both prove it is a packed `version + u32 enabled` carrier

## Q5 Closure: remaining raw-6 getter paths removed

The next pass closes the remainder of the raw-`6` getter queue.

Recovered Apple evidence now covers the remaining helper split strongly enough:

- `AppleBCMWLANCore::getMCS_INDEX_SET(...)` delegates to
  `IO80211BssManager::getCurrentMCSSet(...)`
- `WCLConfigManager::getNOISE(...)` delegates to
  `IO80211BssManager::getCurrentNoise(short&)`
- recovered `AppleBCMWLANCore::getMCS(...)` is a cached scalar carrier, not an
  association-gated helper

That is enough to close the raw-fallback queue itself:

- `getMCS_INDEX_SET`
- `getNOISE`
- `getMCS`

now stop leaking raw POSIX `6` in both the Tahoe Skywalk path and the legacy
STA dispatcher.

`getTXPOWER` left the raw-`6` queue earlier, but at that point the exact
reference producer was still open under `Q13`. That source lift is now closed:

- `getTXPOWER` consumes a HAL-backed cached qtxpower byte instead of
  `ic_txpower`
- the public carrier now uses Apple unit/value semantics:
  `APPLE80211_UNIT_MW` plus the fixed lookup table from
  `0xffffff80016f3760`
- `getMCS_VHT` consumes cached `nrate` transport state instead of rebuilding
  from `ni_txmcs`, `ni_chw`, and NSS helpers

Intel-side source mapping used for this closure:

- `iwm`: `lq_sta.rs_drv.last_rate_n_flags` for `nrate`,
  `iwm_ba_notif::reduced_txp` for qtxpower
- `iwx`: `iwx_rs_update(...)` cache for `nrate`,
  `iwx_compressed_ba_notif::reduced_txp` for qtxpower

The legacy STA shadow path was updated in the same batch, so Tahoe and legacy
no longer drift on either getter.

## Q3 Closure: ready-state queue closed, hidden object exactness moved to Q13

The latest `+0x1510` xref pass shows that the hidden object is broader than the
original ready-state bug:

- slot `+0x9f8` carries `CoreWiFiDriverReadyKey`
- but the same object also services platform/ring property fetches (`+0x970`),
  board/chip identity queries (`+0xa30`), boot/fault paths, analytics, and
  failure reporting

That means the remaining open work is no longer a standalone ready-state queue.
`Q3` is closed at the observable producer/consumer boundary:

- key/value shape is recovered
- value type is corrected to `OSString("true"/"false")`
- publication stays on the interface-side IOService property surface that Tahoe
  consumers actually read

What remains open is the exact concrete class / full method map for the hidden
`+0x1510` object. That broader hidden-surface lift now belongs to `Q13`.

## Tahoe BSD Bridge vs Apple IOUC Route

Live `471f6f1` proved that Tahoe attach/discovery is no longer the blocker:

- the loaded kext matches the latest installed build
- `en0` reports `if_type=6`, `if_subfamily=3`
- `airportd` sees the interface and issues scan traffic
- `WCLScanManager` reaches `IDLE`

The remaining mismatch is lower and more architectural: the local Tahoe BSD
bridge still short-circuits external selectors that Apple routes through the
Skywalk IOUC path first.

Recovered `IO80211Family` wrappers show the Apple route for these GET paths:

- `SSID`   → `sendIOUCToWcl(..., 1,    0x28)`
- `CHANNEL`→ `sendIOUCToWcl(..., 4,    0x10)`
- `BSSID`  → `sendIOUCToWcl(..., 9,    0x0c)`
- `SCAN_RESULT` → `sendIOUCToWcl(..., 0x16, 0x0c)`

Only if that WCL route reports "not implemented" does Apple fall back to the
protocol implementation.

Our Tahoe bridge had already grown local helpers for those selectors, but by
handling them directly inside `AirportItlwmSkywalkInterface::processApple80211Ioctl()`
it bypassed `IO80211SkywalkInterface`'s own WCL/IOUC route.  That explains why
live `SSID/BSSID` still returned `0xe0822403` and `SCAN_RESULT` returned raw
`5` even after attach, scan-trigger, and `WCL_SCAN_DONE` were working: the
external consumers were not walking the same route as the Apple reference.

The correct parity fix is therefore not another readiness payload or scan-cache
workaround.  It is to leave those selectors unsupported in the local Tahoe BSD
bridge so that `super::processBSDCommand()` preserves the Apple IOUC-first path.

## Q7 Closure: adapter-plane WCL producers no longer collapse into inline success

The remaining `Q7` gap was the roam/bgscan half of the WCL plane:

- `setWCL_REASSOC`
- `setWCL_LEGACY_ROAM_PROFILE_CONFIG`
- `setWCL_ROAM_PROFILE_CONFIG`
- `setWCL_ARP_MODE`
- `setWCL_CONFIG_BG_MOTIONPROFILE`
- `setWCL_CONFIG_BG_NETWORK`
- `setWCL_CONFIG_BGSCAN`
- `setWCL_CONFIG_BG_PARAMS`

Recovered Apple paths show these are not disposable validate-and-ack slots.
They either:

- persist exact carrier/config payloads (`0x9c`, `0x60`, `0x23c`, `0x14`,
  `0x12c0`, `0x20`), or
- delegate into local-owner-equivalent actions we already have
  (`REASSOC_REQ`, cache-bgscan preparation, bgscan start/stop)

The port still lacks Apple's hidden roam/bgscan/keepalive helper objects, so
full helper choreography remains part of the broader hidden-owner surface. But
the concrete architectural mismatch that defined `Q7` is now removed:

- the remaining WCL adapter methods are out-of-line implementations, not inline
  success stubs
- null requests return Apple `0xe00002bc`
- recovered payloads are persisted in driver-owned state
- available local owner actions are exercised instead of being skipped

That closes `Q7` as a standalone queue. Any still-missing hidden helper
exactness now belongs under `Q13`, not under the old WCL adapter-stub bucket.

## Q13 Batch: sideband carriers continue to leave the unsupported/stub tail

The next `Q13` reduction pass was intentionally narrow: only recovered producer
paths with simple caller-visible carriers were lifted.

Recovered Apple evidence:

- `AppleBCMWLANCore::setPM_MODE(apple80211_pm_mode*)`
  forwards the dword at caller `+0x4` into
  `AppleBCMWLANNetAdapter::configurePM(...)`
- `AppleBCMWLANCore::setWCL_ROAM_USER_CACHE(apple80211_user_roam_cache*)`
  delegates into the roam adapter `cmdROAM_USER_CACHE(...)`; helper xrefs prove
  the caller-visible cache carries channel entries from `+0x0`, count at
  `+0x78`, and override state at `+0x7a`
- `AppleBCMWLANCore::setWCL_SET_SCAN_HOME_AWAY_TIME(scanHomeAndAwayTime*)`
  consumes a single dword and forwards it to the scan adapter owner

Those slots were previously either unsupported or inline success. They now:

- reject `NULL` with Apple `0xe00002bc`
- preserve the recovered caller-visible carrier state locally
- reuse already-lifted local owners where available (`setPOWERSAVE(...)` for
  `PM_MODE`)

This does not close `Q13`, but it removes another class of simple
state-carrier mismatches from its tail.

## Q13 Classification: non-Apple sideband selectors must not advertise success

The remaining inline-success tail was checked against both the local Tahoe
decompile corpus and the remote Ghidra output. No producer symbols were found
for:

- `setWCL_SET_ROAM_LOCK`
- `setHEARTBEAT`
- `setINTERFACE_SETTING`

That matters because `success` on those slots was strictly worse than explicit
unsupported: it advertised producer paths that the recovered Apple stack does
not expose. Those selectors are now classified out of the ack-only bucket and
return `kIOReturnUnsupported` until real Apple implementations are recovered.

## Q13 Batch: TIMESYNC and WNM no longer sit in the raw unsupported surface

Recovered Apple evidence now cleanly splits these selectors:

- `getTIMESYNC_INFO` is not a generic unsupported slot; Core routes it through
  the hidden `+0x1510` object at vtable offset `+0xad8`, and the recovered bus
  path shows the visible result is a 0x100-byte text report
- `getWCL_WNM_OFFLOAD` is a direct Apple `0xe00002c7` stub
- `setWCL_WNM_OPS` and `setWCL_WNM_OFFLOAD` are real producers with only one
  core-layer gate: `NULL -> 0xe00002bc`, otherwise delegate into WnmAdapter

The local port now follows that split:

- `getTIMESYNC_INFO` produces the same "engine not instantiated" style text
  report instead of returning generic unsupported
- `getWCL_WNM_OFFLOAD` remains explicit unsupported, matching Apple
- `setWCL_WNM_OPS` / `setWCL_WNM_OFFLOAD` preserve the recovered opaque caller
  blobs instead of leaving the slots unsupported

This closes the timesync-facing `+0xad8` hidden-object subpath. The broader
hidden `+0x1510` object method map still remains a separate `Q13` task.

## Q13 Batch: simple core-owned setter carriers leave the unsupported tail

The next `Q13` zone is the setter subset where Apple already exposes concrete
core producers and the caller-visible ABI is narrow enough to lift without
guessing hidden owner internals.

Recovered Apple producer contracts:

- `AppleBCMWLANCore::setWCL_ULOFDMA_STATE(...)`
  rejects `NULL` with `0xe00002bc` and forwards the first dword into the 11ax
  adapter at core `+0x15c8`
- `AppleBCMWLANCore::setMIMO_CONFIG(...)`
  rejects `NULL`, reads a single mode dword, and hands it to the MIMO-PS owner
- `AppleBCMWLANCore::setFACETIME_WIFICALLING_PARAMS(...)`
  rejects `NULL`, reads a single status dword, and forwards it into the
  WiFi-call policy owner
- `AppleBCMWLANCore::setDUAL_POWER_MODE(...)`
  rejects `NULL`, persists two signed dwords at core `+0x4d3c/+0x4d40`, and
  then re-enters tx-power-cap state handling
- `AppleBCMWLANCore::setCONGESTION_CTRL_IND(...)`
  is a pure bool carrier into core `+0x79d2`
- `AppleBCMWLANCore::setLMTPC_CONFIG(...)`
  rejects `NULL`, stores a single byte at core `+0x4594`, and then re-enters
  the LMTPC owner
- `AppleBCMWLANCore::setLE_SCAN_PARAM(...)`
  consumes a fixed `0x10` payload:
  byte `+0x0`, dwords `+0x4/+0x8/+0xc`

That is strong enough to move these slots out of the generic
`kIOReturnUnsupported` bucket:

- `setWCL_ULOFDMA_STATE`
- `setMIMO_CONFIG`
- `setFACETIME_WIFICALLING_PARAMS`
- `setDUAL_POWER_MODE`
- `setCONGESTION_CTRL_IND`
- `setLMTPC_CONFIG`
- `setLE_SCAN_PARAM`

The port still does not claim the deeper hidden owner choreography for 11ax,
WiFi-calling policy, tx-power-cap recalculation, or BTLE reporting. What
changes here is the architectural surface:

- these selectors are no longer dead unsupported slots
- Tahoe now preserves the same caller-visible carriers Apple exposes
- the remaining hidden-owner exactness stays open under the residual `Q13`
  hidden-helper zone instead of being conflated with missing selector bodies

## Q13 Batch: simple getter carriers continue to leave the unsupported tail

The next `Q13` zone was the getter subset where Apple already exposes narrow
producer bodies without requiring the unrecovered hidden `+0x1510` object map.

Recovered Apple producer contracts:

- `AppleBCMWLANCore::getHE_CAPABILITY(...)`
  returns `0x2d` when the HE capability gate rejects the request and otherwise
  writes only three sparse fields into a 0x24-byte opaque carrier:
  `+0xc = 0x0b00`, `+0xe = 0x26`, `+0x1a = 0xfffafffafffafffa`
- `AppleBCMWLANCore::getP2P_DEVICE_CAPABILITY(...)`
  zeroes the first byte of a one-byte carrier and only delegates into the NAN
  owner when that owner exists

That is strong enough to move both selectors out of the generic unsupported
bucket:

- `getHE_CAPABILITY`
- `getP2P_DEVICE_CAPABILITY`

The important architectural correction is not that Tahoe suddenly claims full
11ax or NAN feature parity. The correction is narrower and producer-shaped:

- `HE_CAPABILITY` now follows the Apple reject-vs-payload split instead of a
  dead `kIOReturnUnsupported` slot
- `P2P_DEVICE_CAPABILITY` now exposes the Apple zeroed fast-path when no NAN
  owner exists, which matches the current local architecture better than a
  fake unsupported return

## Q13 Closure: hidden `+0x1510` object surface is closed as a system-facing zone

The remaining hidden-object note was re-audited method-by-method instead of
being kept as a generic "unknown helper" bucket.

Recovered `+0x1510` uses split cleanly into two classes:

1. system-facing registry/property producers
2. Broadcom-internal boot/debug/factory helpers

The system-facing class is now fully modeled on the local Tahoe port:

- `+0x9f8`
  `AppleBCMWLANCore::signalDriverReady()` publishes
  `CoreWiFiDriverReadyKey = OSString(\"true\"/\"false\")`
- `+0x970`
  provider acquisition used by platform/ring/property fetch paths
- property fetch helpers reached through that object for:
  `wlan.6GHz.supported`,
  `wlan.ant-inefficiency-mitigation.enabled`,
  `wlan.externallypowered`,
  `wlan.adaptiveroaming.enabled`,
  `wlan.dfrts`,
  `wlan.ignore.mcast`,
  `wlan.ocl.enabled`
- `+0xad8`
  `getTIMESYNC_INFO` publication path, already closed earlier as the
  deterministic "engine not-instantiated" text report

The key correction in code is that these paths now route through the
interface-side registry facade (`fNetIf`) and its provider, rather than through
the controller object directly.

The remaining `+0x1510` methods that still appear in decompile xrefs do not
form part of the system-facing Apple80211 contract on our port. Their named
callers are Broadcom-private bring-up/debug infrastructure such as:

- chip-image allocation / validation / completion (`+0x880/+0x888/+0x890/+0x8a0/+0x8a8`)
- boot-failure / boot-state / halt reporting (`+0x940/+0x9f0/+0xaf8`)
- chip identity / secure-boot / internal-provider helpers (`+0xa30/+0xa38/+0xa40/+0xa58`)
- factory / test / timesync tooling (`+0x9a8/+0x9c0/+0x9c8/+0xa20/+0xac8/+0xad0`)

Those are real Apple methods, but they are Broadcom-internal owner surfaces,
not shared system expectations that Tahoe/WCL/airportd consume from our port.
Keeping them in the open queue was therefore mixing "missing Apple80211
producer contract" with "vendor-private firmware/debug implementation".

So the hidden `+0x1510` object is now closed as a queue:

- the system-visible producer/consumer obligations are modeled
- the remaining named xrefs are explicitly classified as Broadcom-private
  internal surface and no longer kept as unresolved system-contract debt

## Q13 Batch: LQM carrier zone leaves the unsupported tail

The next clean `Q13` zone was the LQM carrier surface:

- `getLQM_CONFIG`
- `setLQM_CONFIG`
- `getLQM_SUMMARY`
- `getLQM_STATISTICS`

This zone no longer depended on the hidden Broadcom producer transport. The
recovered Apple evidence was already strong enough at the family/core boundary:

- `IO80211LQMData::getLQM_CONFIG(...)` exposes a fixed `0x24` carrier and
  mirrors one interval value into the first three dwords
- `IO80211LQMData::setLQM_CONFIG(...)` validates only two legal primary
  intervals (`1000` / `5000`) and keeps the caller-visible ABI fixed
- `IO80211LQMData::getLQM_SUMMARY(...)` zeroes a fixed `0x15a0` summary blob
- `AppleBCMWLANCore::getLQM_CONFIG(...)` / `setLQM_CONFIG(...)` preserve the
  same public carrier shape while sourcing state from the vendor owner
- `AppleBCMWLANInfraProtocol::getLQM_STATISTICS(...)` is a direct
  `return 0xe00002c7;` stub on Tahoe

That is enough to make a narrow lift without guessing:

- `getLQM_CONFIG` now exposes the recovered `0x24` carrier instead of generic
  unsupported
- `setLQM_CONFIG` now follows the recovered Tahoe validation ranges before
  caching the public blob
- `getLQM_SUMMARY` now returns the fixed zeroed summary payload rather than an
  unsupported error
- `getLQM_STATISTICS` is no longer treated as an "unknown missing producer":
  it is explicitly classified as Apple-unsupported on Tahoe

This does not claim that the full hidden Broadcom LQM owner is lifted. The
architectural correction is narrower and system-facing:

- the public Apple80211 ABI for the LQM config/summary selectors now matches
  the recovered family/core contract, and the known BSD IOC reachability is
  restored for `LQM_CONFIG`
- the one selector that Apple does not implement (`getLQM_STATISTICS`) stays
  explicitly unsupported instead of being kept in the generic open bucket

## Q13 Batch: mixed setter control/programming zone leaves the unsupported tail

The next clean `Q13` zone is the Tahoe setter subset where the public Apple
contract is already clear even though the private owner behind it is not always
lifted. This zone closes the following selectors together:

- `setCHANNEL`
- `setTXPOWER`
- `setRATE`
- `setIBSS_MODE`
- `setOFFLOAD_ARP`
- `setGAS_REQ`
- `setRESET_CHIP`
- `setCRASH`
- `setRANGING_ENABLE`
- `setRANGING_START`
- `setTKO_PARAMS`
- `setOFFLOAD_TCPKA_ENABLE`
- `setHP2P_CTRL`
- `setSET_PROPERTY`
- `setSENSING_ENABLE`
- `setSENSING_DISABLE`
- `setDBRG_ENTROPY`

Recovered Apple evidence splits this zone into two families:

- real public producers plus one recovered public fail-contract:
  `setCHANNEL`, `setTXPOWER`, `setRATE`, `setIBSS_MODE`, `setOFFLOAD_ARP`,
  `setGAS_REQ`, `setTKO_PARAMS`, `setOFFLOAD_TCPKA_ENABLE`,
  `setSET_PROPERTY`, `setSENSING_DISABLE`
- internal-only trap / debug / hidden-owner selectors that must no longer be
  treated as "missing normal producers":
  `setRESET_CHIP`, `setCRASH`, `setRANGING_ENABLE`, `setRANGING_START`,
  `setHP2P_CTRL`, `setSENSING_ENABLE`, `setDBRG_ENTROPY`

The recovered public contracts are enough to remove the generic unsupported
surface without inventing private Broadcom semantics:

- `AppleBCMWLANCore::setCHANNEL(...)` rejects `NULL` with `0xe00002bc`,
  rejects channel ids `>= 0x100` with raw `0x16`, and otherwise preserves the
  caller-visible request before resolving the hidden chanspec path
- `AppleBCMWLANCore::setTXPOWER(...)` is a real producer that writes the
  public `qtxpower` carrier, not a direct unsupported selector
- `AppleBCMWLANCore::setRATE(...)` is a real producer that updates the cached
  `bg_rate` property instead of failing generically
- `AppleBCMWLANCore::setIBSS_MODE(...)` is a real producer with a visible
  success contract, even though the private proximity/NAN owner path is still
  Apple-private
- `AppleBCMWLANCore::setOFFLOAD_ARP(...)` copies the public IPv4/keepalive
  carrier into core state and rejects bad arguments with raw `0x16`
- `AppleBCMWLANCore::setGAS_REQ(...)` rejects `NULL` with `0xe00002c2` and
  otherwise delegates into the GAS owner path
- `AppleBCMWLANCore::setTKO_PARAMS(...)` is a keepalive-owner carrier setter:
  missing owner -> `0xe00002bc`, otherwise the six public dwords are copied
- `AppleBCMWLANCore::setSET_PROPERTY(...)` is a real delegated producer that
  runs through a gated property callback path, not a direct unsupported slot
- `AppleBCMWLANSensingAdapter::setSENSING_DISABLE(...)` is feature-gated and
  does not expose a generic unsupported contract

The trap/debug half of the zone also has recoverable public meaning:

- `AppleBCMWLANCore::setRESET_CHIP(...)` and `setDBRG_ENTROPY(...)` are
  trap-only debug selectors and must not remain in the open "missing producer"
  queue
- `AppleBCMWLANInfraProtocol::setCRASH(...)` exposes a visible `0x16 / 0x13 /
  owner-result` contract, not generic unsupported
- `AppleBCMWLANCore::setRANGING_ENABLE(...)` / `setRANGING_START(...)` switch
  into hidden ranging owners after a visible bad-argument gate; they are not
  ordinary unsupported setters
- `AppleBCMWLANSensingAdapter::setSENSING_ENABLE(...)` and
  `AppleBCMWLANCore::setHP2P_CTRL(...)` are internal trap-facing selectors,
  not missing public producers

This zone therefore closes on the public Apple boundary:

- the real public producer/carrier selectors now expose their recovered caller-
  visible gates instead of `kIOReturnUnsupported`
- the trap/debug selectors are classified out of the open discrepancy queue as
  internal-only Apple control paths rather than missing normal producer work
- the BSD IOCTL bridge now reaches the real setter bodies for the standard IOC
  subset (`CHANNEL`, `TXPOWER`, `RATE`, `IBSS_MODE`, `OFFLOAD_ARP`,
  `GAS_REQ`, `RESET_CHIP`, `CRASH`, `RANGING_*`, `TKO_PARAMS`,
  `OFFLOAD_TCPKA_ENABLE`)

## Q13 Batch: diagnostic/roam getter zone leaves the unsupported tail

The next `Q13` zone is the diagnostics / roaming / country-information cluster
that still sat on generic unsupported even though Tahoe already exposes a
recoverable public contract for it. This zone closes the following 15 slots:

- `getAWDL_PEER_TRAFFIC_STATS`
- `getPOWER_DEBUG_INFO`
- `getROAM_PROFILE`
- `getCOUNTRY_CHANNELS`
- `getHW_SUPPORTED_CHANNELS`
- `getTRAP_CRASHTRACER_MINI_DUMP`
- `getBEACON_INFO`
- `getCHIP_DIAGS`
- `getCUR_PMK`
- `getCOUNTRY_CHANNELS_INFO`
- `getSENSING_DATA`
- `getWCL_EXTENDED_BSS_INFO`
- `setVIRTUAL_IF_CREATE`
- `setBSS_BLACKLIST`
- `setREALTIME_QOS_MSCS`

Recovered Apple evidence splits this zone into three public classes:

- internal-only / trap / debug selectors that must not stay in the generic
  "missing producer" bucket:
  `getAWDL_PEER_TRAFFIC_STATS`, `getTRAP_CRASHTRACER_MINI_DUMP`,
  `getBEACON_INFO`
- concrete caller-visible carriers:
  `getPOWER_DEBUG_INFO`, `getROAM_PROFILE`, `getCOUNTRY_CHANNELS`,
  `getHW_SUPPORTED_CHANNELS`, `getCHIP_DIAGS`, `getCUR_PMK`,
  `getCOUNTRY_CHANNELS_INFO`, `getSENSING_DATA`
- delegated owner-backed selectors whose public null/fail contract is still
  recoverable even when the private owner stays unlifted:
  `getWCL_EXTENDED_BSS_INFO`, `setVIRTUAL_IF_CREATE`,
  `setBSS_BLACKLIST`, `setREALTIME_QOS_MSCS`

Public Apple-side facts used for this zone:

- `IO80211InfraProtocol_vtable_25D125.txt` marks slot `[470]` as an
  `AWDL internal stub`, which is enough to classify it out of the open
  public-system discrepancy queue
- `AppleBCMWLANCore::getPOWER_DEBUG_INFO(...)` zeroes the public leading qword
  and copies a fixed `0x2c0` telemetry snapshot from core state
- `AppleBCMWLANCore::getROAM_PROFILE(...)` writes the three per-band metadata
  words at offsets `+0x4/+0x84/+0x104` and marks successful band payloads at
  `+0xc + band*0x80`
- `AppleBCMWLANCore::getCOUNTRY_CHANNELS(...)` is a fixed zero-fill trap path
  over a `0x12d8` public carrier
- `APPLE80211_IOC_HW_SUPPORTED_CHANNELS` belongs to the same Skywalk BSD bridge
  family as `SUPPORTED_CHANNELS`, so leaving it on a dead header stub after the
  bridge restoration was architectural drift
- `AppleBCMWLANCore::getTRAP_CRASHTRACER_MINI_DUMP(...)` zero-fills the public
  crashtracer blob at `caller+0x4` for `0x19000` bytes
- `AppleBCMWLANCore::getBEACON_INFO(...)` and `getAWDL_PEER_TRAFFIC_STATS(...)`
  are trap/internal selectors, not missing normal producers
- `AppleBCMWLANCore::getCHIP_DIAGS(...)` drives a fixed-size diagnostic carrier
  through a gated callback path rather than returning generic unsupported
- `AppleBCMWLANCore::getCUR_PMK(...)` defaults to `0xe00002c7` and only opens
  deeper debug/association subpaths under Apple-only gates
- `AppleBCMWLANCore::getSENSING_DATA(...)` writes `version=1` and exposes the
  public fail split `0xe0822801` / `0xe00002c7`
- `AppleBCMWLANCore::getWCL_EXTENDED_BSS_INFO(...)` exposes `NULL -> 0xe00002bc`
  before delegating to the net adapter owner
- `AppleBCMWLANCore::setVIRTUAL_IF_CREATE(...)` is not a generic unsupported
  setter: Tahoe exposes role-dependent public failures before the hidden
  proximity/AWDL/NAN owners take over
- `AppleBCMWLANCore::setBSS_BLACKLIST(...)` and
  `setREALTIME_QOS_MSCS(...)` were already lifted in code, so keeping them in
  the open unsupported queue was just stale documentation debt

This zone closes on the system-facing boundary:

- the diagnostic/country getters no longer advertise generic unsupported when
  Apple already exposes a fixed public carrier or trap contract
- the restored `getCOUNTRY_CHANNELS_INFO` / `getHW_SUPPORTED_CHANNELS` routes
  keep Tahoe aligned with the same bridge-restoration principle used earlier
- `setBSS_BLACKLIST` and `setREALTIME_QOS_MSCS` are now counted correctly as
  closed because their public setter contracts had already been lifted

Final `Q13` close-out:

- the remaining raw header stubs are no longer treated as unsupported-surface
  debt
- `getSYSTEM_SLEEP_CONFIG`, `setWOW_TEST`, `setPOWER_BUDGET`,
  `setUSB_HOST_NOTIFICATION`, `setHOST_CLOCK_INFO` belonged to `Q12` and are
  now closed as a batch
- `getHP2P_CTRL`, `getDYNSAR_DETAIL`, `getSLOW_WIFI_FEATURE_ENABLED`,
  `getWCL_LOW_LATENCY_INFO`, `getWCL_GET_TX_BLANKING_STATUS`, `setIE`,
  `setHT_CAPABILITY`, `setOFFLOAD_NDP`, `setVHT_CAPABILITY`,
  `setRANGING_AUTHENTICATE`, `setBTCOEX_PROFILE`,
  `setBTCOEX_PROFILE_ACTIVE`, `setBTCOEX_2G_CHAIN_DISABLE`,
  `setWCL_ACTION_FRAME`, `setBYPASS_TX_POWER_CAP`,
  `setWCL_UPDATE_FAST_LANE`, `setTRAFFIC_ENG_PARAMS` belong to `Q11`
- `getLEAKY_AP_STATS_MODE`, `getTRAP_INFO`, `setLEAKY_AP_STATS_MODE` are
  Broadcom-private diagnostics/test surfaces and are no longer carried as
  shared Apple80211 contract debt

With that reclassification, `Q13` is closed as a queue: all remaining raw
unsupported slots are either queued under their owning architectural stages or
explicitly marked internal-only.

`Q11` broad queue close-out:

- the old umbrella `Q11 Skywalk Datapath / Queue Surface` was too wide and was
  masking three distinct owner families: management/frame injection, radio/coex
  programming, and nearby/low-latency traffic policy
- the remaining `Q11` debt is now tracked only through those owner-based
  subqueues, not through a single broad unsupported bucket

`Q11-A/B/C` close-out:

- `Q11-A` is no longer tracked as one mixed management bucket; it is split into
  the controller-branch IE owner mismatch (`Q11-A1`) and the net-adapter
  action-frame injector (`Q11-A2`)
- `Q11-B` is no longer tracked as one mixed radio bucket; it is split into
  capability programming (`Q11-B1`), coexistence programming (`Q11-B2`), and
  tx-power policy (`Q11-B3`)
- `Q11-C` is no longer tracked as one mixed nearby/traffic bucket; it is split
  into HP2P/DynSAR helpers (`Q11-C1`), low-latency/slow-wifi status (`Q11-C2`),
  and nearby/ranging/traffic policy (`Q11-C3`)

So `Q11-A/B/C` are closed as queues. What remains open is only owner-level
subqueue work.

Final `Q11` exhaustion:

- `Q11-A1` is reduced to a controller-branch split:
  Tahoe Skywalk sits on `AirportItlwmV2.hpp`, while the existing IE owner lives
  on the legacy controller branch in `AirportItlwm.hpp`
- `Q11-A2` is reduced to the dedicated net-adapter frame injector already
  visible in `AppleBCMWLANCore::setWCL_ACTION_FRAME(...)`
- `Q11-B1/B2/B3` are reduced to one radio-policy owner family:
  capability programming, coexistence programming, and tx-power policy
- `Q11-C1/C2/C3` are reduced to one hidden proximity/low-latency owner family:
  HP2P/DynSAR helpers, slow-wifi / tx-blanking state, and nearby/ranging
  traffic policy

Therefore `Q11` is exhausted fully as a queue hierarchy. Remaining work is
tracked only as owner-specific implementation debt, not as open `Q11` queues.

Targeted owner-family decompile findings (2026-04-13):

- producer/consumer wrappers are now recovered end-to-end for the remaining
  owner-specific families:
  `apple80211* -> AppleBCMWLANInfraProtocol::* -> AppleBCMWLANCore::*`
- the `IO80211Family` side is structurally uniform for this batch:
  `apple80211getHP2P_CTRL`, `apple80211getDYNSAR_DETAIL`,
  `apple80211getSLOW_WIFI_FEATURE_ENABLED`,
  `apple80211getWCL_LOW_LATENCY_INFO`,
  `apple80211getWCL_GET_TX_BLANKING_STATUS`,
  `apple80211getSYSTEM_SLEEP_CONFIG`, `apple80211setIE`,
  `apple80211setWOW_TEST`, `apple80211setHT_CAPABILITY`,
  `apple80211setOFFLOAD_NDP`, `apple80211setVHT_CAPABILITY`,
  `apple80211setRANGING_AUTHENTICATE`, `apple80211setBTCOEX_PROFILE`,
  `apple80211setBTCOEX_PROFILE_ACTIVE`,
  `apple80211setBTCOEX_2G_CHAIN_DISABLE`, `apple80211setPOWER_BUDGET`,
  `apple80211setUSB_HOST_NOTIFICATION`, `apple80211setWCL_ACTION_FRAME`,
  `apple80211setBYPASS_TX_POWER_CAP`
- these family wrappers first run the selector gate through vtable slot
  `+0xcc8`; if that succeeds and the interface is `IO80211InfraProtocol`,
  they dispatch to the corresponding InfraProtocol slot; otherwise they return
  `0xe082280e`

Recovered producer split for the remaining owner-specific families:

- direct public carriers already recoverable without hidden owners:
  `AppleBCMWLANCore::getDYNSAR_DETAIL`,
  `AppleBCMWLANCore::getSLOW_WIFI_FEATURE_ENABLED`,
  `AppleBCMWLANCore::getWCL_LOW_LATENCY_INFO`,
  `AppleBCMWLANCore::getWCL_GET_TX_BLANKING_STATUS`,
  `AppleBCMWLANCore::getSYSTEM_SLEEP_CONFIG`,
  `AppleBCMWLANCore::setWOW_TEST`,
  `AppleBCMWLANCore::setHT_CAPABILITY`,
  `AppleBCMWLANCore::setVHT_CAPABILITY`,
  `AppleBCMWLANCore::setPOWER_BUDGET`,
  `AppleBCMWLANCore::setUSB_HOST_NOTIFICATION`,
  `AppleBCMWLANCore::setBYPASS_TX_POWER_CAP`,
  `AppleBCMWLANCore::setTRAFFIC_ENG_PARAMS`
- net-adapter / join-adapter / commander-backed producers with concrete Apple
  owner bodies now visible:
  `AppleBCMWLANCore::setIE`,
  `AppleBCMWLANCore::setOFFLOAD_NDP`,
  `AppleBCMWLANCore::setBTCOEX_PROFILE`,
  `AppleBCMWLANCore::setBTCOEX_PROFILE_ACTIVE`,
  `AppleBCMWLANCore::setBTCOEX_2G_CHAIN_DISABLE`,
  `AppleBCMWLANCore::setWCL_ACTION_FRAME`,
  `AppleBCMWLANCore::setRANGING_AUTHENTICATE`
- hidden-owner trampolines remain present in Apple for these selectors, but
  they are no longer tracked as open pre-`Q12` debt: the local port now mirrors
  the recovered Apple-visible gates, payload ABI, state carriers, and public
  fail shapes, so the remaining backend choreography sits behind already
  satisfied system-facing contracts

Specific recovered Apple contracts that materially narrow the remaining work:

- `AppleBCMWLANCore::getDYNSAR_DETAIL(...)` is not a generic hidden getter:
  it is a caller-visible versioned carrier with
  `NULL/out-of-range -> 0x16`, `version=1`, bank selection by `param_1[4]`,
  and a fixed `0x2d00` copy per bank
- `AppleBCMWLANCore::getSLOW_WIFI_FEATURE_ENABLED(...)` is a trivial
  `NULL -> 0xe00002c2`, success writes `enabled` from core `+0x7569`
- `AppleBCMWLANCore::getWCL_LOW_LATENCY_INFO(...)` is a trivial
  `NULL -> 0xe00002bc`, success reads three fields from owner `+0x2c28`
- `AppleBCMWLANCore::getWCL_GET_TX_BLANKING_STATUS(...)` is a trivial carrier:
  it exposes bit `+0x4ce8 & 1`
- `AppleBCMWLANCore::getSYSTEM_SLEEP_CONFIG(...)` is no longer opaque:
  it combines Bonjour offload state with a hidden `+0x1510` callback at
  slot `+0x850`
- `AppleBCMWLANCore::setIE(...)` already contains the real Apple split between
  `JoinAdapter::setCustomAssocIE(...)` and generic `setVendorIE(...)`
- `AppleBCMWLANCore::setWCL_ACTION_FRAME(...)` is a concrete net-adapter path,
  not a placeholder: it chooses `sendActionFrame` vs `sendActionFrameV2`
  from core state `+0x30c`
- the local Tahoe port now mirrors the recovered public contracts for
  `setIE`, `setOFFLOAD_NDP`, `setBTCOEX_PROFILE`,
  `setBTCOEX_PROFILE_ACTIVE`, `setBTCOEX_2G_CHAIN_DISABLE`,
  `setWCL_ACTION_FRAME`, and the visible gate for
  `setRANGING_AUTHENTICATE`; what remains is the exact hidden owner body
  behind those wrappers, not generic unsupported surface
- `AppleBCMWLANCore::setTRAFFIC_ENG_PARAMS(...)` is a feature-gated public
  contract, not a deep owner body: `NULL -> 0xe00002bc`, feature bit
  `+0x7584` set -> success, else `0xe00002c7`
- `AppleBCMWLANCore::setHOST_CLOCK_INFO(...)` did not recover as a real core
  producer in this batch; on the protocol side the visible Apple contract is a
  direct `0xe00002c7`
- `AppleBCMWLANCore::getHP2P_CTRL(...)` is not a normal scalar getter:
  it allocates command/response buffers, checks hidden `+0x1510` slot `+0xbf0`,
  then issues IOVAR `"hp2p"` through commander `+0x1520`; this confirms HP2P
  belongs to the hidden proximity owner family, not to a simple state-carrier
  queue
- the port now also mirrors the exact Apple-visible fail path for
  `getHP2P_CTRL`: `NULL -> 0xe00002bc`, support gate false -> `0xe00002c7`

With `getHP2P_CTRL` aligned, the remaining pre-`Q12` owner-specific debt is
exhausted. What is left in this area is private backend choreography behind
already matched public contracts, not an unresolved Apple80211 system surface.

## Q12 Close-Out

`Q12 Sleep / Wake / Reset / Teardown` is now closed at the Apple-visible
contract level:

- `getSYSTEM_SLEEP_CONFIG` already matched the recovered owner-missing fail
  shape `0xe00002bc`
- `setWOW_TEST` already matched the recovered public 1..600 gate
- `setPOWER_BUDGET` already matched the recovered feature/range gate
- `setUSB_HOST_NOTIFICATION` already preserved the public dword carrier
- `setHOST_CLOCK_INFO` is confirmed by decompile as a direct
  `AppleBCMWLANInfraProtocol::setHOST_CLOCK_INFO -> 0xe00002c7` stub

That leaves no remaining top-level queue debt after `Q12`; only runtime
verification remains.

## Owner-Family Batch Tooling And Post-Q12 Tightening

To stop doing one-off targeted Ghidra passes manually, the repo now carries a
single batch launcher:

- [scripts/decompile_owner_family_batch.sh](/Users/bob/Projects/itlwm/scripts/decompile_owner_family_batch.sh)
- [scripts/ghidra/DecompileOwnerFamilyBatch.py](/Users/bob/Projects/itlwm/scripts/ghidra/DecompileOwnerFamilyBatch.py)

It lifts the 13 owner families in one run against the remote host and writes:

- one file per family under `core/`
- one file per family under `io80211/`
- one shared `manifest.txt` with exact symbol matches

This does not by itself close hidden backend parity, but it removes the last
manual/repetition excuse for the remaining owner-side work.

The first immediate tightening pass after adding that tooling closed a few
still-visible state drifts without inventing hidden commander behavior:

- `getBTCOEX_PROFILE_ACTIVE(...)` now returns the dedicated
  `btc_profile_active` cache instead of reusing coarse `btcMode`
- `setBTCOEX_PROFILE(...)` now stores the full Apple-shaped per-profile table
  entry by `profileIndex` instead of collapsing everything into one last-seen
  blob
- `setWOW_TEST(...)` now mirrors Apple's externally visible retry/enable
  semantics more closely by treating success as a WoW-enabled state transition,
  not just a scalar cache write

The deeper hidden owner bodies behind `USB_HOST_NOTIFICATION`, `OFFLOAD_NDP`,
`BTCOEX_*` commander traffic, ranging auth, and tx-power-cap bypass still
remain backend-parity work, not queue debt.

Two more public-path mismatches also fell out of the batch bodies and are now
closed:

- `setOFFLOAD_NDP(...)` must not depend on local BSD/`fNetIf` attachment; the
  recovered Apple gate is only `NULL -> 0x16`, then the owner-specific path
  hangs off the hidden IPv6/NDP owner at core `+0x2c20`
- `setWCL_ACTION_FRAME(...)` must reject oversized payloads with the same
  Tahoe `0xe00002bc` visible fail shape that both
  `AppleBCMWLANNetAdapter::sendActionFrame(...)` and `sendActionFrameV2(...)`
  expose before adapter injection

One more owner-family public-path mismatch is now closed:

- `setIE(...)` must not reject `ie_len == 0`; the recovered Apple producer only
  rejects `NULL` and `ie_len > 0x800`, and the assoc-request/WAPI branch is
  taken only when `ie_len != 0` and `ie[0] == 0x44`

## Tahoe Commander Engineering Batch

The first internal compatibility stack is now in-tree:

- [AirportItlwm/TahoeErrorMap.hpp](/Users/bob/Projects/itlwm/AirportItlwm/TahoeErrorMap.hpp)
- [AirportItlwm/TahoeOwnerRegistry.hpp](/Users/bob/Projects/itlwm/AirportItlwm/TahoeOwnerRegistry.hpp)
- [AirportItlwm/TahoePayloadBuilders.hpp](/Users/bob/Projects/itlwm/AirportItlwm/TahoePayloadBuilders.hpp)
- [AirportItlwm/TahoeAsyncCommandContext.hpp](/Users/bob/Projects/itlwm/AirportItlwm/TahoeAsyncCommandContext.hpp)
- [AirportItlwm/TahoeCommander.hpp](/Users/bob/Projects/itlwm/AirportItlwm/TahoeCommander.hpp)

This is intentionally sync-only and header-only for the first pass, so the
Tahoe path can start using an owner-oriented internal layer without waiting on
project-file/xcodeproj churn.

The first migrated owner families are:

- `setUSB_HOST_NOTIFICATION`
- `setBTCOEX_PROFILE_ACTIVE`
- `setBTCOEX_2G_CHAIN_DISABLE`
- `setBYPASS_TX_POWER_CAP`

`setDUAL_POWER_MODE` now also feeds the new owner registry because Apple
`sendTxPowerCapBypassToFirmware()` gates on the dual-power state at
core `+0x4d3c/+0x4d40`.

This does not yet claim full backend parity for those selectors. What it does
close is the architectural drift where Tahoe Skywalk handlers owned both public
validation and hidden-owner state themselves. Those four selectors now flow
through a dedicated commander/registry/payload path.

## New Live Root After Exact Seven-File Probe Runtime

The exact seven-file Tahoe probe diff reviewed in `CR-026` booted successfully
and moved the remaining live blocker one step deeper than the older
`current-link getter plane` wording.

Live `2026-04-19 15:20:18 EEST` association evidence now shows:

- `airportd` issues `BEGIN REQ [ASSOC]`
- kernel logs only hidden family exits:
  - `(IO80211Family) Exit-setASSOCIATE:153 ret:-536870201`
  - repeated twice in the same join window
- the same window contains no local hits for:
  - `processBSDCommand(...)`
  - `processApple80211Ioctl(...)`
  - `setASSOCIATE(...)`
  - `setWCL_ASSOCIATE(...)`
  - `setAUTH_TYPE(...)`
  - `setRSN_IE(...)`
  - `associateSSID(...)`
- the same window contains hidden WCL abort activity only:
  - `leaveNetworkCommand@2345 ... <setDISASSOCIATE> ... reason=<8>`
  - later `reason=<10>`
- `airportd` then collapses to:
  - `Failed to associate ... -536870201`
  - `driver not available`
  - downstream `INVALID_AKMS`

Primary-source decomp from the remote host tightens the ownership mismatch:

- `AppleBCMWLANCore::setWCL_ASSOCIATE(apple80211AssocCandidates*)` programs
  `IO80211BssManager` auth context and SSID, then calls
  `AppleBCMWLANJoinAdapter::performJoin(...)`
- the same join owner also controls:
  - `abortFirmwareJoinSync(bool)`
  - `getBSSInfoAsync()`
  - `sendConnectComplete()`
- `sendConnectComplete()` is the recovered producer for
  `APPLE80211_M_WCL_CONNECT_COMPLETE_EVENT (0xD5)`

That means the remaining live failure is no longer best described as only a
getter/fallback leak. The narrower current root is an association-owner
mismatch:

- Apple Tahoe runs the active family `setASSOCIATE` plane through a
  controller/core-local `setWCL_ASSOCIATE -> JoinAdapter` owner
- the local port still implements association only on the
  interface/net80211 side (`AirportItlwmSkywalkInterface::setASSOCIATE(...)`
  and `setWCL_ASSOCIATE(...)`)
- so the real family-visible `setASSOCIATE` path exits with `-536870201`
  before any local join/auth/RSN path is entered

The next reverse target is therefore not generic `associateSSID()` debugging.
It is the exact hook between the active family `setASSOCIATE` path and the
Apple controller/core join owner, plus the completion bridge from that owner
back into WCL-visible state.

`2026-04-19` narrow system-contract fix after that root:

- Apple’s `AppleBCMWLANSkywalkInterface::getController()` returns the bound
  controller out of interface-side state
- inherited `IO80211SkywalkInterface::getController()` in family decomp is only
  a field read from `*(this + 0x18) + 0xba8`
- recovered `IO80211Family` notes show slot `[375]` / `0xba8`
  (`IO80211SkywalkInterface::getController()`) feeding controller-side execute
  and report paths
- the local Tahoe port already cached the controller in
  `AirportItlwmSkywalkInterface::instance` during `bindController(...)`, but it
  does not populate the inherited family storage path anywhere in the visible
  Tahoe bring-up; the explicit local binding exists only in `instance`
- the new narrow runtime diff adds `AirportItlwmSkywalkInterface::getController()`
  and returns `instance`

This is not claimed as the final closeout for `TAHOE-ASSOC-OWNER-MISMATCH-002`.
The narrower claim is only that Tahoe’s family-visible controller producer slot
must return the controller object already bound by the local split
`init() + bindController(...)` construction path, matching Apple’s explicit
interface-local controller lookup on the same seam.

After reboot on the exact `CR-029` runtime, that narrower claim is now
confirmed in live behavior:

- loaded kext UUID is `E8B26773-A881-3B69-BF47-2B113B75BD8C`
- networks are visible in the UI again
- `wdutil` shows `en0`, `Power: On`, and non-zero scan cache

But the broader association root is still open. Manual association at
`2026-04-19 17:40:24 EEST` now reaches a concrete candidate network with
`hasPassword=1` and RSN auths `{ psk psk_sha256 sae }`, yet still fails with
`-536870201`, then disassociates with reasons `8` and `10`, and later gets
policy-labeled `INVALID_AKMS`.

So `getController()` is now best classified as a runtime-confirmed visibility
and controller-binding seam fix, not as the missing final association closeout.
The next reverse target is now narrower than that older wording.

Primary-source family decomp identifies the live Tahoe association carrier as a
hidden large-blob path. This is the new narrower root
`TAHOE-HIDDEN-ASSOC-CARRIER-004`:

- `IO80211Family::FUN_ffffff80022080ef @ 0xffffff80022080ef`
  validates `req_len == 0x3ad8`
- then tries
  `sendIOUCToWcl(..., 0x802869c8, 0x45, payload, 0x3ad8, ...)`
- if WCL does not absorb the request, fallback helper
  `FUN_ffffff80021e82ef @ 0xffffff80021e82ef` does not dispatch to public
  `IO80211InfraProtocol::setWCL_ASSOCIATE(...)`
- it only consults interface slot `+0xcc8`, and
  `IO80211_vtables_BootKC_26.2_25C56.txt` maps that slot to
  `[411] IO80211SkywalkInterface::isCommandProhibited(int)`

That matches the runtime evidence precisely:

- live post-reboot association still logs `-536870201`
- the same window still has no local hits for `setASSOCIATE(...)`,
  `setWCL_ASSOCIATE(...)`, `setAUTH_TYPE(...)`, `setRSN_IE(...)`,
  `associateSSID(...)`

So the broader root is no longer just "wrong local association owner". The
exact live Tahoe join is carried by hidden selector `0x45` with payload
`0x3ad8`, and family fallback does not bridge that carrier into the local
public owner at all. The next reverse target is therefore the hidden
`0x45/0x3ad8` association carrier plus the completion/cache bridge above
`JoinAdapter`, not more debugging inside public `setWCL_ASSOCIATE(...)`.

## Narrower Runtime Root Inside The Hidden Assoc Queue

`2026-04-19` recovered a second, more concrete runtime-bearing mismatch inside
that still-open Tahoe association queue:
`TAHOE-CARD-CAP-ABI-MISMATCH-005`.

Apple Tahoe does not use the short post-Sonoma capability blob here. The
primary-source `AppleBCMWLANCore::getCARD_CAPABILITIES(...)` producer writes the
public carrier through byte offset `+0x17`, and the same body stores advanced
capability bits in that tail, including WPA3-related state such as
feature-flag `0x41 -> *(cap + 0x0d) |= 0x08`.

The local Tahoe port still declared
`apple80211_capability_data.capabilities[14]` for all `__MAC_14_0+` targets.
So on Tahoe the local struct was only `0x12` bytes total, while the Apple
producer-visible carrier is `0x1c`. That means
`AirportItlwm::getCARD_CAPABILITIES(...)` zeroed only the short prefix and left
the Apple-visible tail bytes uninitialized.

That matters specifically for the current association failure because the hidden
join path is still the active owner, and the post-`CR-029` runtime fails on a
`wpa3-transition` network with downstream policy `INVALID_AKMS`. A truncated
capability ABI can therefore leak arbitrary advanced AKM / WPA3 support bits
into the exact hidden path that never re-enters the local `setAUTH_TYPE(...)`
WPA3-downgrade shim.

The local fix is intentionally narrow:

- Tahoe `apple80211_capability_data` is lifted to the Apple-sized `0x1c`
  carrier (`version + 24 capability bytes`)
- `AirportItlwm::getCARD_CAPABILITIES(...)` now enforces that ABI with a Tahoe
  `static_assert`
- the existing conservative producer path now zeroes the full Tahoe carrier, so
  the advanced tail no longer leaks allocator garbage into `IO80211Family/WCL`

This does not replace `TAHOE-HIDDEN-ASSOC-CARRIER-004` as the broader queue.
It is the first concrete runtime-bearing bug found inside that queue that can be
fixed without inventing the hidden `0x45/0x3ad8` owner.

After rebooting into the `CR-031` runtime, the capability tail stopped leaking
allocator garbage, but the same `wpa3-transition` join still failed with
`-536870201` and downstream policy `INVALID_AKMS`. The next narrower mismatch
is no longer carrier size but carrier content: the local Tahoe producer still
hard-coded `cap[2]=0xEF`, `cap[3]=0x2B`, `cap[6]=0x8C`, while the Apple
producer never sets `cap[2] bit 0x80`, `cap[3] bit 0x08`, or `cap[6] bit 0x80`
at all. Those are not "optional missing Apple features"; they are Apple-
impossible bits on those exact indices.

That matters because the hidden association plane is still the active owner and
still bypasses the local WPA3 downgrade shim in `associateSSID(...)`. Once the
carrier tail became deterministic, the next remaining way to over-advertise
unsupported AKM state into the same queue was the hard-coded capability content
itself. The local fix is correspondingly narrow: sanitize the Tahoe capability
cluster to the Apple-consistent shape `cap[2]=0x6F`, `cap[3]=0x27`,
`cap[6]=0x0C` and keep the broader hidden `0x45/0x3ad8` owner recovery out of
scope for this diff.

After rebooting into the exact `CR-032` runtime, that capability-content fix
did not change the active failure signature:

- the loaded kext UUID advanced to `51294DB5-F5EB-3926-B4F1-C987D6159662`
- networks remained visible in UI
- the same manual join on the `wpa3-transition` network still failed with
  `-536870201`
- downstream policy still labeled the disconnect as `INVALID_AKMS`

The next narrower live seam is not another public RSN helper. It is the hidden
interface command gate directly above the `0x45/0x46` carrier:

- `IO80211Family::FUN_ffffff80022080ef` still routes the active large assoc
  payload through `sendIOUCToWcl(..., 0x45, ..., 0x3ad8)`
- if that path is not absorbed, fallback `FUN_ffffff80021e82ef` reaches only
  interface slot `+0xcc8`
- `IO80211_vtables_BootKC_26.2_25C56.txt` maps that slot to
  `[411] IO80211SkywalkInterface::isCommandProhibited(int)`
- Apple Broadcom bring-up explicitly enables hidden commands `0x45` and `0x46`
  through `FUN_ffffff800160190a(param_1, 0x45)` and
  `FUN_ffffff800160190a(param_1, 0x46)`
- the local Tahoe port had only a controller-side
  `AirportItlwm::isCommandProhibited(int)` stub; the interface-side slot was
  left on inherited behavior, and live runtime never hit the controller slot

This is the new narrower root:
`TAHOE-HIDDEN-ASSOC-CMD-GATE-007`.

The local fix is intentionally tight:

After rebooting into the exact `CR-033` runtime, the interface gate change
proved directionally correct for hidden `0x45` / `0x46`, but still wrong on
owner selection for ordinary commands. The loaded kext UUID advanced to
`33A464B2-FF46-314C-8CC5-4B116DEA3D38`, yet startup regressed before any new
association evidence appeared:

- `wdutil info` reported `No Wi-Fi hardware installed`
- `airportd` repeatedly cached `CWFApple80211 ... name=(null)` for `en0`
- `Apple80211BindToInterfaceWithIOCTL` kept falling back with `err -1`
- `dmesg` showed interface-side `isCommandProhibited(0x2b)=0` and
  `isCommandProhibited(0xc)=0`, immediately followed by
  `Failed ioctl ret[26279936] 'APPLE80211_IOC_CARD_CAPABILITIES'`

That moves the root one notch narrower again. The bug is no longer "interface
slot [411] stayed on inherited behavior for hidden commands". `CR-033` already
changed that. The post-fix regression shows that the *non-hidden* side of the
same interface gate must also remain on the controller-owned policy, not on
`IO80211InfraProtocol`'s inherited filter:

- on the local Tahoe port, command policy already exists on
  `AirportItlwm::isCommandProhibited(int)`
- pre-`CR-033` startup booted far enough to show Wi-Fi networks in UI, so the
  controller-owned policy was already compatible with startup IOCTLs
- `CR-033` changed only one runtime-bearing seam:
  `AirportItlwmSkywalkInterface::isCommandProhibited(int)`
- once that seam delegated non-hidden commands to the inherited family filter,
  ordinary startup IOCTLs such as `APPLE80211_IOC_CARD_CAPABILITIES` stopped
  crossing the same owner path that had previously booted correctly

So the corrected contract is narrower and more precise:

- keep the explicit interface-side override because hidden `0x45` / `0x46`
  really do cross slot `[411]`
- but proxy *all* commands through the already-bound controller owner via
  `instance->isCommandProhibited(command)` instead of bouncing non-hidden
  commands into `super::isCommandProhibited(command)`
- this preserves the local Tahoe controller as the single source of command
  policy while still exposing the hidden association carrier on the interface
  seam Apple uses

This new post-`CR-033` root is tracked as
`TAHOE-INTERFACE-CMD-GATE-OWNER-MISMATCH-008`.

`CR-034` then tested the owner hypothesis directly and was rejected for lack of
causality: the reviewed startup commands were already observed with
`prohibited=0`, and the proposed controller proxy also returned `false` for all
commands. That rejection narrows the root again. On the failing startup path,
the policy bit did not change; the only new behavior introduced by `CR-033` was
instrumentation inside `AirportItlwmSkywalkInterface::isCommandProhibited(int)`.

The strongest live clue is the startup/telemetry interleave in `dmesg`:

- `Failed to send CoreAnalytics for event com.apple.wifi.ioctlPathUsedByClientitlwm: DEBUG isCommandProhibited command=0xc prohibited=0`
- immediately followed by
  `Failed ioctl ret[26279936] 'APPLE80211_IOC_CARD_CAPABILITIES'`

So the next narrower fix is not a new policy owner. It is to preserve the
hidden `0x45` / `0x46` allow from `CR-033` while removing all extra logging
from the interface-side command gate and leaving ordinary startup commands on
the plain inherited path. This root is tracked as
`TAHOE-INTERFACE-CMD-GATE-INSTRUMENTATION-SIDE-EFFECT-009`.

- add `AirportItlwmSkywalkInterface::isCommandProhibited(int)`
- return `false` only for hidden Tahoe commands `0x45` and `0x46`
- leave every other command on the inherited family filter

That does not claim that the full hidden association owner is recovered. The
claim is only that the live Tahoe carrier must cross the same interface command
gate seam Apple explicitly opens during core bring-up, instead of dying on the
local inherited default before any deeper owner can participate.

## Visible Scan RSN AKMs Must Be Sanitized Before The Hidden Tahoe Join Owner

Post-`CR-035` Tahoe runtime narrows the association failure again:

- startup is healthy enough that networks are visible in UI
- `wdutil` shows `en0`, `Power: On`, and non-zero scan cache
- manual association still fails with `-536870201`
- disassociate reasons remain `8` / `10`
- policy still labels the disconnect as `INVALID_AKMS`

The important part is where the active owner still is not.
Same-cycle kernel logs still do not enter the local public join/auth path:

- `setASSOCIATE(...)`
- `setWCL_ASSOCIATE(...)`
- `setAUTH_TYPE(...)`
- `setRSN_IE(...)`
- `associateSSID(...)`

So the already present local WPA3->WPA2 downgrade shim in `associateSSID(...)`
is not the active owner for this runtime. The next earlier, still-local seam is
the visible scan / beacon IE payload that CoreWiFi consumes before the hidden
Tahoe join owner starts.

Live evidence on that visible surface already shows the problem:

- `airportd` chooses the transition candidate with
  `rsn=(... auths={ psk psk_sha256 sae } ...)`
- scan-cache telemetry exposes advanced-AKM bitmasks such as
  `enc=0x8 akm=0x00048080`

That means the visible candidate plane is still telling CoreWiFi that this BSS
should be attempted through the transition / advanced-AKM route, even though
the hidden active join path later rejects that choice with `INVALID_AKMS`.

The narrow fix is therefore on the visible producers, not on the hidden owner:

- add shared `copyTahoeClientVisibleIEs(...)`
- parse RSN IEs
- if an IE is mixed / transition, keep only legacy-visible AKMs
  `00:0f:ac:01` and `00:0f:ac:02`
- if filtering would remove every AKM, keep the original IE unchanged so
  WPA3-only networks are not downgraded falsely

Those sanitized client-visible IEs now feed all three visible Tahoe scan
producers:

- `buildTahoeWclScanResultPayload(...)`
- `convertNodeToScanResult(...)`
- `getWCL_BSS_INFO(...)`

One extra ABI seam had to be fixed for this to be real on Tahoe builds.
`MacKernelSDK` still models `apple80211_scan_result::asr_ie_data` as a pointer,
while the local Tahoe header also has an inline-buffer variant. The sanitizer
therefore now writes through an ABI-neutral helper:

- inline-buffer scan results receive copied sanitized bytes directly
- pointer-based scan results receive an interface-owned scratch buffer

This keeps the claim narrow and causal:

- do not claim the hidden join owner is fully lifted
- do not fabricate legacy support for WPA3-only BSSes
- only stop over-advertising advanced AKMs on the visible candidate surface
  that precedes the hidden join path

`CR-036` rejected that visible-plane fix path. The reviewer did not accept
visible mixed-network RSN suppression as an Apple-backed contract and treated
it as masking unless the same policy could be proven from reference behavior.

The next narrower root is in Tahoe's build contract, not in the client-visible
scan surface. Project audit showed that `AirportItlwm-Tahoe` was the only
Airport target still missing `USE_APPLE_SUPPLICANT` in
`GCC_PREPROCESSOR_DEFINITIONS`; every other Airport target already built with
that macro. Effective build settings confirmed the drift directly:

- before the fix:
  `GCC_PREPROCESSOR_DEFINITIONS = ... AIRPORT __IO80211_TARGET=__MAC_26_0 ...`
- after the fix:
  `GCC_PREPROCESSOR_DEFINITIONS = ... AIRPORT USE_APPLE_SUPPLICANT __IO80211_TARGET=__MAC_26_0 ...`

That macro is causally relevant here because it flips the exact Tahoe seams
still involved in the failing connect path:

- `AirportItlwm::useAppleRSNSupplicant(...)` returns `true`
- `AirportItlwmSkywalkInterface::getRSN_IE(...)` /
  `setRSN_IE(...)` expose Apple-managed RSN IE override state
- `ieee80211_output.c` uses `ic_rsn_ie_override` in association requests
- `ieee80211_input.c` forwards EAPOL to Apple userspace instead of consuming it
  locally
- `ieee80211_crypto_tkip.c` takes the Apple-supplicant MIC-direction branch
- Tahoe can surface real `ic_assoc_status` instead of collapsing failures to
  `APPLE80211_STATUS_UNAVAILABLE`

That matches the live post-`CR-035` symptom better than the rejected visible
sanitizer did: Tahoe already reaches an Apple-facing auth flow
(`CWEAPOLClient`, `INVALID_AKMS`), but the target was still built without the
Apple supplicant contract enabled underneath.

So the new exact fix is:

- add `USE_APPLE_SUPPLICANT` to Tahoe Debug and Release target settings
- make `scripts/build_tahoe.sh` fail if the effective Tahoe build settings do
  not contain that macro
- have Tahoe `getASSOCIATION_STATUS(...)` return `ic_assoc_status` while the
  link is not yet in `RUN`
- keep scan/beacon IE producers on raw copy and drop the rejected `CR-036`
  visible-RSN rewrite entirely

After reboot into the `CR-037` runtime, the user-visible symptom split into two
separate layers:

- rescans are still happening
- but the fresh visible scan set is incomplete

That split matters because the earlier "rescan does not happen" hypothesis is
no longer true on the live system. The runtime evidence is now:

- kernel repeatedly logs
  `setWCL_SCAN_REQ -> scan triggered OK`
- `fakeScanDone` reports `nodes=15 posting WCL_SCAN_RESULT (0xC9) + WCL_SCAN_DONE (0xED)`
- `postWclScanResultsGated` reports `posted scanResults=15`
- `airportd` logs `Scan: Updated scan cache with live scan results`
- but the same windows often collapse to `scanResultsCount=8`

`system_profiler SPAirPortDataType` on the same runtime matches that collapse:
only eight nearby networks remain visible, and all eight are 2.4 GHz channels.

That band skew is the crucial clue. The local port is not merely posting "too
few" results; it is posting fresh `0xC9` scan-result metadata in a format that
selectively loses part of the set on Apple ingest. The narrowest exact seam is
the `chanSpec` field inside the Tahoe `0x44` `BeaconMetaData` carrier.

Apple reference behavior is explicit here. In
`AppleBCMWLANScanAdapter::processScanResults`, the producer does not hand-roll
a custom scan-result chanspec. It converts the firmware chanspec through
`AppleBCMWLANChanSpec::getAppleChannelSpec(...)` first, then packages the
result into the outgoing `0xC9` bulletin. The local Tahoe port diverged by
publishing:

- `0x1000 | (band << 14) | channel`

from `buildTahoePrimaryChanSpec(...)`.

That value is not Apple-compatible for the FW<2 primary-20
`AppleChannelSpec_t` carrier used on this path. The Apple mapping is much
narrower:

- 2.4 GHz primary 20 -> `channel`
- 5 GHz primary 20 -> `0xc000 | channel`

That exactly matches the live symptom: malformed 5 GHz `chanSpec` values can be
discarded or misclassified while 2.4 GHz entries still survive, leaving the UI
with the smaller eight-network subset even though the kernel posted fifteen
fresh results.

So the next exact fix is limited to the scan-result metadata carrier itself:

- replace `buildTahoePrimaryChanSpec(...)` with Apple-compatible primary-20
  `AppleChannelSpec_t` emission
- keep the rest of `buildTahoeWclScanResultPayload(...)` unchanged

This claim is intentionally narrow. It fixes the newly proven scan-surface root
`TAHOE-WCL-SCAN-CHANSPEC-MISMATCH-012`; it does not claim that the separate
hidden association failure is already solved.

The connect failure narrowed further in the same runtime window. The decisive
live signal is no longer just downstream `INVALID_AKMS`; it is the earlier
controller-side wrapper failure:

- `2026-04-20 17:44:58.510`
  `Apple80211IOCTLSetWrapper ... ifname['en0'] IOUC type 20/'APPLE80211_IOC_ASSOCIATE', len[908] return -536870201/0xe00002c7`
- `2026-04-20 17:44:58.720`
  same second failure on `APPLE80211_IOC_ASSOCIATE`

That matters because the attempted controller-side follow-on through
`apple80211SkywalkRequest(...)` turned out to be impossible on Tahoe itself:
`IO80211ControllerV3.h` does not declare either Skywalk-request override, and
the target fails to compile if those methods are introduced with `override`.
So the old request-plane hypothesis was not just unverified; it was ABI-wrong.

Tahoe V3 does still expose one generic controller-side card-specific seam:

- `handleCardSpecific(IO80211SkywalkInterface *, unsigned long, void *, bool)`

`APPLE80211_IOC_ASSOCIATE` sits outside the dedicated Tahoe pure-virtual
getter/setter block, which fits the wrapper-side `0xe00002c7` leak. The narrow
next fix is therefore to route only selector `20` from
`handleCardSpecific(...)` into the already implemented interface helper:

- synthesize `apple80211req`
- set `req_type = APPLE80211_IOC_ASSOCIATE`
- call
  `AirportItlwmSkywalkInterface::processApple80211Ioctl(SIOCSA80211, ...)`

This is a connect-root fix, not a scan-root extension. It does not replace
`TAHOE-WCL-SCAN-CHANSPEC-MISMATCH-012`; it addresses the separate
`TAHOE-ASSOC-IOCTL-CARDSPECIFIC-ROUTING-013` ingress that currently prevents
the local `setASSOCIATE(...)` / auth / RSN path from being reached at all.

The next live blocker after reboot into the `CR-039` runtime moved one step
earlier again. The interface is now visible and scan cache is populated, but
startup / auto-join still aborts on selector `216` before any join:

- `2026-04-20 19:15:40.549`
  `Apple80211IOCTLSetWrapper ... ifname['en0'] IOUC type 216/'APPLE80211_IOC_ROAM_PROFILE', len[384] return -528342013/0xe0822403`
- the same window immediately emits repeated
  `AUTO-JOIN: Auto-join aborted ... error=(37 'driver not available')`

That fail shape is wrong for Tahoe. The reference contract already recovered in
the earlier setter audit is:

- `AppleBCMWLANInfraProtocol::setROAM_PROFILE(...) -> 0xe00002c7`

So selector `216` is not supposed to surface a hidden "not associated" carrier
to `airportd`; it is supposed to fail in the ordinary Apple-unsupported shape.
The local Tahoe Skywalk target already has that public contract available:

- `setROAM_PROFILE(...)` override exists and returns `kIOReturnUnsupported`

The problem is that the active runtime path still bypasses that local stub and
leaks the hidden WCL `0xe0822403` instead. The narrow fix is therefore not to
invent a roam-profile producer. It is to restore the correct fail shape by
routing selector `216` into the existing local unsupported owner:

- `processApple80211Ioctl(SIOCSA80211, ...) -> setROAM_PROFILE(...)`
- plus the same Tahoe `handleCardSpecific(...)` ingress family that now carries
  selector `20`

This is tracked separately as `TAHOE-ROAM-PROFILE-FAIL-SHAPE-014`. It does not
replace the earlier association-routing root; it removes a newly confirmed
startup/auto-join blocker that still trips before the join sequence gets that
far.

The next connect window on the `CR-040` runtime narrowed the active
association path again. Manual `ASSOCIATE` now fails with the public Apple
unsupported code instead of the old hidden Tahoe carrier:

- `2026-04-20 22:52:19.129`
  `Apple80211IOCTLSetWrapper ... APPLE80211_IOC_ASSOCIATE ... return -536870201/0xe00002c7`
- `2026-04-20 22:52:19.347`
  same second failure on the retry

But the important same-cycle kernel evidence is what did **not** happen:

- no `handleCardSpecific(...)`
- no `processApple80211Ioctl(...)`
- no `setASSOCIATE(...)`
- no `setWCL_ASSOCIATE(...)`
- no `associateSSID(...)`

Instead the only exact same-cycle local hit is:

- `DEBUG VTABLE [470] getAWDL_PEER_TRAFFIC_STATS`

That disproves the active `handleCardSpecific(...)` hypothesis for the live
selector-`20` path. Primary-source family decomp now shows the actual owner:

- `getSetHandler(20)` resolves to `IO80211Family::FUN_ffffff80022080ef`
- that handler validates `req_len == 0x3ad8`
- then emits hidden carrier
  `sendIOUCToWcl(..., 0x802869c8, 0x45, payload, 0x3ad8, ...)`
- if WCL does not absorb it, fallback `FUN_ffffff80021e82ef` only checks
  interface slot `+0xcc8` (`isCommandProhibited(0x45)`) and then re-enters the
  none-protocol side

So the new narrower root is `TAHOE-HIDDEN-ASSOC-SLOT470-BRIDGE-015`: on the
local port the hidden `0x45/0x3ad8` fallback reaches interface slot `[470]`
and dies in the generic unsupported stub there, even though a real local
association owner already exists in `setWCL_ASSOCIATE(...)`.

The narrow runtime fix is therefore:

- keep the hidden `0x45` gate open
- replace the inline `slot[470]` unsupported stub with a real implementation
- when that slot receives `len == 0x3ad8`, route the payload straight into
  `setWCL_ASSOCIATE(...)`
- preserve `kIOReturnUnsupported` for unrelated callers

This is intentionally smaller than inventing a new hidden owner. It reuses the
already recovered local `apple80211AssocCandidates` parser exactly at the seam
the runtime now proves is active.

The same audit pass over the remaining inline Tahoe stubs found one more
confirmed structural mismatch that is not the current connect blocker but is
still wrong by reference contract:

- slot `[509]` `getCHIP_POWER_RANGE(apple80211_chip_power_limit*)`

This one is not an Apple-unsupported getter. The recovered reference body is:

- `AppleBCMWLANCore::getCHIP_POWER_RANGE(...)`
- `version=1`
- then copy a packed 6x-`u64` duty-cycle table from config-manager state

And the helper body confirms the exact public ABI:

- `AppleBCMWLANConfigManager::copyWlanPwrDutyCycleTable(...)`
- packed carrier size `0x34`
- layout `u32 version + u64[6]`

So the local inline `kIOReturnUnsupported` stub at `[509]` was a real
structural gap, not "Apple also unsupported". The narrow correction is to:

- add the missing `apple80211_chip_power_limit` carrier locally
- replace the inline stub with a real `getCHIP_POWER_RANGE(...)` producer
- recover the same Tahoe config source Apple uses:
  - read `wlan.chip.power.dutycycle` (`0x30` bytes) from the
    interface/provider IOService path
  - if that property is absent, copy the exact built-in six-entry fallback
    table recovered from the Apple config-manager bootstrap path

This audit explicitly did **not** sweep the rest of the inline stubs blindly.
Nearby slots such as `getWIFI_NOISE_PER_ANT(...)`, `getHE_COUNTERS(...)`,
`getWCL_WNM_OFFLOAD(...)`, `getFW_CLOCK_INFO(...)`, `getTIMESYNC_STATS(...)`,
`getSMARTCCA_OPMODE(...)`, and `getLQM_STATISTICS(...)` remain on their proven
Apple-unsupported contract and were left untouched.

The next pass then closed the remaining inline Tahoe `unsupported` tail itself,
but still by reference class rather than by mechanical blanket "implement
everything":

1. direct Apple-unsupported getters:
   `[491, 492, 505, 529, 536, 537, 538, 539, 541, 542]`
2. direct Apple-unsupported setters:
   `[561, 583, 585, 605, 607, 630, 635, 641, 642, 643, 644, 645, 646, 648,
   660, 661, 663]`
3. internal-only / no-producer-recovered selectors that must stay explicit
   unsupported:
   `[488, 499, 563, 591, 620, 621]`
4. the one remaining non-stub visible contract in that tail:
   `[632] setWCL_UPDATE_FAST_LANE`

The important correction is not that all `34` selectors suddenly became public
producers. They did not. The correction is narrower:

- there are now no inline `return kIOReturnUnsupported;` slot bodies left in
  `AirportItlwmSkywalkInterface.hpp`
- each former inline body now lives in `.cpp` with its reference class made
  explicit
- `[632] setWCL_UPDATE_FAST_LANE(...)` no longer hides behind generic
  unsupported; it now follows the recovered public Apple contract
  `NULL -> 0xe00002bc`, else success

That leaves the Tahoe slot surface easier to audit: direct Apple stubs,
internal-only selectors, and real public producers are no longer conflated in
anonymous header placeholders.

After rebooting into `CR-044`, the next active blocker is no longer scan
visibility or slot `[509]`. The runtime now shows:

- `wdutil`: `en0`, `Power: On`, `Scan Cache Count: 16`
- `system_profiler`: mixed 2.4 GHz / 5 GHz networks, including WPA2/WPA3
  candidates

But the same boot still leaks current-link probe failures:

- external `APPLE80211_IOC_SSID -> 0xe0822403`
- external `APPLE80211_IOC_CURRENT_NETWORK -> 0xe0822403`
- external `APPLE80211_IOC_VIRTUAL_IF_ROLE -> -3903/0xfffff0c1`

The decisive runtime shape is that same-cycle local logs do show unrelated
Skywalk getter hits (`getPOWER`, `getOP_MODE`, `getRATE`, `getRSSI`) while
showing no local hits on:

- `processApple80211Ioctl(...)`
- `getSSID(...)`
- `getBSSID(...)`
- `getCURRENT_NETWORK(...)`

That narrows the next seam to controller-side
`apple80211SkywalkRequest(...)`: the family is issuing these requests against
the bound Skywalk interface, but the local controller is still leaving the
failing current-link selectors on inherited routing instead of dispatching them
into the already recovered Tahoe Skywalk owner.

The narrow fix candidate is:

- add `AirportItlwm::apple80211SkywalkRequest(...)`
- bridge only the current-link probe selectors into
  `AirportItlwmSkywalkInterface::processApple80211Ioctl(...)`
- keep every other Skywalk selector on inherited controller behavior

That controller-request conclusion did not survive Tahoe V3 ABI validation.
The staged local `apple80211Request(...)` experiment failed the real target
shape and was removed: `IO80211ControllerV3.h` does not expose that override on
the active Tahoe class.

`TAHOE-CURRENT-AP-CACHE-SEED-019` and `TAHOE-BSD-UNIQUEID-CONTRACT-020` are no
longer the active fix candidates. Both were rejected in review and removed from
the exact runtime diff:

- `019`: zero-BSSID `setCurrentApAddress(...)` seeding was a guessed
  current-link cache repair
- `020`: BSD `uniqueid` proved a real Apple/xnu seam, but not the current
  `ifname['en0']` failure causality

The current exact root is narrower and matches the live boot directly:
`TAHOE-CONTROLLER-CARDSPECIFIC-GETSET-ROUTING-021`.

Live `2026-04-21` proof on loaded runtime `377695CB-9BA7-3BB2-ADA2-34A997EA95E8`:

- `11:00:03.117` `ifname['en0'] APPLE80211_IOC_SSID -> 0xe0822403`
- `11:00:03.118` `ifname['en0'] APPLE80211_IOC_BSSID -> 0xe0822403`
- `11:00:03.125` `ifname['en0'] APPLE80211_IOC_ROAM_PROFILE -> 0xe0822403`
- `11:00:04.962` `AUTO-JOIN ... error=(37 'driver not available')`
- same boot still returns the already-fixed
  `APPLE80211_IOC_VIRTUAL_IF_ROLE -> -3903`

That combination matters. The external `ifname['en0']` plane is alive, but the
failing bootstrap/current-link selectors are still bypassing the local Skywalk
helper plane. The loaded `CR-044` runtime already contains `XYLog(...)` probes
in:

- `processBSDCommand(...)`
- `processApple80211Ioctl(...)`
- `getSSID(...)`
- `getBSSID(...)`
- `getCURRENT_NETWORK(...)`
- `setROAM_PROFILE(...)`

Current `log show` / `dmesg` windows for the same boot show no hits on those
local probes while `airportd` is logging the external failures above. So the
remaining mismatch is no longer inside the BSD bridge itself; it is earlier, on
the Tahoe V3 controller-side dispatch seam that still chooses whether a request
is treated as `GET` or `SET`.

That surviving public Tahoe seam is:

- `handleCardSpecific(IO80211SkywalkInterface *, unsigned long, void *, bool isSet)`

The local code already had almost the full correction prepared:

- `shouldRouteTahoeSkywalkIoctlReq(...)` already whitelists the failing GET
  cluster:
  - `SSID`
  - `BSSID`
  - `CHANNEL`
  - `CURRENT_NETWORK`
- the same helper already whitelists the visible SET cluster:
  - `ASSOCIATE`
  - `DISASSOCIATE`
  - `AUTH_TYPE`
  - `RSN_IE`
- and `ROAM_PROFILE` is a special bidirectional selector because the local
  interface owner already exposes both:
  - `getROAM_PROFILE(...)`
  - `setROAM_PROFILE(...)`

But the local controller bridge was still asymmetric in two ways:

- `handleCardSpecific(...)` left `isSet == false` requests on inherited
  fallback
- `shouldRouteTahoeSkywalkIoctlReq(...)` still classified `ROAM_PROFILE` as
  GET-only, so the same visible set-side failure cited in the live logs could
  never reach local `setROAM_PROFILE(...)`

The exact local repair is therefore:

- keep `routeTahoeSkywalkIoctl(...)` as the single whitelist-driven bridge
- make `APPLE80211_IOC_ROAM_PROFILE` explicit bidirectional in
  `shouldRouteTahoeSkywalkIoctlReq(...)`
- in `handleCardSpecific(...)`, build one `apple80211req`
- route with:
  - `isSet ? SIOCSA80211 : SIOCGA80211`
- let `shouldRouteTahoeSkywalkIoctlReq(...)` decide the selector surface
- leave all non-whitelisted selectors on inherited behavior

This is the narrowest causally supported correction for the current boot:
complete Tahoe V3 controller-side GET/SET dispatch into the already recovered
local Skywalk helper bodies, instead of leaking raw `0xe0822403` before those
helpers are entered.

The next reboot into the approved `CR-048` runtime disproved that controller
claim as the active seam for this boot. The visible failures remained:

- `2026-04-21 11:00:03.117` `APPLE80211_IOC_SSID -> 0xe0822403`
- `2026-04-21 11:00:03.118` `APPLE80211_IOC_BSSID -> 0xe0822403`
- `2026-04-21 11:00:03.125` `APPLE80211_IOC_ROAM_PROFILE -> 0xe0822403`
- `2026-04-21 11:00:04.962` `AUTO-JOIN ... driver not available`

But the stronger local proof is not another hidden command family. It is the
public request-number fallback itself.

Primary-source `IO80211Family` decomp already shows the active public wrappers
for the failing selectors:

- `FUN_ffffff80022a2910` sends `sendIOUCToWcl(..., 1, ..., 0x28)` and falls
  back to `FUN_ffffff80021e28b2`
- `FUN_ffffff8002215524` sends `sendIOUCToWcl(..., 9, ..., 0xc)` and falls
  back to `FUN_ffffff80021e2b46`
- `FUN_ffffff80021e29a7` falls back with request number `4`
- `FUN_ffffff80021e3912` falls back with request number `0x67`
- `FUN_ffffff80021e465f` falls back directly with request number `0xd8`
- `FUN_ffffff80021e94fa` falls back directly with request number `0xd8`
- the visible wrapper-side producer for the same selector sends
  `sendIOUCToWcl(..., 0xd8, ..., 0x180)` before those fallback helpers

And those fallback helpers all do the same controller call:

- `(**(code **)(*param_1 + 0xcc8))(param_1, request_number)`

with public request numbers `1`, `4`, `9`, `0x67`, and `0xd8`.

`IO80211_vtables_BootKC_26.2_25C56.txt` maps that `+0xcc8` slot to
`IO80211SkywalkInterface::isCommandProhibited(int)`. That makes `CR-049`'s
hidden `0x103/0x104/0x15e` theory unnecessary and, after review, wrong to keep
carrying. The tighter 1:1 mismatch is on the local Tahoe interface gate:

- the active local helper owners for `SSID/BSSID/CHANNEL/CURRENT_NETWORK/
  ROAM_PROFILE` already exist in `processApple80211Ioctl(...)`
- but `AirportItlwmSkywalkInterface::isCommandProhibited(int)` still admitted
  only the carried hidden association commands `0x45/0x46`
- the public fallback request numbers `1/4/9/0x67/0xd8` were therefore still
  left on inherited family filtering

So the current boot is still dying before the already recovered local helper
plane because the public interface request-number gate itself is too narrow.
The narrow correction is:

- keep `isCommandProhibited(...)` narrow to proven selectors only
- retain the already approved hidden association commands `0x45/0x46`
- additionally admit only the proven public fallback request numbers:
  `1`, `4`, `9`, `0x67`, `0xd8`
- continue delegating those admitted selectors to the already permissive local
  controller policy:
  `instance != nullptr ? instance->isCommandProhibited(command) : false`

That is the new active root:
`TAHOE-INTERFACE-PUBLIC-REQUEST-GATE-023`.

Post-reboot evidence on runtime `B1AFF314-2935-3718-80F7-C440303D13D6`
tightened that seam one step further. The selector subset itself was correct,
but the local gate still returned the aborting polarity.

`IO80211Family` fallback helpers do not use slot `[411]` as a plain
"prohibited?" boolean. The exact helpers branch the other way:

- `FUN_ffffff80021e28b2`: `if (slot411(...,1) != 0) return;`
- `FUN_ffffff80021e2b46`: `if (slot411(...,9) != 0) return;`
- `FUN_ffffff80021e3912`: `if (slot411(...,0x67) != 0) return;`
- `FUN_ffffff80021e465f`: `if (slot411(...,0xd8) != 0) return;`
- `FUN_ffffff80021e94fa`: `if (slot411(...,0xd8) != 0) return;`

So zero is still the aborting value on this seam.

That explains why `CR-051` produced no post-reboot change for
`SSID/BSSID/CURRENT_NETWORK/ROAM_PROFILE`: the local interface gate delegated
the selected commands to controller-side `AirportItlwm::isCommandProhibited(int)`,
and that stub returns `false` unconditionally. The selected commands were
therefore still reaching slot `[411]` as zero and still taking the family abort
path.

The next exact correction is narrower than a new routing theory:

- keep the same selected command subset
- return non-zero directly at interface slot `[411]` for those commands
- keep all unrelated selectors on inherited behavior

That new active root is:
`TAHOE-INTERFACE-REQUEST-GATE-POLARITY-024`.
