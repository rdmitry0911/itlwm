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

That result removed the bootstrap and no-op producers, but the surviving real
radio-transition producer was still not reference behavior. Exact current
`AppleBCMWLANCore::handlePowerStateChange` decompilation contains no
`POWER_CHANGED` post. The selector is owned by the separate IOPM system
sleep/wake path. Removing the radio producer proved necessary but incomplete:
FBT saw no system-power call during radio-off, and the next radio-on still
replayed WAKE into NetManager `WAITING_FOR_IP`. Exact IOPM decompilation then
showed the remaining drift: local deferred thread calls, manual PM ack, ON
initial state, and an extra system-OFF selector `1`; reference transitions are
synchronous on the command gate, preserve a shared atomic state word, apply
the `0x30 == 0x20` transition gate, remove `IO80211WokeSystem` before OFF,
publish unavailable/available `0x37` through `powerOff(true)`/`powerOn()`, and
publish selector `1` only after system ON. Corrected UUID
`8AFE24EC-4859-33BD-9E12-452F4DC24A90` executed real framework sleep/wake,
four independent post-wake radio cycles, and concurrent `240/240` ping with
240-second iperf3 without a fault. It remains a confirmed producer deviation,
not the sole replay-panic root.

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

The first batch identified from decompile was:

- `setOS_FEATURE_FLAGS`
- `setDHCP_RENEWAL_DATA`
- `setBATTERY_POWERSAVE_CONFIG`
- `setPOWER_PROFILE` (reclassified below as a no-local-backend quarantine)
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

### `setPOWER_PROFILE`: ConfigManager-backed quarantine

Apple path:

- null returns `0xe00002bc`
- stores the first 32-bit profile in core state at `+0x29e8`
- dispatches through Core virtual `+0x560`; the `AppleBCMWLANCore` vtable
  entry at `0x1003a1648` resolves to `setPowerProfile` at `0x100124398`
- `setPowerProfile` selects ConfigManager at Core `+0x1558` and calls its
  `setPowerProfile` owner at `0x10008b53e`

The local port had only an unread `cachedPowerProfile` dword and returned
success. That cache does not implement the ConfigManager/power-profile owner or
its downstream status. The port now preserves the null error and returns
`kIOReturnUnsupported` for non-null input before reading it, removing the
pseudo-state and both reset sites. This does not claim complete carrier ABI,
ConfigManager, power-profile, or valid-input return-status parity.

### `setIPV4_PARAMS`

Apple path:

- null returns `0xe00002bc`
- optionally forwards to the infra-interface side, without propagating that
  call's status
- stores raw IPv4/mask/router/tail fields in persistent core state
- triggers IPv4 notification handling, optional Proximity update, and
  keepalive notification if both address and mask are non-zero

The former local cache-only success was therefore false: its five IPv4 fields
had no reader outside initialization and this setter, while the port has no
Infra IPv4 owner, notification, Proximity, or keepalive lifecycle. The local
`NULL -> kIOReturnBadArgumentTahoe` boundary matches Apple; a non-null request
now returns unsupported before carrier access or mutation. This does not claim
Apple valid-input return-code parity.

### `setIPV6_PARAMS`

Apple path:

- optionally forwards to infra-interface if present, without propagating that
  call's status
- clamps the raw count to 10, reads the first dword of each `+4 + 0x10*i`
  entry, clears its companion state area, records count, seeds link-local
  `fe80` state, and schedules IPv6 notification handling

Again, this is lifecycle work rather than an ack-only slot. The former local
IPv6 table/link-local cache had no reader outside initialization and this
setter, and the port has no matching Infra or notification owner. The raw
25C56 Core body does not establish a safe Apple NULL return: its NULL branch
reaches an immediate carrier dereference. The retained local NULL rejection is
therefore a safety boundary, not null-return parity; non-null input returns
unsupported before access or mutation.

### `IPV4_PARAMS` / `IPV6_PARAMS` correction

The earlier state-carrier lift conflated Apple state persistence with a valid
local cache-only implementation. Tahoe 25C56 does perform those state writes,
but it also drives Infra / notification / keepalive lifecycle operations absent
from AirportItlwm. Both dead local cache layouts and their reset paths are
removed; neither public setter now reports success for an unapplied request.
This correction does not synthesize IP configuration, direct firmware work,
private IOCTLs, notifications, or valid-input Apple status parity.

### `setINFRA_ENUMERATED`

Apple path:

- null returns `0xe00002bc`
- a non-null carrier is read at byte `+0`; zero returns success without the
  owner action, while nonzero reaches
  `AppleBCMWLANCommander::deviceBootStationaryNotification()`
- that Commander terminal updates its command-timeout state and still returns
  success to Core

This is therefore not a cache-only minimal producer contract. The canonical
25C56 recovery is corrected below: AirportItlwm has neither the Commander
owner nor a defined local ABI for the opaque forward-declared carrier.

### 2026-07-14 correction: `INFRA_ENUMERATED` is Commander-backed

The canonical DEXT routes
`AppleBCMWLANInfraProtocol::setINFRA_ENUMERATED` at `0x10001936c` directly to
Core `0x100142bf0`. `NULL` returns `0xe00002bc`; Core reads exactly byte `+0`.
Only a nonzero byte follows `(Core + 0x48) + 0x1520` to
`AppleBCMWLANCommander::deviceBootStationaryNotification()` at `0x100181e04`.
That void terminal first follows Commander `+0x40` to its internal state, then
writes timeout `0x61a8` at state `+0x10c`, with a `wlan.factory` adjustment
using state `+0xa8`.

The local carrier remains only a forward declaration, without an IOC route or
length validation, so reproducing Apple’s byte-zero branch would add an
unchecked opaque-carrier read. The local boundary keeps its safe NULL error,
rejects every non-null carrier with `kIOReturnUnsupported` before access, and
removes the dead cache. It does not claim valid-input return-code parity or
Commander timeout behavior.

See [CR-479-infra-enumerated-commander-quarantine-20260714.md](reference/CR-479-infra-enumerated-commander-quarantine-20260714.md).

## Batch 1 Fix Direction

For the methods above, strict parity means:

- remove inline ack-only stubs from the Tahoe vtable
- move them into explicit out-of-line implementations
- return Apple `0xe00002bc` for null requests
- persist incoming state in driver-owned cached fields only when recovery
  proves that no deferred owner is required
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
- `setWCL_QOS_PARAMS`: decode a flagged carrier. Bits `0x01`, `0x04`, and
  `0x08` enter NetAdapter retry/lifetime transports; `0x20` enters Core
  real-time-app policy; `0x40` conditionally enters the 11be MLO owner. Only
  `0x02` (RTS) and `0x10` (powersave) are independently meaningful local
  actions; unknown `0x80` is an Apple no-op
- `setWCL_LINK_UP_DONE`: call `PowerManager::handleLinkUpConfiguration()`
- `setOFFLOAD_TCPKA_ENABLE`: default `0xe00002c7`, flip the keepalive enable
  byte only when the feature gate and owner object exist
- `setOFFLOAD_ARP`: reject NULL / no infra owner with raw `0x16`, then carry
  IPv4 + keepalive fields into core-owned state

The local Tahoe port still lacks Apple's hidden PowerManager / KeepAlive owner
objects. Locally owned pieces remain lifted, while owner-dependent ARP/
keepalive requests are explicit no-backend quarantines rather than inline
successes:

- powersave re-entry through the lifted Tahoe `setPOWERSAVE(...)` path
- RTS threshold through `ieee80211com::ic_rtsthreshold`
- post-link MAC-context refresh through `ic_updateedca`
- the separate paired IP-parameter no-backend quarantine, which does not
  fabricate IPv4/IPv6 notification or keepalive completion

### Q10 correction: WCL QoS has a selective owner boundary

The QoS carrier cannot be acknowledged as a cache-only whole. Tahoe routes
flags `0x01|0x04|0x08|0x20|0x40` through owners and transports absent from the
Intel port: retry limit via Commander IOCTL `0x22`, lifetimes via the
`lifetime` IOVAR, Core real-time-app policy, and feature-gated MLO
configuration. The port therefore returns `kIOReturnUnsupported` for any
non-null carrier containing the aggregate missing-owner mask `0x6d`, before
RTS/powersave mutation. It retains local RTS (`0x02`) and powersave (`0x10`)
actions, and preserves the reference no-op for unknown bit `0x80`.

This closes `Q10` as a selector queue without claiming full QoS carrier or
valid-input return-status parity. See
`docs/reference/CR-479-wcl-qos-selective-quarantine-20260714.md`.

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

## Q13 Setter Contract Zone: `MWS_*_WIFI_ENH` quarantines plus `NDD_REQ`

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

That recovered private-notifier graph establishes that a cache-only local
acknowledgement would be false success. The port has no corresponding MWS
owner, IOVAR transport, or callback chain, so every valid non-null
`MWS_*_WIFI_ENH` request is an explicit `kIOReturnUnsupported` no-local-backend
quarantine after its existing null/validation guard. This is not Apple
valid-input return-code parity and does not claim to mirror the carrier cache.
`setNDD_REQ(...)` remains the separately recovered feature-gated
`0xe00002c7` path.

## Q13 Minimal Setter-Contract Zone: remaining opaque carriers and fixed fail shapes

The remaining `Q13` zone closes the following setter slots, which share the
pragmatic boundary: Tahoe already exposes a stable public contract for them,
but the hidden owner choreography is either feature-gated or still private.

Closed in this zone:

- `setAP_MODE(...)`
- `setTHERMAL_INDEX(...)`
- `setBSS_BLACKLIST(...)`
- `setREALTIME_QOS_MSCS(...)`
- `setRSN_XE(...)`
- `setWCL_LIMITED_AGGREGATION(...)`
- `setWCL_BCN_MUTE_CONFIG(...)`

Recovered Apple behavior is consistent enough to lift this as one zone:

- several selectors are pure minimal contracts:
  `setWCL_LIMITED_AGGREGATION`
- several are opaque state carriers with only a null gate or a small public
  field split:
  `setBSS_BLACKLIST`, `setWCL_BCN_MUTE_CONFIG`, `setRSN_XE`
- several expose fixed Tahoe fail shapes rather than generic unsupported:
  `setAP_MODE -> 0xe00002c7`, `setTHERMAL_INDEX -> 0xe00002bc`
- `setOFFLOAD_TCPKA_ENABLE(...)` remains feature-gated and uses the same
  visible fail/success split as the getter-side path

`setPRIVATE_MAC(...)` was initially grouped here as a raw-`0x16` fixed-fail
shape. That classification was corrected on 2026-07-14 after the canonical
25C56 DEXT recovery showed a BGScanAdapter-backed valid-input path; it is now
handled by the separate owner/state quarantine below.

That closes the zone at the public Apple80211 surface:

- each remaining setter follows its recovered public boundary rather than an
  unqualified cache-only rule
- caller-visible carriers are preserved locally only where the remaining
  contract is genuinely cache-only
- fixed Tahoe fail codes remain explicit where Apple exposes them

## 2026-07-14 correction: `PRIVATE_MAC` is BGScanAdapter-backed

The canonical 25C56 DEXT shows `AppleBCMWLANInfraProtocol::setPRIVATE_MAC`
at `0x100018528` dispatching through Core virtual `+0x6f0` to
`AppleBCMWLANCore::setPRIVATE_MAC` at `0x10011ee12`. `NULL` returns raw
`0x16`, but a valid carrier configures BGScan private-MAC timeout/MAC state,
enables or disables private scan MAC according to carrier `+0x4`, and returns
success. This is not a fixed-fail selector.

`getPRIVATE_MAC` likewise reads BGScan adapter state and the private
`"scanmac"` IOVAR. AirportItlwm has no corresponding BGScan owner or backend.
Its prior local setter nevertheless copied timeout/MAC bytes before returning
raw `0x16`, which made a failed direct request observably alter the getter.
The local boundary now keeps raw `0x16` for `NULL`, rejects valid ownerless
carriers with `kIOReturnUnsupported` before reading them, removes the synthetic
cache, and returns only the existing zero packed-carrier baseline from the
getter. This does not claim Tahoe dynamic getter state or valid-input
return-code parity.

See [CR-479-private-mac-rejected-state-20260714.md](reference/CR-479-private-mac-rejected-state-20260714.md).

## Q13 correction: `setGAS_ABORT` is GASAdapter-backed

`setGAS_ABORT(...)` is not a local successful no-op. Tahoe 25C56 Infra
`0x100019b52` dispatches through Core virtual `+0x540` to
`AppleBCMWLANCore::setGAS_ABORT` `0x100137992`, which selects the GASAdapter
at Core `+0x1560`. Its terminal `0x1001a171a` conditionally calls
`issueGASAbort` `0x1000205e8` under feature bit `0x11`; that path sends the
private `anqpo_stop_query` IOVAR, emits GAS completion, and clears adapter
state. Even with the feature absent, Tahoe emits a completion status and clears
that state. The input pointer is neither read nor tested; its return is the
event-admission result, not a fixed successful no-op.

AirportItlwm has no GAS/ANQP adapter, abort IOVAR, fragment/completion event
path, or adapter state to clear. It therefore returns
`kIOReturnUnsupported` unconditionally, without adding a null gate or
synthetic completion. This is a no-local-backend quarantine, not Apple
return-code parity; see
`docs/reference/CR-479-gas-abort-quarantine-20260714.md`.

## Q13 correction: DBG guard time is commander-backed

`getDBG_GUARD_TIME_PARAMS(...)` and `setDBG_GUARD_TIME_PARAMS(...)` are not
paired local cache carriers. Tahoe 25C56 Infra getter `0x100017210` dispatches
through virtual `+0x2d0` to Core `0x100106ce8`, which calls Commander
`runIOVarGet("forced_pm", ...)` `0x10017b780` for an eight-byte reply. It
copies reply bytes into caller offsets `+0x4/+0x8/+0xa/+0xb` only for status
zero or `0xe00002e3`, while returning the raw transport status. Infra setter
`0x100018490` dispatches virtual `+0x4d8` (vtable entry `0x1003a15c0`) to Core
`0x1001203d4`; it packs selected caller bytes with fixed `0xaa` padding and
calls `runIOVarSet("forced_pm", ...)` `0x10017b6e6`, returning raw transport
status.

AirportItlwm has no matching command owner or transport. A local write-only
cache could neither apply the setting nor observe changes in the reference
owner, and a synthesized getter reply falsely claimed success. Both selectors
retain their existing local null guards as safety boundaries and return
`kIOReturnUnsupported` for non-null input. This does not claim Tahoe null,
carrier-allocation, transport-status, or direct-runtime parity; see
`docs/reference/CR-479-dbg-guard-time-quarantine-20260714.md`.

## Q13 correction: `setWCL_ASSOCIATED_SLEEP` is PowerStateAdapter-backed

`setWCL_ASSOCIATED_SLEEP(...)` is not an opaque cache carrier.  In Tahoe
25C56, Infra forwards virtual slot `+0x778` to Core, whose terminal setter
updates power-management state and calls the `PowerStateAdapter` at Core
`+0x8c88` to configure beacon SOI, data SOI, excess-PM alert, and
associated-sleep roam scanning.  A local byte copy cannot supply those
effects.

The port keeps its local null guard but returns `kIOReturnUnsupported` for a
non-null request before any pseudo-state mutation, and removes the dead
associated-sleep cache.  This is a no-local-backend quarantine: it does not
claim Apple null handling, complete carrier allocation, or valid-input return
code parity.  See
`docs/reference/CR-479-wcl-associated-sleep-quarantine-20260713.md`.

## Q13 correction: `setWCL_SOI_CONFIG` is PowerStateAdapter-backed

`setWCL_SOI_CONFIG(...)` is likewise not an opaque cache carrier.  Tahoe
25C56 forwards virtual slot `+0x780` to Core, which passes the base carrier
and its `+0x1c` portion through Core `+0x8c88` to the beacon-SOI and
data-SOI `PowerStateAdapter` configurators.  The recovered downstream paths
reach real commander IOVAR operations, including `bcn_li_bcn` and
`pm2_sleep_ret`; a local byte copy cannot reproduce those effects.

The port keeps its local null guard but returns `kIOReturnUnsupported` for a
non-null request before pseudo-state mutation, and removes the dead SOI
cache.  This is a no-local-backend quarantine: it does not claim Apple null
handling, complete carrier allocation, or valid-input return-code parity.
See `docs/reference/CR-479-wcl-soi-quarantine-20260713.md`.

## Q13 correction: `setOS_ELIGIBILITY` is NetAdapter-backed

`setOS_ELIGIBILITY(...)` is also not an opaque cache carrier.  Tahoe 25C56
forwards virtual slot `+0x7d0` to Core.  On a change to its eligibility bit,
and when the commander is awake, Core uses its NetAdapter at `+0x15e0` to
configure aggressive EDCA before it records the full carrier word.  The
recovered adapter reaches `wme_ac_sta` and retry-limit work; a local dword
copy cannot reproduce those effects.

The port keeps its local null guard but returns `kIOReturnUnsupported` for a
non-null request before pseudo-state mutation, and removes the dead
eligibility cache.  This is a no-local-backend quarantine: it does not claim
Apple null handling, complete carrier allocation, or valid-input return-code
parity.  See
`docs/reference/CR-479-os-eligibility-quarantine-20260713.md`.

## Q13 correction: `setDYNAMIC_RSSI_WINDOW_CONFIG` is ConfigManager-backed

`setDYNAMIC_RSSI_WINDOW_CONFIG(...)` is not an opaque cache carrier.  Tahoe
25C56 identifies its Infra wrapper symbol at `0x100019530` and, separately,
the Core implementation `setDYNAMIC_RSSI_WINDOW_CONFIG` at `0x10014365e`.
The latter passes the carrier dword to `configureDynamicRssiWindow` at
`0x100140672`.  Core selects its
ConfigManager through `+0x1558`; the recovered manager at `0x10008c6a6`
gates the feature and sends the `rssi_win` and `snr_win` commander IOVARs.  A
local dword copy cannot reproduce those effects.

The port keeps its local null guard but returns `kIOReturnUnsupported` for a
non-null request before pseudo-state mutation, and removes the dead dynamic
RSSI cache.  This is a no-local-backend quarantine: it does not claim Apple
null handling, complete carrier allocation, range/error, feature-gate, or
transport-status parity.  See
`docs/reference/CR-479-dynamic-rssi-window-quarantine-20260713.md`.

## Q13 correction: `setWCL_WNM_OFFLOAD` is WnmAdapter-backed

`setWCL_WNM_OFFLOAD(...)` is not an opaque cache carrier.  Tahoe 25C56 Infra
wrapper `0x100019af6` tail-jumps to Core `0x1001429d2`, which selects the
WnmAdapter at Core `+0x15b0` and tail-jumps to
`configureWnmOffloadFeatures` `0x1000a99e0`.  The adapter consumes control
bits at carrier `+0x00` and `+0x04`, calls its offload configure/unconfigure
operations, and its recovered descendants reach commander IOVARs `tclas_add`,
`wnm_dms_set`, and `wnm_dms_dependency`.  A local byte copy cannot reproduce
those effects.

The port keeps its local null guard but returns `kIOReturnUnsupported` for a
non-null request before pseudo-state mutation, and removes the dead WNM
offload cache and flag.  This is a no-local-backend quarantine: it does not
claim a complete carrier allocation or Apple null, valid-input, feature-gate,
or transport-status parity.  See
`docs/reference/CR-479-wcl-wnm-offload-quarantine-20260713.md`.

## Q13 correction: `setWCL_WNM_OPS` is WnmAdapter-backed

`setWCL_WNM_OPS(...)` is not an opaque cache carrier. Tahoe 25C56 Infra
wrapper `0x100019abe` tail-jumps to Core `0x1001429b0`, which selects the
WnmAdapter at Core `+0x15b0` and tail-jumps to `configureWnmFeatures`
`0x1000a7ff0`. The adapter branches into enterprise, product-info, and beacon
reporting configuration. Its recovered enterprise path reaches
`configureWNM` `0x1000aa9e0`, checks WNM support, and sends the `wnm`
commander IOVAR through `runIOVarSet` `0x10017b6e6`. A local byte copy cannot
reproduce those effects or their command-status handling.

The port keeps its local null guard but returns `kIOReturnUnsupported` for a
non-null request before pseudo-state mutation, and removes the dead WNM OPS
cache and flag. This is a no-local-backend quarantine: it does not claim a
complete carrier allocation or Apple null, valid-input, feature-gate, or
transport-status parity. See
`docs/reference/CR-479-wcl-wnm-ops-quarantine-20260713.md`.

## Q13 correction: `setREALTIME_QOS_MSCS` is QoS/MSCS-backed

`setREALTIME_QOS_MSCS(...)` is not a standalone state cache. Tahoe 25C56
Infra wrapper `0x1000189ac` dispatches through Core virtual `+0x7b0` to
`AppleBCMWLANCore::setREALTIME_QOS_MSCS` `0x1001e81a4`. Core obtains the
current BSS, checks feature bit `0x5f`, configuration byte `+0x7579`, and the
current-BSS QoS/MSCS capability virtual at `+0x290`. Only after those gates
does it read the state dword, set `+0x757b`, and call
`sendQoSMgmtMSCSReq` `0x10013d028`.

The sender reaches `confiQoSMgmtMSCS` `0x10013cda6`, whose recovered terminal
builds `WL_QOS_CMD_RAV_MSCS` and submits it through `qosSetIOVar`; the matching
firmware response is handled by `handleMSCSEvent` `0x1001de8dc`. A local dword
copy has none of that owner, firmware, or completion behavior.

The port preserves its local null guard but returns `kIOReturnUnsupported` for
a non-null request before pseudo-state mutation, and removes the dead cache.
This does not claim universal Apple null-input or valid-input return parity:
the reference checks its gates before the null branch. See
`docs/reference/CR-479-realtime-qos-mscs-quarantine-20260713.md`.

## Q13 correction: `setEAP_FILTER_CONFIG` is packet-filter-backed

`setEAP_FILTER_CONFIG(...)` is not a cache-only public carrier.  Tahoe 25C56
Infra wrapper `0x1000191ac` directly dispatches to Core `0x10014294e`, which
rejects null with `0xe00002bc` and stores only the first observed dword at
Core `+0x4d48`.  The deferred packet-filter owner at
`configurePktFilters` `0x10012f310` calls `deleteEapolFilter` and then
`configureEapolFilter` at `0x100135022`.  That configurator reads `+0x4d48`
and, when enabled, submits `pkt_filter_add` through Commander
`runIOVarSet` `0x10017b6e6`; its sibling delete path is a separate firmware
operation.

The port preserves its existing `kIOReturnBadArgumentTahoe` null guard but
returns `kIOReturnUnsupported` for a non-null request before pseudo-state
mutation, and removes the dead cache.  The recovery establishes only the
first effective dword, not a complete opaque carrier layout, valid-input
return parity, or a replacement packet-filter lifecycle.  Ordinary local
EAPOL RX/TX data paths are not this firmware filter backend.  See
`docs/reference/CR-479-eap-filter-config-quarantine-20260713.md`.

## Q13 Telemetry/Cache Getter Zone: public carriers without hidden owner lift

The original `Q13` zone classified fourteen getter slots as exposing a stable
Tahoe public contract even when the deeper Broadcom owner was still hidden.
That classification is superseded for `getMIMO_STATUS(...)`,
`getWCL_LOW_LATENCY_INFO_STATS(...)`, and `getWCL_TRAFFIC_COUNTERS(...)` by
the 2026-07-14 corrections below: their reference bodies require real
feature/owner/core work, so the local all-zero success carriers were not
stable contracts.

Closed in this zone:

- `getAWDL_RSDB_CAPS(...)`
- `getTKO_PARAMS(...)`
- `getTKO_DUMP(...)`
- `getBTCOEX_PROFILE(...)`
- `getBTCOEX_PROFILE_ACTIVE(...)`
- `getMAX_NSS_FOR_AP(...)`
- `getBTCOEX_2G_CHAIN_DISABLE(...)`
- `getBSS_BLACKLIST(...)`
- `getTXRX_CHAIN_INFO(...)`
- `getWCL_FW_HOT_CHANNELS(...)`
- `getRSN_XE(...)`

Recovered Apple behavior splits into three public buckets:

- fixed-fail selectors:
  `getBTCOEX_PROFILE -> 0xe00002c2`,
  `getTKO_PARAMS/getTKO_DUMP -> 0xe00002bc` when the keepalive owner is absent
- compact cache-backed carriers:
  `getRSN_XE`, `getBSS_BLACKLIST`
- state-backed telemetry carriers:
  `getAWDL_RSDB_CAPS`, `getBTCOEX_PROFILE_ACTIVE`,
  `getMAX_NSS_FOR_AP`, `getBTCOEX_2G_CHAIN_DISABLE`,
  `getTXRX_CHAIN_INFO`, `getWCL_FW_HOT_CHANNELS`

This batch intentionally stops at the public Apple80211 boundary:

- unsupported headers are removed for the remaining Q13 selectors
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
timesync setters. `setWCL_WNM_OPS(...)` and `setWCL_WNM_OFFLOAD(...)` are real
Apple producers, but each requires its own local no-backend quarantine rather
than a generic Apple-unsupported claim.

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
  writes a 32-bit scalar at caller offset `+4` from core state
  `(Core + 0x48) + 0x0`
- `AppleBCMWLANCore::getPOWER_BUDGET(apple80211_power_budget_t*)`
  writes a 32-bit scalar at caller offset `+4` from core state
  `(Core + 0x48) + 0x4`
- both producers return success directly; there is no hidden helper, WCL
  bulletin, or additional transport layer in between

That gives a sufficiently strong ABI recovery for the getter side:

- both payloads are 8-byte carriers
- offset `+0` is the standard `version`
- offset `+4` is the 32-bit payload value

This batch still does **not** justify lifting the setter side:

- `setTHERMAL_INDEX(...)` is not a cache-only carrier: it feature-gates a
  `tvpm` firmware transaction, validates the requested index, and updates
  core state only after its transport result permits that commit
- `getOFFLOAD_TCPKA_ENABLE(...)` remains unresolved because only the setter
  body is currently present in the vendor decompile

### `THERMAL_INDEX` correction: rejected setters must not manufacture getter state

The earlier minimal fixed-fail lift for `setTHERMAL_INDEX` retained a local
`cachedThermalIndex` write before returning `0xe00002bc`. That made a rejected
direct vtable request visible through the otherwise direct scalar getter.

The recovered Tahoe 25C56 setter is instead a feature-gated firmware path:

- Infra `0x100018760` dispatches through Core virtual `+0x4f0` to
  `AppleBCMWLANCore::setTHERMAL_INDEX` at `0x100120586`.
- When `featureFlagIsBitSet(0x3b)` is false, Core returns `0xe00002bc`.
  Enabled Core accepts exactly `1..100`, builds a 12-byte `tvpm` payload, and
  calls `runIOVarSet("tvpm")`.
- Core writes state `(Core + 0x48) + 0x0` only when the transport result is
  zero or special status `0xe3ff8117`, then returns that raw transport status.

The Intel port has no `tvpm` owner or matching transport. It now preserves the
local fixed-fail safety boundary without reading the rejected carrier and
removes the rejected-request cache. `getTHERMAL_INDEX` continues to provide
its established zero-initialized ABI carrier, but that zero is only the local
baseline; it is not a claim of dynamic Tahoe thermal-state parity.

## Q13 Confirmed Producer Mini-Batch: `getGUARD_INTERVAL`

`getGUARD_INTERVAL` is also a recoverable Tahoe getter, but the old symbol note
for `0xffffff800162176c` was stale: that address is a label inside
`getMCS_VHT`, not the standalone producer. The real Core entry recovered from
the target symbol map / pointer-scan decompile is
`AppleBCMWLANCore::getGUARD_INTERVAL(...) @ 0xffffff80016217bc`.

Recovered Apple producer contract:

- rejects `NULL` with `0xe00002c2`
- queries cached `"nrate"` from the same config path as `getMCS_VHT`
- returns the original nrate-query status unchanged
- if that query returns success or `0xe00002e3`, writes interval at caller
  offset `+0x04`
- VHT family `0x02000000` uses nrate bit 23: set -> `400`, clear -> `800`
- HT family `0x03000000` uses `(rate >> 10) & 3`: `0/1 -> 800`,
  `2 -> 1600`, `3 -> 3200`
- any other accepted nrate family falls back to `800`

The important architectural point for Tahoe is that slot `[478]` is a cached
nrate producer, not a peer-capability query and not a generic unsupported IOC.

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
- recovered `AppleBCMWLANCore::getMCS(...)` is a cached `nrate` carrier, not
  an association-gated helper or direct `ic_bss->ni_txmcs` read

That is enough to close the raw-fallback queue itself:

- `getMCS_INDEX_SET`
- `getNOISE`
- `getMCS`

now stop leaking raw POSIX `6` in both the Tahoe Skywalk path and the legacy
STA dispatcher.

The CR-120 forced-disassembly pass tightened the MCS source further:

- `AppleBCMWLANCore::getMCS(...)` at
  `0xffffff80016214c4..0xffffff8001621603` queries cached `"nrate"`
- success and `0xe00002e3` are accepted query outcomes
- rate family `0x01000000` publishes `rate & 0xff`
- rate families `0x02000000` and `0x03000000` publish `rate & 0x0f`
- the handler returns the original nrate query status rather than always
  returning success

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

The remaining `Q7` gap was the roam half of the WCL plane:

- `setWCL_REASSOC`
- `setWCL_LEGACY_ROAM_PROFILE_CONFIG`
- `setWCL_ROAM_PROFILE_CONFIG`

Recovered Apple paths show these are not disposable validate-and-ack slots.
They either:

- persist exact carrier/config payloads (`0x9c`, `0x60`, `0x23c`), or
- delegate into a local-owner-equivalent action we already have (`REASSOC_REQ`)

The port still lacks Apple's hidden roam/bgscan/keepalive helper objects, so
full helper choreography remains part of the broader hidden-owner surface. The
historical `Q7` closure remains valid for its out-of-line form, but it does not
make every copied carrier a complete owner implementation:

- the remaining WCL adapter methods are out-of-line implementations, not inline
  success stubs
- null requests return Apple `0xe00002bc`
- payload persistence is retained only where a matching local owner exists
- adapter-owned requests without that owner are reclassified rather than
  acknowledged by a partial local cache

`setWCL_ARP_MODE`, `setWCL_CONFIG_BG_MOTIONPROFILE`,
`setWCL_CONFIG_BG_NETWORK`, `setWCL_CONFIG_BGSCAN`, and
`setWCL_CONFIG_BG_PARAMS` are specifically reclassified below as no-local-
backend quarantines. Any still-missing hidden helper exactness belongs under
`Q13`, not under the old WCL adapter-stub bucket.

The historical list also included both profile variants because they had moved
out of inline stubs. Their later recoveries establish distinct RoamAdapter
policy/transport lifecycles, not reusable opaque caches; each is therefore
reclassified below as a no-local-backend quarantine. Reassociation remains a
separate scope.

## Q13 correction: WCL Roam Profile Config is RoamAdapter-backed

Tahoe 25C56 Infra wrapper `0x100018b74` dispatches virtual `+0x6d8` to Core
`0x100141e10`. Null reaches `0x1001a01a0` and returns `0xe00002bc`; non-null
selects RoamAdapter at `+0x15c0` and tail-jumps to
`setROAM_PROFILE_CONFIG` `0x10001c3f8`. That owner conditionally dispatches
three per-band records to `setRoamingProfileV6` `0x10001bfca`, drives
`join_pref` through `disable6GForRoamScans` `0x10001c5b0`, applies candidate
boost, configures multi-AP state, and uses Commander `roam_prof` requests with
async callbacks/status paths.

The modern-profile recovery demonstrates a RoamAdapter policy and transport
lifecycle and is reclassified. The port preserves the direct null error and
returns `kIOReturnUnsupported` for non-null input before reading the opaque
carrier, removing its dead 0x23c pseudo-layout/cache/flag/reset lines. No
generic STA `ROAM_PROFILE`, reassociation, scan, key, link, WCL event, or
generic adaptive-roaming property path changes. This makes no complete
carrier-layout, policy, Commander transport, completion, or valid-input
return-status parity claim. See
`docs/reference/CR-479-wcl-roam-profile-quarantine-20260714.md`.

## Q13 correction: WCL Legacy Roam Profile Config is RoamAdapter-backed

Tahoe 25C56 Infra wrapper `0x100018b28` dispatches virtual `+0x6d0` to Core
`0x100141de4`. Core selects RoamAdapter at `+0x15c0` and tail-jumps to
`setLEGACY_ROAM_PROFILE_CONFIG` `0x10001c272`. Null reaches `0x10019ffce`
and stores `0xe00002bc`; a non-null request calls `setRoamingProfile`
`0x10001a17e`, which selects V4 `0x10001a782` or V2 `0x10001b3c4` policy.
That lifecycle builds `roam_prof` Commander work through `sendIOVarSet`
`0x10017b900` with `handleRoamProfileAsyncCallBack` `0x10001bd9a`, then
configures Multi-AP state through `configureMultiAPBit` `0x10001c322` and
the `roam_multi_ap_env` callback `0x10001e809`.

The port preserves the direct null error and returns `kIOReturnUnsupported`
for non-null input before reading the former 0x60 pseudo-carrier. It removes
the unread cache/flag/reset lines and makes no complete carrier-layout,
policy, Commander transport, completion, or valid-input return-status parity
claim. No modern profile, generic STA `ROAM_PROFILE`, reassociation, scan,
key, link, WCL event, or generic adaptive-roaming property path changes. See
`docs/reference/CR-479-wcl-legacy-roam-profile-quarantine-20260714.md`.

## Q13 correction: WCL ARP mode is KeepAlive/WnmAdapter-backed

`setWCL_ARP_MODE(...)` is not a reusable direct OFFLOAD_ARP carrier. Tahoe
25C56 Infra wrapper `0x100018cec` tail-jumps to Core `0x1001e85e8`. Core
returns `0xe00002bc` for null. Its dword at carrier `+0x8` selects either Core
ARP keepalive or a KeepAliveOffload GARP owner; its enabled byte at `+0x10`
selects `programARPKeepAlive` `0x1000d9d6e` / `stopARPKeepAlive`
`0x1000d9cba` or `programGARP` `0x10009cdea` / `stopGARP` `0x10009d1e6`.
When both sideband bytes at `+0x4` and `+0x5` are nonzero, it tail-calls
WnmAdapter `configureWNMKeepAlives` `0x1000ad5a0` with the two u16s at `+0`
and `+0x2`.

The port preserves the direct null guard and returns `kIOReturnUnsupported`
for a non-null ARP-mode request before reading the carrier. It removes the
dead 0x14 pseudo-layout/cache/flag/reset lines and does not reuse the separate
direct `setOFFLOAD_ARP(...)` quarantine. This makes no complete carrier-layout,
mode-validity, keepalive/GARP/WNM transport, completion, or return-status
parity claim. See `docs/reference/CR-479-wcl-arp-mode-quarantine-20260713.md`.

## Q13 correction: BGScanAdapter-backed producer quarantines

`setWCL_CONFIG_BG_MOTIONPROFILE(...)` is not a standalone 0x40-byte cache.
Tahoe 25C56 Infra wrapper `0x10001921c` tail-jumps to Core `0x100142b46`.
Core returns `0xe00002bc` for null and otherwise selects its BGScanAdapter at
Core `+0x1578`, whose setter is `0x10000e856`. The adapter first calls
`configureMotionProfileMapping` `0x10000e96e`, which builds the `mpf_map`
Commander IOVAR through `runIOVarSet` `0x10017b6e6`; it then calls
`configureMotionProfilePNO` `0x10000eb3a` and
`configureMotionProfileEPNO` `0x10000ec9a`, whose recovered terminals submit
`pfn_mpfset` requests.

The PNO helper's `data + 1` condition is only one subpath among mapping, PNO,
EPNO, capability, and transport handling. It cannot justify a local byte gate
or cache-and-success substitute. The port preserves the direct null guard and
returns `kIOReturnUnsupported` for a non-null request before reading the
carrier; it removes the dead cache, flag, and local pseudo-layout. This makes
no full carrier-layout, valid-input/error, IOVAR-payload, or completion parity
claim. See
`docs/reference/CR-479-bg-motion-profile-quarantine-20260713.md`.

`setWCL_CONFIG_BG_NETWORK(...)` is likewise not a standalone 0x12c0-byte
cache. Tahoe 25C56 Infra wrapper `0x100019254` tail-jumps to Core
`0x100142b68`. Core returns `0xe00002bc` for null and otherwise selects the
same BGScanAdapter at `+0x1578`, whose setter is `0x10000ee46`. That adapter
clears PFN state with `pfnclear`, calls `configurePFN(0)` and
`configurePFNSuspend(0)`, caches adapter-owned state, then submits `pfn_set`,
`pfn_add`, and `pfn_add_bssid` through Commander `runIOVarSet`
`0x10017b6e6`, propagating status along the way.

The port preserves the direct null guard and returns `kIOReturnUnsupported`
for a non-null BG network request before reading its carrier. It removes the
dead 0x12c0 pseudo-layout/cache/flag and the setter-local scan-iterator
mutations, but it keeps the live scan-result fields and their other owners.
This makes no full carrier-layout, valid-input/error, PFN/IOVAR-payload, or
completion parity claim. See
`docs/reference/CR-479-bg-network-quarantine-20260713.md`.

`setWCL_CONFIG_BG_PARAMS(...)` is also not a standalone 0x20-byte cache.
Tahoe 25C56 Infra wrapper `0x1000192c4` tail-jumps to Core `0x100142bac`.
Core returns `0xe00002bc` for null and otherwise selects the same
BGScanAdapter at `+0x1578`, whose setter is `0x1000102a2`. When the first
sub-command is enabled, that setter calls `configureDynamicScanFreq` at
`0x1000103ec`; it constructs a 0x18-byte `pfn_override` request and submits
it through Commander `sendIOVarSet` with an async completion callback. When
the second sub-command is enabled, it calls `configureUnAssociatedScanTime` at
`0x100010504`, which sends the four-byte `scan_unassoc_time` request through
Commander `runIOVarSet` `0x10017b6e6` and retains the relevant status path.

The port preserves the direct null guard and returns `kIOReturnUnsupported`
for a non-null BG params request before reading its carrier. It removes the
dead 0x20 pseudo-layout/cache/flag and their reset lines. This makes no full
carrier-layout, sub-command validity, PFN/IOVAR-payload, async-completion, or
return-status parity claim. See
`docs/reference/CR-479-bg-params-quarantine-20260713.md`.

`setWCL_CONFIG_BGSCAN(...)` is not a generic net80211 start/stop control. Tahoe
25C56 Infra wrapper `0x10001928c` tail-jumps to Core `0x100142b8a`. Core
returns `0xe00002bc` for null and otherwise selects the same BGScanAdapter at
`+0x1578`, whose setter is `0x10000f852`. A nonzero byte 0 invokes
`configurePFN(0)` at `0x10000f516`; a nonzero byte 1 invokes `configPNO` at
`0x10000fa18` with byte 2, which issues the four-byte `scan_nprobes` Commander
request through `runIOVarSet` `0x10017b6e6`; and a nonzero byte 3 invokes
`configEPNO` at `0x10000fc20` with byte 4. Those branches retain adapter-owned
PFN/PNO/EPNO status and lifecycle handling rather than controlling the port's
generic scan flags.

The port preserves the direct null guard and returns `kIOReturnUnsupported`
for a non-null BGSCAN request before reading the carrier. It removes the dead
eight-byte pseudo-layout/cache/flag/reset lines and only the setter-local
generic bgscan mutations. It keeps generic net80211 scan fields and their
separate owners. This makes no full carrier-layout, branch-validity,
PFN/PNO/EPNO IOVAR-payload, completion, or return-status parity claim. See
`docs/reference/CR-479-bgscan-quarantine-20260713.md`.

## Q13 Batch: sideband carriers continue to leave the unsupported/stub tail

The next `Q13` reduction pass was intentionally narrow: only recovered producer
paths with simple caller-visible carriers were classified.

Recovered Apple evidence:

- `AppleBCMWLANCore::setPM_MODE(apple80211_pm_mode*)`
  forwards the dword at caller `+0x4` into
  `AppleBCMWLANNetAdapter::configurePM(...)`, which issues its own
  asynchronous PM IOC rather than a generic powersave cache update
- `AppleBCMWLANCore::setWCL_ROAM_USER_CACHE(apple80211_user_roam_cache*)`
  delegates into the roam adapter `cmdROAM_USER_CACHE(...)`; helper xrefs prove
  the caller-visible cache carries channel entries from `+0x0`, count at
  `+0x78`, and override state at `+0x7a`
- `AppleBCMWLANCore::setWCL_SET_SCAN_HOME_AWAY_TIME(scanHomeAndAwayTime*)`
  consumes a single dword and forwards it to the scan adapter owner

Scan-home-away retains its separately scoped behavior. The user-cache recovery
demonstrates a RoamAdapter lifecycle and is reclassified below rather than
preserved as a cache-and-success substitute.

- reject `NULL` with Apple `0xe00002bc`
- do not substitute a nearby local owner for an Apple-owned asynchronous
  transport path

PM_MODE correction: `configurePM(...)` maps a nonzero public mode to request
value `2`, sends four bytes through Commander IOC `0x56`, installs an async
completion callback, and returns its enqueue/transport status. The local
`setPOWERSAVE(...)` call was not that operation. With no equivalent Intel
owner, callback, or IOC transport, non-null PM_MODE requests now return
unsupported before a synthetic cache or powersave transition.

This does not close `Q13`, but it removes another class of simple
state-carrier mismatches from its tail.

## Q13 correction: WCL Roam User Cache is RoamAdapter-backed

`setWCL_ROAM_USER_CACHE(...)` is not a standalone 0x7c-byte cache. Tahoe
25C56 Infra wrapper `0x100018ca0` dispatches virtual `+0x6e0` to Core
`0x100141e52`, which selects RoamAdapter at `+0x15c0` and tail-jumps to
`cmdROAM_USER_CACHE` `0x10001c916`. The helper allocates 0x78 bytes of backend
state, validates the request, clears channel state at `0x10001cb3a`, adds
channels at `0x10001cc16`, conditionally changes override state at
`0x10001cd78`, and retains each status path. Its recovered carrier handling
uses channel entries from `+0x0` in 0x0c strides, channel count at `+0x78`,
and override-related bytes including `+0x7a`.

The port preserves the direct null guard and returns `kIOReturnUnsupported`
for a non-null user-cache request before reading the carrier. It removes the
dead pseudo-layout/cache/flag/reset lines and does not change reassoc, roam
lock, roam-profile, or generic adaptive-roaming platform-property paths. This
makes no full carrier-layout, channel validation, backend-state, transport,
completion, or return-status parity claim. See
`docs/reference/CR-479-wcl-roam-user-cache-quarantine-20260713.md`.

## Q13 Classification: sideband selectors must not advertise success

The remaining sideband tail was checked against the local Tahoe decompile
corpus and the remote Ghidra output. No producer symbols were found for
`setHEARTBEAT` or `setINTERFACE_SETTING`, so those selectors remain explicit
unsupported rather than advertising an ack-only success path.

## Q13 correction: WCL Roam Lock is RoamAdapter-backed

The roam-lock recovery demonstrates a RoamAdapter transport lifecycle and is
reclassified. Tahoe 25C56 Infra wrapper `0x100018adc` dispatches virtual
`+0x4b0` to Core `0x10011ed1e`. Null reaches cold path `0x1002082a6` and
returns raw `0x16`; non-null selects RoamAdapter at `+0x15c0`, reads byte 0 as
the boolean input, and tail-jumps to `setRoamLock` `0x10001e4e0`. The adapter
serializes a four-byte boolean `roam_off` request, sends it asynchronously via
Commander, and returns its enqueue/transport status through callback
`handleRoamOffAsyncCallBack` `0x10001e59e`.

The previous local byte-cache-and-success substitute is removed. The port
preserves the raw null error and returns `kIOReturnUnsupported` for non-null
input before reading it, because no local RoamAdapter owner, `roam_off`
transport, callback, or status lifecycle exists. No reassociation, scan,
key, link, WCL event, or generic adaptive-roaming property path changes. This
makes no full carrier-allocation, completion, transport, or valid-input
return-status parity claim. See
`docs/reference/CR-479-wcl-roam-lock-quarantine-20260714.md`.

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
- `setWCL_WNM_OPS` now returns `kIOReturnUnsupported` locally because its
  real WnmAdapter/commander effects have no matching local configurator
- `setWCL_WNM_OFFLOAD` separately returns `kIOReturnUnsupported` locally for
  its own missing WNM-offload configurator

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
  rejects `NULL` and enters the MIMO power-save owner path without mutating the
  public `getMIMO_STATUS` carrier; the neighboring core `+0x29f0` field belongs
  to `setPOWER_PROFILE`, not MIMO config
- `AppleBCMWLANCore::setFACETIME_WIFICALLING_PARAMS(...)`
  rejects `NULL`, reads a single status dword, invokes
  `setWiFiCallPolicies(...)`, and then returns success. The helper only enters
  `AppleBCMWLANPowerManager::setWiFiCallPowerPolicy(...)` when feature flag
  `0x2c` is enabled, using the PowerManager in Core's `+0x48` state block at
  `+0x1590`; its side effects are the operation rather than a Core cache write
- `AppleBCMWLANCore::setDUAL_POWER_MODE(...)`
  rejects `NULL`, persists two signed dwords at core `+0x4d3c/+0x4d40`, and
  then re-enters tx-power-cap state handling
- `AppleBCMWLANCore::setCONGESTION_CTRL_IND(...)`
  has no NULL return contract, reads effective byte `+0`, and writes it into
  traffic-monitor state `(Core + 0x48) + 0x89d2`
- `AppleBCMWLANCore::setLMTPC_CONFIG(...)`
  rejects `NULL`, stores a single byte at core `+0x4594`, and then re-enters
  the LMTPC owner
- `AppleBCMWLANCore::setLE_SCAN_PARAM(...)`
  is a direct Core statistics update: it reads enable byte `+0`, peak dword
  `+0x4`, total dword `+0x8`, and duty dword `+0xc`; enabled calls increment
  enabled/peak/total statistics, disabled calls increment only the disabled
  count, and duty values `0..6` increment their matching bucket. The optional
  object at Core's `+0x48` state block offset `+0x1588` only reports the
  completed statistics; it does not own the operation. Core directly
  dereferences the carrier and has no NULL return path.

That is strong enough to move these slots out of the generic
`kIOReturnUnsupported` bucket:

- `setWCL_ULOFDMA_STATE`
- `setMIMO_CONFIG`
- `setDUAL_POWER_MODE`
- `setLMTPC_CONFIG`
- `setLE_SCAN_PARAM`

The port still does not claim the deeper hidden owner choreography for 11ax,
WiFi-calling policy, tx-power-cap recalculation, or BTLE reporting. What
changes here is the architectural surface:

- these selectors are no longer dead unsupported slots
- Tahoe now preserves the same caller-visible direct state where the recovered
  body proves it, including LE_SCAN_PARAM's narrow cumulative statistics
- the remaining hidden-owner exactness stays open under the residual `Q13`
  hidden-helper zone instead of being conflated with missing selector bodies

## 2026-07-14 correction: `CONGESTION_CTRL_IND` is traffic-monitor state

The initial direct-carrier classification did not recover the consumer of
`setCONGESTION_CTRL_IND`. In the 25C56 DEXT, Infra wrapper `0x1000192fc`
reaches Core `0x1001429f4`, which reads only carrier byte `+0` and writes it
to `(Core + 0x48) + 0x89d2`. `collectRealTimeAppCongestionState()` at
`0x10013d482` returns that state at `0x10013d5a9`, and
`trafficMonitorCallback()` consumes the collector. This is not a standalone
QoS/DynSAR cache surface.

The local carrier, registry field, and sync helper had no local consumer or
traffic-monitor/WMM backend. The slot now retains its local NULL safety guard
and rejects every non-null carrier before reading it. This no-owner quarantine
does not claim Apple NULL or valid-input status parity; it removes only the
previous false local success/state assertion.

FACETIME_WIFICALLING_PARAMS correction: the former local status cache did not
perform Apple's WiFi-call policy/PowerManager action. Tahoe 25C56 Infra wrapper
`0x100019094` jumps directly to Core `0x100142714`, which calls
`setWiFiCallPolicies(...)` at `0x100139fbc` after the `NULL -> 0xe00002bc`
gate. The local port has no policy or PowerManager owner, so it preserves the
matching null rejection and returns unsupported for non-null input before any
synthetic cache mutation. This makes no Apple valid-input return-code or
feature-state parity claim.

LE_SCAN_PARAM correction: the former six-dword hidden-owner cache was not an
Apple behavior. Tahoe 25C56 Infra wrapper `0x100019414` jumps directly to
Core `0x100140d6c`, which updates counters in its `+0x48` state block and then
invokes `reportBTLECnxStats(...)` only when the optional reporter exists at
state offset `+0x1588`.
The port now mirrors the direct counter/bucket semantics without inventing a
reporter or owner. Its `NULL -> kIOReturnBadArgumentTahoe` guard is retained
only as a local safety divergence, not as Apple return-code parity.

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

## Historical Q13 LQM carrier lift (superseded by owner-boundary correction)

The earlier `Q13` lift treated the LQM carrier surface as a self-contained
public ABI:

- `getLQM_CONFIG`
- `setLQM_CONFIG`
- `getLQM_SUMMARY`
- `getLQM_STATISTICS`

That lift did not yet distinguish the carrier layout from the Core-owned LQM
configuration graph:

- `IO80211LQMData::getLQM_CONFIG(...)` exposes a fixed `0x24` carrier and
  mirrors one interval value into the first three dwords
- `IO80211LQMData::setLQM_CONFIG(...)` validates only two legal primary
  intervals (`1000` / `5000`) and keeps the caller-visible ABI fixed
- `AppleBCMWLANCore::setLQM_CONFIG(...)` rejects NULL and sub-1000 interval
  dwords with raw `0x16`; the separate `0x2d` path is the pre-validation
  feature-disabled gate, not an interval validation return
- `IO80211LQMData::getLQM_SUMMARY(...)` zeroes a fixed `0x15a0` summary blob
- `AppleBCMWLANCore::getLQM_CONFIG(...)` / `setLQM_CONFIG(...)` preserve the
  same public carrier shape while sourcing state from the vendor owner
- `AppleBCMWLANInfraProtocol::getLQM_STATISTICS(...)` is a direct
  `return 0xe00002c7;` stub on Tahoe

The following historical conclusion has been superseded by the current 25C56
owner-boundary recovery:

- `getLQM_CONFIG` previously exposed a local default carrier
- `setLQM_CONFIG` previously validated and cached a public blob
- `getLQM_SUMMARY` now returns the fixed zeroed summary payload rather than an
  unsupported error
- `getLQM_STATISTICS` is no longer treated as an "unknown missing producer":
  it is explicitly classified as Apple-unsupported on Tahoe

This historical lift did not claim that the full hidden Broadcom LQM owner was
lifted. Its final disposition is replaced by the correction below.

### 2026-07-14 correction: `LQM_CONFIG` is an owner-backed control surface

Current Tahoe 25C56 recovery shows that the Core endpoint cannot be lifted
from the carrier shape alone:

- Infra `setLQM_CONFIG` at `0x100018844` dispatches through Core virtual
  `+0x710` to `AppleBCMWLANCore::setLQM_CONFIG` at `0x100119d98`.
- `NULL` returns raw `0x16`; a non-null request can first take an opaque Core
  `+0x43f` `0x2d` gate. When feature bit `0x27` is absent, Core returns
  `0xe00002bc` before consuming the carrier.
- Only the enabled path validates all three interval fields, synchronizes
  eCounters, calls the LQM owner timer, and configures `rssi_event` and
  `chq_event` state/firmware paths. The latter configuration effects are real
  even where their helper status is not propagated to the public return.
- Core `getLQM_CONFIG` at `0x100119800` checks its LQM owner at
  `(Core + 0x48) + 0x15e8` before dereferencing output, and returns
  `0xe00002bc` if no owner exists.

The Intel port owns a separate LQM statistics timer and real `0x27` telemetry
producer, but not the eCounters/LQM/RSSI/channel-quality configuration graph.
`getLQM_CONFIG` therefore takes the recovered no-owner error boundary; the
setter preserves raw `0x16` for `NULL` and otherwise returns the local
no-backend error without reading, caching, or retuning from a public carrier.
That setter disposition is a feature-off-equivalent local quarantine, not a
claim that a missing Apple LQM owner would return the same status. This
intentionally removes dynamic public interval tuning. The internal 5000 ms
default timer, association lifecycle, and telemetry producer remain intact.

No Apple enabled-path success, opaque `+0x43f` gate, valid-input return-code,
or public config-to-telemetry mapping parity is claimed.

The non-config LQM results remain unchanged:

- `getLQM_SUMMARY` retains its fixed zeroed public producer
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
- `setSENSING_ENABLE`
- `setSENSING_DISABLE`
- `setDBRG_ENTROPY`

Recovered Apple evidence splits this zone into two families:

- real public producers plus one recovered public fail-contract:
  `setCHANNEL`, `setTXPOWER`, `setRATE`, `setIBSS_MODE`, `setOFFLOAD_ARP`,
  `setGAS_REQ`, `setTKO_PARAMS`, `setOFFLOAD_TCPKA_ENABLE`,
  `setSENSING_DISABLE`
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
  public `qtxpower` carrier through firmware, so a port without that owner
  must not acknowledge the operation by changing a getter cache
- `AppleBCMWLANCore::setRATE(...)` is a real `bg_rate` GET/SET/GET firmware
  producer, so a port without that owner must not acknowledge it through a
  local cache
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
  runs through a gated property callback path, not a direct unsupported slot;
  the later local callback quarantine below does not claim to lift that owner
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

- real public producer/carrier selectors expose recovered caller-visible gates
  only when their local owner exists; `TXPOWER` and `RATE` now fail closed
  rather than fabricating their missing firmware transitions
- the trap/debug selectors are classified out of the open discrepancy queue as
  internal-only Apple control paths rather than missing normal producer work
- the BSD IOCTL bridge now reaches the real setter bodies for the standard IOC
  subset (`CHANNEL`, `TXPOWER`, `RATE`, `IBSS_MODE`, `OFFLOAD_ARP`,
  `GAS_REQ`, `RESET_CHIP`, `CRASH`, `RANGING_*`, `TKO_PARAMS`,
  `OFFLOAD_TCPKA_ENABLE`)

`SET_PROPERTY` is excluded from this historical closure: its separate
correction below removes the local delegated-success claim because the required
callback control plane is absent.

## 2026-07-14 correction: `SET_PROPERTY` is a gated callback control plane

The canonical 25C56 DEXT dispatches
`AppleBCMWLANInfraProtocol::setSET_PROPERTY` at `0x100018914` through Core
virtual `+0x728` to `AppleBCMWLANCore::setSET_PROPERTY` at `0x1000da8de`.
There is no recovered NULL guard. The Core path uses the opaque carrier pointer
at `+0x8`, first checks `(Core + 0x48) + 0x7950` through virtual `+0x90`,
marks in-flight state at `(Core + 0x48) + 0x794b`, and returns the result of
Core virtual `+0x790`. Vtable entry `0x1003a1878` resolves that callback to
`AppleBCMWLANCore::setProperties(OSObject*)` at `0x1000da982`. When the direct
gate is unavailable, Core schedules the same work through virtual `+0x68`,
then command-gate virtual `+0x38` invokes `setPropertyIoctlGated` at
`0x1000da8aa`.

This is broad generic property control, not a narrow cache-only carrier. The
local type is only forward-declared and has no BSD IOC route or callback
backend; its old code only set an unread flag and returned success. The local
boundary now keeps its existing NULL safety error and rejects every non-null
carrier before access. It deliberately does not claim Apple NULL, callback, or
valid-input return-code parity.

See [CR-479-set-property-callback-quarantine-20260714.md](reference/CR-479-set-property-callback-quarantine-20260714.md).

## Q13 Batch: diagnostic/roam getter zone leaves the unsupported tail

The next `Q13` zone is the diagnostics / roaming / country-information cluster
that still sat on generic unsupported even though Tahoe already exposes a
recoverable public contract for it. This zone closed the following slots;
the former `getPOWER_DEBUG_INFO` and `getWCL_EXTENDED_BSS_INFO`
classifications are superseded by the 2026-07-14 no-backend corrections below:

- `getAWDL_PEER_TRAFFIC_STATS`
- `getROAM_PROFILE`
- `getCOUNTRY_CHANNELS`
- `getHW_SUPPORTED_CHANNELS`
- `getTRAP_CRASHTRACER_MINI_DUMP`
- `getBEACON_INFO`
- `getCHIP_DIAGS`
- `getCUR_PMK`
- `getCOUNTRY_CHANNELS_INFO`
- `getSENSING_DATA`
- `setVIRTUAL_IF_CREATE`
- `setBSS_BLACKLIST`
- `setREALTIME_QOS_MSCS`

Recovered Apple evidence splits this zone into three public classes:

- internal-only / trap / debug selectors that must not stay in the generic
  "missing producer" bucket:
  `getAWDL_PEER_TRAFFIC_STATS`, `getTRAP_CRASHTRACER_MINI_DUMP`,
  `getBEACON_INFO`
- concrete caller-visible carriers:
  `getROAM_PROFILE`, `getCOUNTRY_CHANNELS`, `getHW_SUPPORTED_CHANNELS`,
  `getCHIP_DIAGS`, `getCUR_PMK`, `getCOUNTRY_CHANNELS_INFO`,
  `getSENSING_DATA`
- delegated owner-backed selectors whose public null/fail contract is still
  recoverable even when the private owner stays unlifted:
  `setVIRTUAL_IF_CREATE`, `setBSS_BLACKLIST`, `setREALTIME_QOS_MSCS`

Public Apple-side facts used for this zone:

- `IO80211InfraProtocol_vtable_25D125.txt` marks slot `[470]` as an
  `AWDL internal stub`, which is enough to classify it out of the open
  public-system discrepancy queue
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
- `AppleBCMWLANCore::setVIRTUAL_IF_CREATE(...)` is not a generic unsupported
  setter: Tahoe exposes role-dependent public failures before the hidden
  proximity/AWDL/NAN/APSTA owners take over
- the CR-120 legacy-shadow pass mirrors that role fail shape for Tahoe-era
  targets as an interim no-owner shape; role 7 itself is now tracked as a
  required `IO80211SapProtocol` / APSTA owner reconstruction item rather than a
  permanent no-APSTA policy
- `AppleBCMWLANCore::setBSS_BLACKLIST(...)` was lifted in code;
  `setREALTIME_QOS_MSCS(...)` was initially classified from a cache stub, but
  the current QoS/MSCS correction above supersedes that classification

This zone closes on the system-facing boundary:

- the diagnostic/country getters no longer advertise generic unsupported when
  Apple already exposes a fixed public carrier or trap contract
- the restored `getCOUNTRY_CHANNELS_INFO` / `getHW_SUPPORTED_CHANNELS` routes
  keep Tahoe aligned with the same bridge-restoration principle used earlier
- `setBSS_BLACKLIST` remains part of that historic close-out; the
  `setREALTIME_QOS_MSCS` classification is superseded by the current
  QoS/MSCS no-local-backend quarantine above

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
- `setWOW_TEST` preserves the recovered local 1..600 rejection and quarantines
  the missing valid wake-test backend
- `setPOWER_BUDGET` preserves its local null/feature gates, recognizes the
  recovered 1..100 range, then quarantines the missing firmware-owner request
- `setUSB_HOST_NOTIFICATION` already preserved the public dword carrier
- `setHOST_CLOCK_INFO` is confirmed by decompile as a direct
  `AppleBCMWLANInfraProtocol::setHOST_CLOCK_INFO -> 0xe00002c7` stub

That leaves no remaining top-level queue debt after `Q12`; only runtime
verification remains.

Power Budget correction: the recovered valid range is 1..100, not its
complement. Tahoe then sends a 12-byte request through its `tvpm` firmware
owner and returns the transport status. The port has no equivalent owner, so
the valid local path returns unsupported without updating the cache; this is a
quarantine boundary, not Apple valid-input return-code parity.

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
- `setWOW_TEST(...)` preserves the visible range rejection but quarantines the
  missing retry/event-bit/firmware wake-test owner rather than treating success
  as a locally manufactured WoW-enabled state transition

The deeper hidden owner bodies behind `USB_HOST_NOTIFICATION`, `OFFLOAD_NDP`,
`BTCOEX_*` commander traffic, ranging auth, and tx-power-cap bypass still
remain backend-parity work, not queue debt.

WoW Test correction: valid mode requests use a retrying firmware owner in the
reference and only its successful completion enables WoW state. The port has
no equivalent owner or transport, so it returns unsupported for valid non-null
requests without a synthetic cache or `setWoWEnabled(true)` side effect.

TXPOWER/RATE correction: the qtxpower getter's BA-notification cache is an
observation path, not a substitute for the separate Apple setter transactions.
Apple uses `qtxpower` SET and `bg_rate` GET/SET/GET firmware operations; the
port has neither owner. Both non-null setters now return unsupported before
altering the BA-backed getter cache or a dead local rate cache.

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

- `setIE(...)` must reject `ie_len == 0` with raw `0x16`: the recovered Apple
  producer accepts only `1..0x800` bytes and places `ie[0]` at carrier `+0x14`;
  the WAPI branch additionally requires frame type `4`, nonzero `add`, and
  first byte `0x44`

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

2026-07-07 addendum: the same content contract is now shared between the Tahoe
controller producer and the legacy STA dispatcher shadow through
`TahoeCapabilityContracts::applyAppleConsistentCardCapabilityCluster()`. The
legacy shadow no longer publishes the old `0xEF / 0x2B / 0x8C` cluster.

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
4. the one remaining visible Fast Lane policy selector in that tail:
   `[632] setWCL_UPDATE_FAST_LANE`

The important correction is not that all `34` selectors suddenly became public
producers. They did not. The correction is narrower:

- there are now no inline `return kIOReturnUnsupported;` slot bodies left in
  `AirportItlwmSkywalkInterface.hpp`
- each former inline body now lives in `.cpp` with its reference class made
  explicit
- `[632] setWCL_UPDATE_FAST_LANE(...)` no longer hides behind a generic
  inline stub; it preserves the recovered null gate `NULL -> 0xe00002bc` and
  quarantines its missing non-null WME/ACM owner work

That leaves the Tahoe slot surface easier to audit: direct Apple stubs,
internal-only selectors, and real public producers are no longer conflated in
anonymous header placeholders.

Fast Lane correction: the normal wrapper return is reached only after Fast Lane
capability dispatch and, for the observed enabled pair, the NetAdapter WME/ACM
override submission. Since the port has no corresponding owner or firmware
transport, the explicit local body returns unsupported for non-null carriers
instead of reporting a false successful policy application.

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

## CR-115 Rejection: Skywalk Queue Layer ABI Surface

CR-115 failed structural review for a build/link reason, not because the active
Skywalk inventory layer was disproven.

The reviewer-built kext imported two symbols that the Tahoe BootKC does not
export:

- `IOSkywalkPacketQueue::getCapacity()` non-const mangling
- `IOSkywalkTxCompletionQueue::withPool(...)` with an `int`/`IOReturn`
  callback mangling

The current BootKC symbol surface proves the corrected ABI:

- `IOSkywalkTxCompletionQueue::withPool(...)` is exported with a `UInt32`
  callback (`PFj...`)
- `IOSkywalkTxCompletionQueue::initWithPool(...)` is exported with the same
  `UInt32` callback ABI
- `IOSkywalkPacketQueue::getCapacity()` is exported only as the const method
  `__ZNK20IOSkywalkPacketQueue11getCapacityEv`
- `IOSkywalkPacketQueue::getFreeSpace()` is exported only as the const method
  `__ZNK20IOSkywalkPacketQueue12getFreeSpaceEv`

The next batch therefore keeps the distinct TX completion queue, but corrects
the local header patching/build ABI to the exported `UInt32` callback form. It
also stops importing packet queue capacity methods for driver-owned constants:
local TX/RX queue capacity is already known at creation time and is stored in
the driver as queue metadata.

The AppleBCMWLAN decomp tightens the queue metric layer:

- `AppleBCMWLANSkywalkTxSubmissionQueue::getQueueDepth()` returns its custom
  queue ivar `+0x28`
- `AppleBCMWLANSkywalkTxSubmissionQueue::getCapacity()` returns the same custom
  queue ivar `+0x28`
- `AppleBCMWLANSkywalkRxCompletionQueue::getQueueCapacity()` returns custom
  RX queue ivar `+0x10`
- `AppleBCMWLANSkywalkTxSubmissionQueue::getRingFreeSpace()` returns `0`
- `AppleBCMWLANSkywalkTxSubmissionQueue::getPendingPacketCount()` returns `0`
- `AppleBCMWLANSkywalkInterface::packetSpace(index)` delegates to TX queue
  vtable `+0x340`
- `AppleBCMWLANSkywalkInterface::pendingPackets(index)` delegates to TX queue
  vtable `+0x348`

For the local one-TX-queue topology, the ABI-safe restored layer is:

- keep one TX submission queue and expose it through the interface accessor
- keep one distinct TX completion queue with the exported `UInt32` callback ABI
- keep one RX completion queue
- keep one multicast work source/event source
- expose queue depth/capacity from local queue construction metadata, not from
  a non-exported direct symbol
- return `0` for `pendingPackets()` and `packetSpace()`, matching the recovered
  Apple custom queue methods

This is the structural basis for the CR-116 resubmission. It still does not
claim that RSN/data completion is fixed; it only makes the restored active
Skywalk layer buildable and reference-aligned enough to reach approved runtime.

## APSTA State Scaffold

The APSTA/SoftAP owner audit now has one recovered structural layer encoded in
buildable code instead of YAML only.

`AppleBCMWLANIO80211APSTAInterface` is not a primary-STA shim. The role-7
factory allocates an APSTA object of size `0x138`, stores its private state
pointer at object offset `+0x130`, and later APSTA methods dereference fixed
offsets inside that state block. The local restoration now has
`AirportItlwmAPSTAStateBlock` and `AirportItlwmAPSTAObjectStorageLayout` as
compile-time witnesses for those recovered offsets.

The restored witness covers:

- SoftAP parameter fields at `+0x0e`, `+0x10`, `+0x18`, `+0x1c`, `+0x20`,
  `+0x24`, `+0x28`, `+0x2c`, and `+0x68`
- SoftAP stats copy source at `+0x1b0`, size `0x58`
- APSTA datapath fields at `+0x2a4`, `+0x2b8`, `+0x2d8`, `+0x2e0`,
  `+0x2e8`, `+0x2f0`, `+0x300`, and `+0x320`
- role-7 feature bits at `+0x32a` and `+0x32b`

This deliberately remains an offset scaffold. It does not advertise HostAP,
does not return role-7 success, and does not attach APSTA queues or BSD
registration. The next APSTA cycle is the `IO80211SapProtocol`/owner lifecycle
layer, so role-7 creation can be restored without vtable drift or guessed
storage.

## SAP Protocol Seam

The next APSTA pass recovered enough of the SAP protocol seam to prevent a
wrong local inheritance model.

The Tahoe `IO80211SapProtocol` vtable is not represented by the old local
`IO80211VirtualInterface.h`. The recovered vtable keeps the Skywalk prefix
through the `syncDPSStats` area and then exposes a SAP extension seam. APSTA
overrides typed SoftAP and station-management selectors in that seam:

- station/AP getters at slots `505..516`
- station/AP setters at slots `517..531`
- APSTA datapath overrides at slots `425..439`
- APSTA `forwardPacket(IO80211NetworkPacket*)` at recovered slot `465`

The important local consequence is that APSTA cannot be a direct subclass of
the current primary-STA header. Slot `465` is still named differently in the
local Skywalk header, so defining `IO80211SapProtocol` as a simple subclass of
that class would encode the wrong ABI even though it would compile.

`include/Airport/IO80211SapProtocol.h` is therefore a contract header for now:
it records slot numbers and typed carriers for the recovered SAP/APSTA surface,
is included through the V3/V2 Apple80211 umbrella, and deliberately does not
define the final C++ base class until the remaining slot aliases are resolved.
This keeps the restoration moving without creating a runtime class that would
shift APSTA selectors.

## APSTA Lifecycle State

APSTA lifecycle recovery tightened the private state scaffold from "covers the
known feature bits" to an exact allocation-size witness.

Recovered lifecycle methods prove the state block is `0x338` bytes:

- `free()` calls `freeResources()` and releases/zeroes `*(self+0x130)` with
  size `0x338`
- `freeResources()` releases state pointers at `+0x70`, `+0x78`, `+0x240`,
  `+0x248`, `+0x250`, `+0x258`, and `+0x260`
- `reset()` clears state `+0x26c`, byte `+0x329`, and zeroes the `+0xb8`
  runtime block for `0xf0` bytes
- `initSoftAPParameters()` clears the same `+0xb8` block and scalar `+0x1a8`
- reset/HostAP paths use the owner/core pointer at `+0x218`

The local `AirportItlwmAPSTAStateBlock` now names these lifecycle resources and
asserts exact size `0x338`. This still does not instantiate APSTA. It prevents
the next owner-lifecycle step from accidentally allocating a too-small state
block or leaking timer/work-source resources during teardown.

## APSTA Role-7 Creation Storage

Role-7 creation is now represented as a compiled storage contract, still
without enabling APSTA creation at runtime.

The recovered `AppleBCMWLANCore::setVIRTUAL_IF_CREATE(...)` branch has a fixed
shape:

- role field: `data+0x0c`
- APSTA role: `7`
- factory arguments: `core`, `data+0x04` MAC carrier, role `7`,
  `data+0x10` BSD-name carrier
- APSTA owner storage: core expansion `+0x2c30`
- adjacent proximity owner: core expansion `+0x2c28`
- duplicate owner: `0xe00002d2`
- creation failure: `0xe00002bd`
- unknown role: `0xe0000001`
- successful feature gates: `0x0d -> state+0x32a`,
  `0x0c -> state+0x32b`

`AirportItlwmAPSTAVirtualIfCreateCarrierLayout` and
`AirportItlwmAPSTACoreExpansionStorageLayout` now assert those offsets. This is
the safe prerequisite for the later role-7 runtime path: the storage contract is
fixed first, then the class, registration, queues, and resource creation can be
enabled without guessing where the owner lives.

## APSTA Datapath Activation Resource

The next APSTA pass split two remaining activation operands out of anonymous
state padding.

Reference APSTA does not start queues directly from the primary STA object. Its
activation path first calls a datapath owner/adapter stored at `state+0x2d0`:

- `enableDatapath()` calls vtable `+0x120` on `state+0x2d0`, then starts
  `state+0x2e8` and `state+0x2f0`, then arms the RX completion queue with
  vtable `+0x298`
- `disableDatapath()` calls vtable `+0x128` on `state+0x2d0`, then stops
  `state+0x2f0` and `state+0x2e8`
- missing completion queues still return `0xe00002bc`

The same state block also has a resource at `state+0x210`. A recovered helper
returns it directly, and APSTA event/SoftAP async callback paths use it for
bytestream payload traces.

`AirportItlwmAPSTAStateBlock` now names both fields:

- `resource210` at `+0x210`
- `datapathOwner2d0` at `+0x2d0`

This is still structural restoration only. The batch does not allocate APSTA,
does not create the datapath owner, does not start queues, and does not return
role-7 success.

## APSTA Init And Start Resources

APSTA factory/start recovery now covers the producer side for the state fields
used by the datapath accessors.

`withOptions(core, mac, role, bsdName)` allocates an APSTA object of size
`0x138`, installs the APSTA vtable, and calls
`init(core, mac, role, bsdName)`. The recovered `init` path allocates private
state and fills the owner/resource slots before any registration can happen:

- owner/core pointer at `+0x218`
- timer sources at `+0x70` and `+0x78`
- bytestream/logger resource at `+0x210`
- resource references at `+0x228`, `+0x240`, `+0x248`, `+0x250`, `+0x258`,
  and `+0x260`
- defaults at `+0x14`, `+0x20c`, `+0x268`, `+0x2a4`, and `+0x2a8..+0x2b4`

`start(core, RegistrationInfo*)` then records the work queue at `+0x330`,
stores the bus/provider at `+0x2c8`, resolves the datapath owner at `+0x2d0`,
and calls datapath owner vtable `+0x118` with a stack configuration pointing at
APSTA queue/pool storage fields:

- `+0x2a8` service-class map
- `+0x300` TX subqueue array
- `+0x2f8` additional resource storage
- `+0x2e8` TX completion queue storage
- `+0x2f0` RX completion queue storage
- `+0x320` multicast queue storage
- `+0x2d8` TX packet pool storage
- `+0x2e0` RX packet pool storage

`AirportItlwmAPSTAStartQueueConfigLayout` records this stack-carrier shape.
This still stops at compiled witnesses: no APSTA allocation, no queue creation,
no BSD registration, and no role-7 success is enabled in this batch.

## APSTA HostAP Control State

The AP-mode pass identifies the local state fields used by the HostAP control
path before any firmware IOVAR sequence is implemented locally.

Reference APSTA keeps AP/SoftAP state in fixed bytes and dwords inside the same
private state block:

- `+0x0c`: AP power assertion flag, written by `holdSoftAPPowerAssertion()`
- `+0x0d`: hidden-network flag, written by `setHOST_AP_MODE_HIDDEN(...)`
- `+0x0e`: AP enabled/visible flag, cleared on AP shutdown paths
- `+0xb4`: AP low-power/power state checked by `configureLowPowerModeExit()`
- `+0x26c`: AP-up state used by `setHostApModeInternal(...)`
- `+0x270`: HostAP transition/in-progress state used by
  `setHostApModeInternal(...)`

`setHOST_AP_MODE(...)` wraps the internal state machine by bringing down or
bringing up proximity/NAN owners around the AP transition, gated by feature
`0x46`. The local restoration only names and asserts these fields; it does not
advertise HostAP, does not send firmware AP IOVARs, and does not return role-7
success yet.

## APSTA RegistrationInfo Carrier

The role-7 producer path also has a fixed APSTA registration carrier before it
calls `AppleBCMWLANIO80211APSTAInterface::start(core, info)`.

After creating the APSTA owner and applying feature gates, reference
`setVIRTUAL_IF_CREATE(...)` zeroes a `0x130`-byte stack `RegistrationInfo`,
calls APSTA `initRegistrationInfo(info, 1, 0x130)`, then writes APSTA-specific
fields:

- `+0x0c`: interface subfamily `3`
- `+0x14`: registration type/value `2`
- `+0x24`: options qword `0x8000000080`
- `+0x30`: BSD prefix pointer `"ap"`
- `+0x38`: BSD unit `1`
- `+0x40`: power/reset flags, either `6` or existing flags OR `0x4`
- `+0x108`: six-byte APSTA hardware address

`AirportItlwmAPSTARegistrationInfoLayout` records this carrier without
changing runtime behavior. The local path still does not allocate APSTA, call
`start`, or return role-7 success.

## APSTA Ethernet Queue List

Inside APSTA `start`, reference builds a second carrier before calling
`registerEthernetInterface`.

The APSTA default is four TX submission queues. `start` copies those pointers
from `state+0x300..+0x318`, then appends:

- `state+0x2e8`: TX completion queue
- `state+0x2f0`: RX completion queue

The resulting registration queue list has six entries. The register call uses
`numTxQueues + 2` and the APSTA packet pools at `state+0x2d8` and `state+0x2e0`.
On failure, reference removes work sources for all TX queues, both completion
queues, and the multicast queue.

`AirportItlwmAPSTARegisterQueueListLayout` records this topology so the later
runtime APSTA start path cannot collapse to the primary STA one-queue model.

## APSTA Public SoftAP/SAP Carrier Contract

The public SAP/SoftAP methods on `AppleBCMWLANIO80211APSTAInterface` use fixed
carrier offsets and raw return codes. These are now recorded as compiled
witnesses before a real APSTA owner can be safely enabled.

Reference constants recovered from APSTA public methods:

- `getSTATE(...)`: output `+0x04 = 4`
- `getPEER_CACHE_MAXIMUM_SIZE(...)`: output `+0x04 = 8`
- `getHOST_AP_MODE_HIDDEN(...)`: NULL returns raw `0x16`, non-NULL writes `1`
- `setSOFTAP_TRIGGER_CSA(...)`: AP not ready returns `6`, NULL returns raw
  `0x16`, parsed channel specs below `0x10000` are accepted for IOVAR `csa`,
  and parsed channel specs at or above `0x10000` return raw `0x16`
- `setSOFTAP_WIFI_NETWORK_INFO_IE(...)`: invalid length returns `0xe00002c2`
- `setRSN_CONF(...)`: `state+0x29b` bit `0x10` set returns `0xe00002d5`
- `setSTA_AUTHORIZE(...)`: NULL returns `0xe00002c2`

`AirportItlwmAPSTASoftAPParamsOutputLayout` records the exact
`getSOFTAP_PARAMS(...)` output carrier:

- `+0x04`: copied from `state+0x18`
- `+0x08`: copied from `state+0x1c`
- `+0x0c`: copied from `state+0x20`
- `+0x10`: copied from `state+0x24`
- `+0x14`: copied from `state+0x68`
- `+0x16`: copied from `state+0x10`
- `+0x17`: copied from `state+0x0e & 1`
- `+0x18`: copied from `state+0x28`

`AirportItlwmAPSTASoftAPWifiNetworkInfoCarrierLayout` records the
`setSOFTAP_WIFI_NETWORK_INFO_IE(...)` input shape: byte `+0x03` must be below
`0x21`, and the accepted carrier copy size is `0x24`.

The state byte `rsnConfGate29b` records the recovered RSN_CONF gate at
`state+0x29b`. This remains a structural APSTA restoration: no APSTA object is
instantiated, no AP path is enabled, and no RSN/data success is forced.

## SAP/APSTA Vtable Alias Guard

The APSTA owner cannot be implemented as a naive subclass with guessed virtual
method order. The recovered vtables show a concrete slot alias that must be
preserved.

`IO80211SapProtocol` base vtable:

- recovered range: slots `280..519`
- SAP extension first slot: `481`, byte offset `0x0f08`
- base virtual-interface `forwardPacket`: slot `483`, byte offset `0x0f18`

`AppleBCMWLANIO80211APSTAInterface` concrete vtable:

- concrete APSTA `forwardPacket`: slot `465`, byte offset `0x0e88`
- concrete APSTA `reset`: slot `481`, byte offset `0x0f08`
- concrete APSTA `setMacAddress`: slot `488`, byte offset `0x0f40`
- concrete APSTA public methods continue through `setMIS_MAX_STA` at slot
  `531`, byte offset `0x1098`

This is now guarded in `IO80211SapProtocol.h` with separate base
`IO80211SapProtocol` constants and concrete `AppleBCMWLANAPSTA` slot aliases.
The key invariant is that APSTA `forwardPacket` remains slot `465`; it must not
be moved to the base `IO80211VirtualInterface::forwardPacket` slot `483` when
the real APSTA owner class is introduced.

## APSTA Forward Packet Queue Selection

The concrete APSTA TX forwarding path does not use the primary STA single-queue
shortcut. `AppleBCMWLANIO80211APSTAInterface::forwardPacket(...)` selects a TX
submission queue from APSTA private state and dispatches directly into that
queue.

Recovered reference sequence:

- call packet metadata helper on the `IO80211NetworkPacket`
- compute queue pointer offset as `(metadata >> 4) & 0xff8`
- load the queue pointer from `state+0x300 + offset`
- call selected queue vtable entry `+0x318` with the original packet

The local scaffold now records these operands:

- selector shift: `4`
- selector mask: `0xff8`
- TX subqueue base: `state+0x300`
- selected queue submit vtable offset: `0x318`

This remains a non-runtime witness. It does not enable APSTA forwarding and does
not introduce a fallback queue.

## APSTA Datapath Metric Accessors

The APSTA metric accessors also use APSTA-specific queue internals instead of a
generic queue-capacity API.

Recovered reference behavior:

- `getTxHeadroom()` returns `0`
- `getTxQueueDepth()` reads TX queue pointer `state+0x300`
- missing TX queue returns `0`
- present TX queue reads nested pointer `queue+0x168` and returns dword
  `nested+0x28`
- `getRxQueueCapacity()` reads RX completion queue pointer `state+0x2f0`
- missing RX completion queue returns `0`
- present RX completion queue reads nested pointer `queue+0x138` and returns
  dword `nested+0x10`

The local APSTA scaffold now records those constants. This is still structural:
it does not call unexported capacity APIs and does not synthesize live queue
metrics before APSTA queues exist.

## APSTA Datapath Lifecycle Vtable Offsets

APSTA datapath activation is owner-driven. It must not be collapsed into direct
primary-STA queue toggling.

Recovered `enableDatapath()` sequence:

- call datapath owner `state+0x2d0` vtable `+0x120`
- start TX completion queue `state+0x2e8` via vtable `+0x150`
- start RX completion queue `state+0x2f0` via vtable `+0x150`
- arm RX completion queue via vtable `+0x298` with arguments `0,0`
- return `0xe00002bc` if a required completion queue is missing

Recovered `disableDatapath()` sequence:

- call datapath owner `state+0x2d0` vtable `+0x128`
- stop RX completion queue `state+0x2f0` via vtable `+0x158`
- stop TX completion queue `state+0x2e8` via vtable `+0x158`

The scaffold now records those offsets and the missing-queue return. It still
does not allocate the owner or start/stop any APSTA queues.

## APSTA Start Work-Source Registration

Before APSTA registers its Ethernet interface, reference makes every APSTA queue
that will participate in the registration a work source on the APSTA work
queue.

Recovered start-time sequence:

- datapath owner configuration call: `state+0x2d0` vtable `+0x118`
- queue-count guard: `numTxQueues >= 7` enters the invalid/trap path
- optional multicast queue `state+0x320` is added to work queue `state+0x330`
- each TX submission queue from `state+0x300` is added via work queue vtable
  `+0x140`
- TX completion `state+0x2e8` is added via work queue vtable `+0x140`
- RX completion `state+0x2f0` is added via work queue vtable `+0x140`
- `registerEthernetInterface` receives `numTxQueues + 2`, TX/RX pools
  `state+0x2d8/+0x2e0`, and flags `0`
- on registration failure, reference removes TX queues, TX completion, RX
  completion, and multicast through work queue vtable `+0x148`

The local APSTA scaffold now records those operands. It still does not call
`start`, add work sources, or register the APSTA Ethernet interface.

## APSTA Teardown Resource Cleanup

The start-time APSTA queue/work-source contract has a matching teardown
contract. Reference does not rely on the primary STA queue cleanup path for
APSTA-owned queues.

Recovered `freeResources()` sequence:

- `state+0x70`: cancel via vtable `+0x158`, release via vtable `+0x28`, clear
- `state+0x78`: cancel via vtable `+0x158`, release via vtable `+0x28`, clear
- `state+0x240/+0x248/+0x250/+0x260/+0x258`: release through vtable `+0x28`
  when present and clear each field

Recovered `stop(IOService*)` sequence:

- iterate TX submission queues from `state+0x300` while `i < state+0x2a4`
- for each non-null TX queue: stop via queue vtable `+0x158`, remove from work
  queue via vtable `+0x148`, release via vtable `+0x28` if still present, clear
- repeat the same stop/remove/release/clear sequence for `state+0x2e8` TX
  completion and `state+0x2f0` RX completion
- for `state+0x320` multicast queue: stop, remove through work queue vtable
  `+0x148`, call direct `IO80211WorkQueue::removeWorkSource`, release, clear
- tailcall super stop through vtable offset `+0x5d8`

The scaffold now records these constants and field aliases. It still does not
instantiate APSTA or call APSTA teardown at runtime.

## APSTA Reset And SoftAP Defaults

The APSTA reset/default layer is separate from queue teardown. Reference resets
SoftAP state with fixed field writes before AP mode is brought up.

Recovered `reset()` sequence:

- clear `state+0x26c` and byte `state+0x329`
- call `AppleBCMWLANCore::setConcurrencyState(4, false)` through owner/core
  `state+0x218`
- zero `state+0xb8` for `0xf0` bytes
- clear `state+0x0` and qword `state+0xb0`
- call `setPowerSaveState(0, 0xa)`
- invoke timer sources `state+0x70` and `state+0x78` through vtable `+0x218`
- clear stats `state+0x1b0..+0x207`
- clear runtime qwords `state+0x90`, `state+0x98`, and `state+0xa0`

Recovered `initSoftAPParameters()` sequence:

- clear stats `state+0x1b0..+0x207`
- clear `state+0x1a8`
- zero `state+0xb8` for `0xf0` bytes
- clear `state+0x0`
- write SoftAP defaults `state+0x16 = 1`, `state+0x18 = 0x0f`,
  `state+0x1c = 0x1e`, `state+0x20 = 0x708`, `state+0x24 = 0x0a`,
  `state+0x28 = 3`
- call `setBeaconInterval(state+0x14)`
- if `state+0x16 != state+0x6a`, apply DTIM through IOCTL `0x4e` via commander
  `state+0x228`, then write `state+0x6a = state+0x16` on success

The local scaffold now records those defaults and aliases. It still does not
run APSTA reset or SoftAP initialization at runtime.

## APSTA Beacon And DTIM IOCTL Carriers

The APSTA SoftAP defaults are applied through separate beacon interval and DTIM
command paths. Both use a small payload head and distinct IOCTL numbers.

Recovered `setBeaconInterval(uint16_t)` sequence:

- compare requested interval against applied interval `state+0x68`
- skip IOCTL when equal
- build command payload with data pointer at `+0x0` and length word at `+0x8`
- payload length is `4`
- use commander `state+0x228`
- async path installs `handleSetBcnIntervalAsyncCallBack` at callback context
  `+0x8` and cookie `0` at `+0x10`
- send/run IOCTL `0x4c`
- on success, write requested interval to `state+0x68`

Recovered DTIM apply sequence:

- source DTIM field is `state+0x16`
- applied DTIM field is `state+0x6a`
- skip IOCTL when equal
- use the same payload head with length `4`
- use commander `state+0x228`
- async path installs `handleSetBcnDTIMPeriodAsyncCallBack` at callback context
  `+0x8` and cookie `0` at `+0x10`
- send/run IOCTL `0x4e`
- on success, write `state+0x6a = state+0x16`

Both callbacks return immediately for status `0`. For nonzero status, they log
through the interface logger and emit rxPayload bytestream telemetry through
`state+0x210`, reading payload pointer `+0x0` and length `+0x8`.

## APSTA HostAP Success Tail

After AP bring-up succeeds, reference executes a fixed success-tail before
reinitializing SoftAP parameters.

Recovered success-tail sequence:

- write `state+0x26c = 1`
- clear `state+0x20c` and `state+0x88`
- call `handleAPStatsUpdates(state+0x70)`
- schedule monitor timer `state+0x78` through vtable `+0x1d0` with interval
  `0x3e8`
- read network-data flags at input `+0x4`
- if flags bit `8` is set, write beacon interval `0x64` to `state+0x14`;
  otherwise write `0x12c`
- if flags bit `9` is set, run IOVAR `closednet` through commander
  `state+0x228` with 4-byte payload value `1`
- continue to `initSoftAPParameters()`

The local scaffold records these operands only; it still does not enable
HostAP, force AP-up state, schedule timers, or send `closednet`.

## APSTA HostAP Max-Assoc And Vendor IE Layer

The HostAP success tail is preceded by a separate AP policy/IE programming
layer. Reference does not fold this into generic STA association handling.

Recovered max-assoc sequence:

- read max-assoc through `state+0x218 -> +0x128 -> +0x1558 -> +0x10 -> +0xb4`
- store it at `state+0x8`
- call `setMaxAssoc(unsigned int)`
- call APSTA vtable `+0xb18` with selector `0x57`, payload `state+0x8`,
  payload size `4`, and flags argument `0`
- in `setMaxAssoc`, compute firmware payload as `state+0x0 + requested`
- skip firmware programming if requested already equals `state+0x4` or the
  computed payload exceeds `state+0x8`
- otherwise write `state+0x4 = requested` and send IOVAR `maxassoc` with
  4-byte payload through commander `state+0x228`

Recovered vendor IE sequence:

- network-data vendor IE length is `network_data+0x2dc`
- network-data vendor IE payload starts at `network_data+0x2e0`
- nonzero length calls `programVendorIEList`
- zero length calls `programAppleVendorIE`
- `programVendorIEList` consumes IE chunks with header size `2`, minimum
  remaining size `6`, and per-IE allocation size `0x814`
- the per-IE carrier writes fixed qwords `0x1a00000001` and `0x400000001`,
  stores the IE id at `+0x14`, copies IE bytes to `+0x15`, and calls
  `AppleBCMWLANCore::setVendorIE`

Recovered Apple vendor IE sequence:

- command IOVAR is `vndr_ie`
- set buffer size is `0x52`
- existing Apple OUI entries are deleted with command `del`
- the fixed Apple capability IE is added with command `add`, header qword
  `0x700000001`, payload size `0x18`, and body header qword
  `0x10106f217000add`
- feature flag `0x46` controls use of extended capability fields stored in
  APSTA state at `+0x2c/+0x2e/+0x2f/+0x30/+0x50/+0x51/+0x59`

The scaffold now records these operands and carrier layouts. It still does not
send `maxassoc`, `setVendorIE`, `vndr_ie`, or selector `0x57` at runtime.

## APSTA Enable AP Interface Layer

After the HostAP success tail and concurrency setup, reference enters a
dedicated AP-enable layer before final AP event publication.

Recovered `enableAPInterface()` sequence:

- feature flag `0x15` plus config byte `+0xe2` gates RRM AP-disable commands
- `rrm_bcn_req_thrtl_win` is sent with 4-byte payload value `0`
- `rrm_bcn_req_max_off_chan_time` is sent with 4-byte payload value `0`
- feature flag `0x19` plus config byte `+0xe3` gates WNM AP-disable command
- `wnm` is sent with 4-byte payload value `0`
- boot arg `wlan.ap.maxmpdu` is read with size `4`
- failed boot-arg read maps to `configureMPDUSize(0xffffffff)`
- successful nonzero boot-arg read maps to `configureMPDUSize(value)`
- successful zero boot-arg read skips MPDU override
- core private field `+0x2890` is ORed with `0x10000`
- APSTA vtable `+0xe70` is called with arguments `(2, 1)`
- `scb_probe` uses payload qword `0xf0000001e`, dword `5`, and size `0x0c`
- async `scb_probe` path installs completion owner/callback/cookie at
  `+0x0/+0x8/+0x10`
- sync `scb_probe` path uses the same payload with `runVirtualIOVarSet`
- core notification uses event id `0x1e`, optional interface name capped below
  length `0x11`, and flag `1`
- APSTA vtable `+0xb18` is called with selector `4`
- core event bit `5` is added, then `writeEventBitField()` is called

The scaffold records these AP-enable operands only. It still does not send
RRM/WNM/MPDU/scb_probe commands, publish AP link-up, or write event bits at
runtime.

## APSTA Hidden Mode And Power Assertion Layer

Hidden-mode updates are a public APSTA setter and are not just a copy of the
HostAP success-tail `closednet` default.

Recovered `setHOST_AP_MODE_HIDDEN()` sequence:

- require AP-up state `state+0x26c != 0`
- return `6` when AP is not up
- return raw invalid argument `0x16` for null input
- read hidden value from input `+0x4`
- accept only values `0` and `1`
- send IOVAR `closednet` through commander `state+0x228`
- `closednet` payload size is `4`
- on success, write `state+0x0d = (hidden != 0)`
- if hidden is cleared and AP remains up, call `setPowerSaveState(0, 9)`
- clear `state+0x0e`
- call `holdSoftAPPowerAssertion()`

Recovered `holdSoftAPPowerAssertion()` sequence:

- write `state+0x0c = 1`
- notify core path using resource `state+0x218 -> +0x128 -> +0x2c20`
- notification event id is `0x8d`
- payload is 4-byte value `1`
- notification flag is `1`

The scaffold records these hidden/power operands only. It still does not send
`closednet`, change power-save state, or hold power assertions at runtime.

## APSTA Channel, CSA, And STA-Control Layer

The APSTA public channel/CSA/STA-control methods use fixed carriers and vtable
slots. They are not generic primary-STA fallbacks.

Recovered `getCHANNEL()` sequence:

- use virtual IOCTL get selector `0x1d`
- build a 0x0c-byte RX payload initialized with `0xaa`
- use RX payload range qword `0x0000000c000c000c`
- copy received channel number from RX carrier `+0x04` to output `+0x08`
- OR output flags `+0x0c` with `0x08` below channel `0x0f`, otherwise `0x10`

Recovered `setCHANNEL()` sequence:

- NULL returns raw `0x16`
- input channel at `+0x08` must be below `0x100`
- input flags at `+0x0c` map bandwidth `0x02 -> 2`, `0x04 -> 3`,
  `0x400 -> 4`
- default bandwidth comes from `state+0x218 -> +0x128 -> +0x408`
- zero chanspec returns `0xe00002c2`
- nonzero chanspec is sent as 4-byte IOVAR `chanspec`

Recovered `setSOFTAP_TRIGGER_CSA()` sequence:

- require `state+0x26c != 0` and `state+0x329 & 1`
- NULL returns raw `0x16`
- optional core call uses feature `0x46`, private byte `+0x4d59 & 1`, and
  vtable `+0x1110`
- parsed chanspec values below `0x10000` are accepted
- parsed chanspec values `>= 0x10000` return raw `0x16`
- accepted path sends 6-byte IOVAR `csa`

Recovered STA-control sequence:

- `setSTA_AUTHORIZE(...)` uses MAC at input `+0x08`, NULL return
  `0xe00002c2`, and selectors `0x79/0x7a` selected from flag `+0x04`
- `setSTA_DISASSOCIATE(...)` is APSTA slot `522`, byte offset `0x1050`, and
  uses virtual IOCTL set selector `0xc9` with a 0x0c-byte payload ending in
  sentinel word `0xaaaa`
- `setSTA_DEAUTH(...)` is APSTA slot `523`, byte offset `0x1058`, and tailcalls
  byte offset `+0x1040`

The scaffold records these constants and carriers only. It still does not send
channel, CSA, or STA-control commands at runtime.

## APSTA HostAP Control And Power Layer

The HostAP mode wrapper is a separate owner/gate layer around
`setHostApModeInternal(...)`, not a primary-STA mode flag.

Recovered `setHOST_AP_MODE()` wrapper:

- read network-data mode at input `+0x1c`
- read proximity owner from core-private `+0x2c28`
- read NAN owner from core-private `+0x74f0`
- read NAN data owner from core-private `+0x74f8`
- when input is non-null and mode `+0x1c` is nonzero, feature gate `0x46`
  controls the pre-internal bringdown of proximity/NAN owners
- when input is null or mode `+0x1c` is zero, call `setHostApModeInternal`
  first, then feature gate `0x46` controls post-internal bringup
- bringup additionally requires core-private `+0x2890 & 1` and mode dword
  `+0x4d8c` equal to `4` or `1`

Recovered `hostAPPowerOff()` sequence:

- return `0` if AP-up state `state+0x26c` is zero
- if associated station count `state+0x00` is zero:
  - call `setPowerSaveState(0, 0x0c)`
  - clear `state+0x0e`
  - call `setHostApModeInternal(NULL)`
  - notify core event id `1`, null payload, payload size `0`, flag `1`
- otherwise, if SoftAP concurrency is disabled, call `setPowerSaveState(3, 3)`

Recovered concurrency and low-power gates:

- `isSoftAPConcurrencyEnabled()` requires feature `0x46` and core-private byte
  `+0x4d59 & 0x1b`
- `configureLowPowerModeExit()` returns immediately when `state+0xb4 == 0`
- active low-power exit dispatches through work-queue vtable `+0x130`
- successful low-power exit clears `state+0xb4`

The scaffold records these HostAP control/power operands only. It still does
not execute HostAP mode, power-off, concurrency, or low-power runtime paths.

## APSTA Public SAP Slot Surface

The concrete APSTA public SAP method surface is fixed in the resolved
AppleBCMWLAN APSTA vtable. A local owner class must not leave any of these
methods at base/reserved offsets.

Recovered getter slot range:

- slots `505..516`
- byte offsets `0x0fc8..0x1020`
- methods from `getSSID` through `getSOFTAP_STATS`

Recovered setter slot range:

- slots `517..531`
- byte offsets `0x1028..0x1098`
- methods from `setSSID` through `setMIS_MAX_STA`

`include/Airport/IO80211SapProtocol.h` now records the full public APSTA slot
surface as constants and byte-offset static asserts. This remains an ABI
scaffold; it does not define the final APSTA owner class or route runtime calls
through those slots.

## APSTA Public Simple Body Layer

The simple public APSTA method bodies have fixed state/output contracts. They
are not primary-STA fallbacks and they do not infer values from the current
infrastructure interface.

Recovered getter body sequence:

- `getSSID(...)` reads length from `state+0x274`, rejects lengths greater than
  `0x20` with raw `0x16`, writes output length at `+0x04`, copies SSID bytes
  from `state+0x278` to output `+0x08`, and returns `0`
- `getSTATE(...)` writes value `4` at output `+0x04` and returns `0`
- `getOP_MODE(...)` rejects NULL with raw `0x16`, writes type `1` at output
  `+0x00`, writes mode `8` at output `+0x04` when `state+0x26c` is nonzero,
  otherwise writes mode `0`, and returns `0`
- `getPEER_CACHE_MAXIMUM_SIZE(...)` writes value `8` at output `+0x04`
- `getHOST_AP_MODE_HIDDEN(...)` rejects NULL with raw `0x16`, writes value `1`
  at output base, and returns `0`
- `getSOFTAP_PARAMS(...)` copies fields from APSTA state
  `+0x18/+0x1c/+0x20/+0x24/+0x68/+0x10/+0x0e/+0x28` to fixed output offsets
  and returns `0`
- `getSOFTAP_STATS(...)` copies `0x58` bytes from `state+0x1b0` and returns
  `0`

Recovered setter body sequence:

- `setSSID(...)` performs optional logging only and returns `0`
- `setPEER_CACHE_CONTROL(...)` calls `completePeerCacheControl(input, self)`
  through `state+0x218`, ignores the helper result, and returns `0`
- `setSOFTAP_PARAMS(...)` has no null guard, uses input
  `+0x04/+0x08/+0x0c/+0x10/+0x14/+0x17/+0x18`, updates state
  `+0x0e/+0x18/+0x1c/+0x20/+0x24/+0x28/+0x68/+0x26c`, uses beacon sentinel
  `0xffff`, and performs only the reference power-save calls `(0,0)` and
  `(1,0)`
- `setSOFTAP_EXTENDED_CAPABILITIES_IE(...)` clears state
  `+0x50/+0x58/+0x60`, copies input `+0x00/+0x01/+0x09` to state
  `+0x50/+0x51/+0x59`, and returns `0`
- `setMIS_MAX_STA(...)` calls `setMaxAssoc(input+0x00)` only when
  `state+0x26c` is nonzero, ignores the helper result, and returns `0`

The local scaffold now records these body contracts as constants, layouts, APSTA
state fields, and static asserts. The host-owned APSTA owner now executes the
simple state/fixed-output getters `getSSID`, `getSTATE`, `getOP_MODE`, and
`getPEER_CACHE_MAXIMUM_SIZE` when an APSTA owner exists; no-owner traffic still
uses the primary STA bootstrap/cache path. The command-backed APSTA getters
remain separate station/channel/key layers.

## APSTA Station And Key Body Layer

The station/key public APSTA methods use command buffers and station-table state.
They are not interchangeable with primary STA key or association helpers.

Recovered `getSTATION_LIST()` sequence:

- NULL input returns raw `0x16`
- AP-down state `state+0x26c == 0` returns `0x39`
- allocate a `0x100` byte maclist and write dword `0x2a` at buffer `+0x00`
- use virtual IOCTL get selector `0x9f`
- async path installs completion owner/function/cookie and returns
  `0xe00002d8` on submit failure
- sync path uses RX payload range `0x0000010001000100`
- sync success calls `convertBCMAssocListToAppleAssocList` and returns `0`

Recovered `setCIPHER_KEY()` sequence:

- AP-down state returns `6`
- no null guard is present after AP-up passes
- cipher type is read from input `+0x08`
- cipher `0` returns success without programming
- only cipher types `3` and `5` enter key programming
- unsupported nonzero ciphers log and return success
- `wl_wsec_key` carrier size is `0xa4`
- virtual IOCTL set selector is `0x2d`

Recovered `getSTA_IE_LIST()` sequence:

- NULL returns raw `0x16`
- scan station table `state+0xb9..state+0x1a9` with stride `0x30`
- compare 6-byte MAC from input `+0x04`
- not found returns `2`
- found path uses IOVAR `wpaie`
- success writes output length from output `+0x11 + 2` into output `+0x0c`

Recovered `getSTA_STATS()` sequence:

- AP-down state returns `0x39`
- NULL returns raw `0x16`
- allocation size comes from core-private `+0x30c` with thresholds `7` and
  `0x0f`
- TX payload is 6 bytes from input `+0x04`
- IOVAR name is `sta_info`
- success writes output `+0x00 = 1` and maps RX fields
  `+0x58/+0x68/+0x54/+0x60` to output `+0x0c/+0x10/+0x14/+0x18`

Recovered `getKEY_RSC()` sequence:

- no null guard is present
- read key index from input `+0x0e`
- use virtual IOCTL get selector `0xb7`
- TX payload size is `8`
- RX range is `0x0000000800040008`
- success writes length `8` at output `+0x50` and the RSC qword at output
  `+0x54`

2026-07-07 runtime boundary update:

- `setCIPHER_KEY()` no longer carries a local NULL guard after the AP-up gate.
- `getSTA_IE_LIST()` now performs the recovered station-table lookup, copies
  the station-entry prefix into output `+0x10`, and crosses the AP/GO HAL query
  boundary for the backend-owned `wpaie` tail before applying the recovered
  output-length rule.
- `getSTA_STATS()` now crosses the AP/GO HAL query boundary for the
  backend-owned `sta_info` tail and publishes the valid bit plus recovered
  output fields only after backend success.
- `getKEY_RSC()` now crosses the AP/GO HAL query boundary for the backend-owned
  RSC query and publishes length `8` only after backend success.

The local boundary still does not fabricate station IE/stat/RSC data and does
not alter primary STA key paths. Missing AP backend support remains
fail-closed through default `ItlHalService` unsupported returns.

## APSTA Event And Station Table Producer Layer

The next APSTA layer is the producer side for the station table consumed by the
station/key public methods. `handleEvent` and its helper cluster do not use a
MAC-relative anonymous table. Reference APSTA owns five full station entries at
`state+0xb8`, each with stride `0x30`:

- entry `+0x00`: active byte
- entry `+0x01`: six-byte MAC
- entry `+0x10`: sleep state, initialized to `2`
- entry `+0x20`: AIHS flag
- entry `+0x24`: sharing flag
- entry `+0x28`: Apple-station flag

Recovered event producer contracts:

- `handleEvent(...)` reads type/status/reason/auth/data-length/address/data at
  event `+0x04/+0x08/+0x0c/+0x10/+0x14/+0x18/+0x30`.
- association/reassociation events `8/10` with status/reason `0/0` update
  `state+0x80/+0x84`, call `updateSTAAssocInfo`, parse RSNXE, and post STA
  message id `0x0c` with payload size `0x114`.
- removal events `5/6/11/12` notify each APSTA TX subqueue through vtable
  `+0x358`, clear the station entry, and post STA message id `0x0d` with
  payload size `0x0c`.
- `postMessageForSTA(...)` dispatches through APSTA vtable `+0xb18` and then
  notifies core owner `state+0x218 -> +0x128 -> +0x2c20` with flag `1`.
- `removeStaFromStaTable(index)` rejects indexes `>= 5` with `0xe00002bc` and
  clears six qwords from the entry.

The local scaffold now types `state+0xb8` as the five-entry APSTA station table
and records STA event-message, Apple IE, RSNXE, action-frame, station-list
mismatch, and removal constants as compiled witnesses.

2026-07-07 runtime boundary update:

- the net80211 AP association path preserves the association IE TLV list for
  the APSTA producer
- the APSTA owner recognizes the recovered Apple OUI byte triples
  `00:17:f2`, `00:03:93`, and `00:a0:40`
- association/reassociation updates `state+0x80/+0x84`, derives Apple
  station flags from IE TLVs, copies RSNXE element `0xf4`, and posts the packed
  `0x114` `STA_ARRIVE` carrier
- removal clears/recounts the station entry and posts the packed `0x0c`
  `STA_LEAVE` carrier

This still does not enable AP firmware mode or remove the scoped APSTA
station-event opt-out gate. The auth-ind `0x98` body remains unimplemented
until its payload layout is recovered.

## APSTA Power/Offload/Datapath Tail

The APSTA tail after station-table handling contains several command and
lifecycle contracts that must stay separate from the primary STA path.

Recovered command/offload contracts:

- `configureMPDUSize(uint32_t)` sends `ampdu_mpdu` with payload size `4` only
  when core-private `+0x3fc == 2` and `+0x30c <= 4`.
- Low-power exit sends `lphs_mode` value `0`; beacon wait-period success sends
  `lphs_mode` value `1`; both use 4-byte payloads and no RX expected.
- ARP offload success sends `arp_hostip_clear`, then host-IP clear success
  reads `state+0xac` and sends `arp_hostip` with a 4-byte payload.
- `setBeaconDutyCycle` sends `rpsnoa` payload size `0x10` with qword header
  `0x100100101`, mode word `2`, and enable word at `+0x0e`.
- `configureBeaconDutyCycleParams` sends `rpsnoa` payload size `0x18` with
  qword header `0x300180101`, mode word `2`, dynamic byte `0x0a -
  dynamicPSParams[level].byte8`, and rotated params qword at `+0x10`.

Recovered power/datapath contracts:

- `releaseSoftAPPowerAssertion()` clears `state+0x0c` and notifies event
  `0x8d` through `state+0x218 -> +0x128 -> +0x2c20`, payload value `0`, size
  `4`, flag `1`.
- `softApStatsAccumulatePowerStateDuration(...)` adds duration to
  `state+0x1d0 + power_state * 0x10` and updates timestamp `state+0x1a8`.
- `enable(uint32_t)` checks vtable `+0xd58`, calls superclass slot `+0x860`
  when running, and returns `0xe00002d5` when not running.
- `disable(uint32_t)` calls vtable `+0xda0` and superclass slot `+0x868`.
- `enableDatapath()` checks vtable `+0xcf0`; if the interface is not enabled,
  reference returns `0xe00002bc`. This corrects the older local YAML text that
  described that branch as success.
- APSTA accessors return exact state fields for logger, queues, pools,
  multicast source, and AC-to-TX-subqueue mapping.

Recovered MAC/peer-stats contracts:

- `setMacAddress(...)` sends `cur_etheraddr` with 6-byte payload only when the
  interface id is not `-1` and AP-up state `state+0x26c` is zero.
- `configureSoftAPPeerStats(bool)` requires feature gate `0x7a`, sends
  `softap_stats` payload size `0x0e`, and successful callback writes
  `state+0x328 = cookie & 1`.

The local scaffold records these constants and layouts only. It does not execute
APSTA IOVARs, does not enable APSTA role-7 ownership, and does not alter the
primary STA connect/data path.

## APSTA Monitor, Power-State, And Stats Layer

The next recovered APSTA segment is timer-driven and remains owned by the APSTA
object. It must not be mapped onto primary STA state.

Recovered stats timer contracts:

- `handleAPStatsUpdates` requires timer `state+0x70`, allocates `0x808`, calls
  APSTA vtable `+0xfd8`, treats `0xe00002d8` as async-submit failure, and
  calls `checkForStationListMismatch` on successful data.
- Activity baseline is `state+0x88`; inactivity age is `state+0x20c`.
- The optional firmware activity source reads core-private `+0x4d59`, private
  enable `+0x757e`, and four qwords from `+0x2a78`.
- Inactivity posts STA message id `0x0d` with payload size `0x0c` at threshold
  `0x16e361`, then resets age at `0x170a71`.
- The stats timer reschedules through vtable `+0x1d0` at interval `0x1388`.

Recovered monitor/power contracts:

- `monitorAPInterface` requires timer `state+0x78`, mirrors core-private
  `+0x4d59` bit 0 to `state+0x208`, uses `state+0x62` as the Apple vendor IE
  refresh flag, and tracks low traffic at `state+0x64`.
- RX baselines are `state+0x90` and `state+0x98`; accumulated deltas are
  `state+0x1b8` and `state+0x1c0`; `updateRxCounter` targets `state+0xa0`.
- `setPowerSaveState` is gated by `state+0x0e`, ignores reason `7`, stores
  current state at `state+0x10`, records transition counts at
  `state+0x1c8 + state * 0x10`, and duration buckets at `state+0x1d0`.
- State `2` programs `modesw_bcns_wait` payload `0x0a` before `lphs_mode`
  payload `1`; MFP uses feature gate `0x26` and IOVAR `mfp`.

Recovered assoc-list and print contracts:

- BCM assoc-list count is at `+0x00`, MACs begin at `+0x04`, stride `6`.
- Apple assoc-list output size is `0x808`, version `1`, count `+0x04`,
  entries `+0x08`, entry stride `0x10`, max `0x80`, clamp threshold `0x81`.
- `printDataPath` uses userPrintCtx offsets `+0x18/+0x20/+0x24/+0x28` and
  vtable slots `+0x338/+0x320/+0x328/+0xc68`.

The local scaffold records these monitor/power/stats contracts as constants,
layouts, and static asserts only. It still does not execute APSTA timers,
does not enable APSTA role-7 ownership, and does not alter primary STA paths.

## APSTA Async Callback Telemetry Layer

The adjacent callback tail covers HostAP filter cleanup and beacon/DTIM command
telemetry. It is still APSTA-owned and must not be collapsed into primary STA
diagnostics.

Recovered filter-delete contract:

- `setHostApModeInternal` sends `pkt_filter_delete` before the ARP offload
  setup path.
- The payload is a single dword `0x6c`, size `4`.
- The command source is `state+0x228`, no RX is expected, completion cookie is
  `0`, and the callback is `deleteIPv4PktFiltersAsyncCallBack`.
- Nonzero callback status logs at level `2`, line `0x0ea0`, using error decode
  through `state+0x218` vtable `+0x780`.

Recovered beacon/DTIM callback contract:

- `setBeaconInterval` uses IOCTL `0x4c`, 4-byte payload, skip/apply target
  `state+0x68`, callback `handleSetBcnIntervalAsyncCallBack`, and sync error
  line `0x106b`.
- DTIM setup uses IOCTL `0x4e`, 4-byte payload, source `state+0x16`, apply
  target `state+0x6a`, callback `handleSetBcnDTIMPeriodAsyncCallBack`, and sync
  error line `0x1091`.
- Both callbacks return immediately on status `0`.
- Nonzero callback status logs at level `1`, then emits the RX payload through
  `state+0x210` with data offset `+0x00`, length offset `+0x08`, telemetry flag
  `1`.
- Labels are `BCNPRD IOCTL rxPayload bytestream: ` and
  `DTIMPRD IOCTL rxPayload bytestream: `.

The local scaffold now records these strings and constants as compiled
witnesses. It does not execute the APSTA callbacks, does not enable HostAP, and
does not alter primary STA behavior.

## APSTA Action-Frame And LPHS Layer

The next recovered APSTA segment is the action-frame branch in `handleEvent`
and the adjacent LPHS all-station low-power checker. This remains APSTA-owned
and must not be folded into primary STA association or datapath logic.

Recovered action-frame parse contracts:

- `handleEvent` dispatches action frames from event type `0x4b`.
- The payload base is `event+0x30`; minimum accepted payload length is `0x12`.
- Raw version `0x0100` reads category/action at payload `+0x10/+0x11`, absolute
  event offsets `+0x40/+0x41`.
- Raw version `0x0200` requires payload length `0x1a` and reads category/action
  at payload `+0x18/+0x19`, absolute event offsets `+0x48/+0x49`.
- The byte-swapped version reject threshold is `3`; unknown category/action
  sentinel is `0xaa`.

Recovered LPHS state contracts:

- LPHS category is `0x7f`; accepted actions are `1` and `2`.
- Apple writes the action value directly to station-table sleep-state
  `state+0xb8 + index*0x30 + 0x10`.
- New station entries initialize sleep-state to `2`.
- Active entries with sleep-state `2` block `checkIfAllStaAreInLPM`.
- Therefore action `1` is low-power/sleep, and action `2` is awake/default.
- If no active station remains in blocking state `2` and SoftAP concurrency is
  disabled, Apple calls `setPowerSaveState(3, 0x0b)`.

The local scaffold now records these constants and aliases as compiled
witnesses. It does not synthesize LPHS frames, does not force APSTA power-save
state, does not enable HostAP, and does not alter primary STA behavior.

## WCL Action-Frame Send Layer

The outbound WCL action-frame path has a separate Apple net-adapter contract
from the APSTA LPHS receive path. It must preserve V1/V2 transport shape even
while the local Tahoe commander is still an internal compatibility layer.

Recovered sender contracts:

- `AppleBCMWLANCore::setWCL_ACTION_FRAME` rejects `NULL` with `0xe00002bc`.
- The carrier fields are category `+0x00`, channel `+0x04`, peer address
  `+0x08`, frame length `+0x0e`, and frame bytes `+0x10`.
- V2 is selected when core-private firmware generation `+0x30c > 0x14`.
- V1 `sendActionFrame` accepts total action-frame bytes up to `0x707`, zeroes a
  `0x718` buffer, and sends a fixed IOVAR CommandTxPayload length `0x724`.
- V2 `sendActionFrameV2` rejects total bytes `>= 0x708`, allocates
  `total + 0x34`, and uses issue-command dispatch.

Local corrections in this batch:

- Action-frame capacity is now a named `0x708` contract with maximum payload
  `0x707`.
- V1 dispatch now reports the fixed `0x724` request length instead of the
  caller frame length.
- The local last-action-frame cache now has the full `0x708` capacity instead
  of truncating at `0x200`.

This still does not inject real Broadcom action frames. It aligns the local WCL
sender contract and telemetry shape with Apple before deeper backend work.

## WCL Action-Frame Progress And Scan Gate Layer

The adjacent Apple layer tracks outbound action-frame completion progress in
core-private state and prevents scans from starting while a non-overdue action
frame is still marked in progress.

Recovered progress contracts:

- `setActionFrameProgress(bool)` writes the progress byte at core-private
  `+0x4478`.
- `getActionFrameProgress()` calls `checkActionFrameCompleteOverdue()` before
  returning bit 0 from `+0x4478`.
- `checkActionFrameCompleteOverdue()` reads start milliseconds from
  core-private `+0x4480` and clears `+0x4478` after unsigned elapsed time
  reaches `0x12d` ms.
- The overdue path logs line `0x3b1d` and reports status `0xe3ff852b` through
  line `0x3b1e`.
- `AppleBCMWLANScanAdapter::startScan(...)` runs the overdue check and rejects
  scan with `0xe00002d5` / line `0x00a5` while the progress bit remains set.

Local corrections in this batch:

- Tahoe action-frame constants now include the progress flag offset `0x4478`,
  start-ms offset `0x4480`, overdue threshold `0x12d`, overdue status
  `0xe3ff852b`, and scan reject status `0xe00002d5`.
- The Tahoe owner registry now has action-frame `progress` and
  `progressStartMs` witnesses plus pure helper methods for set/get/overdue
  semantics.

This batch intentionally does not connect the recovered scan gate to runtime
scan handling yet. The timestamp producer and completion-clear lifecycle must
be recovered first; otherwise scan rejection could become a guessed blocking
state instead of Apple-equivalent behavior.

## Controller Queue, Capacity, Promiscuous, And Multicast Layer

The next recovered controller-facing layer supplies queue sizing, Skywalk data
depth, action-frame capacity, promiscuous state, and multicast owner contracts.
These are framework-facing methods, so returning success without writing outputs
or inheriting the wrong base default can change IO80211/Skywalk behavior before
association and data path tests reach firmware.

Recovered queue/depth contracts:

- `requestQueueSizeAndTimeout` reads `wlan.coalesce.qsize` and
  `wlan.coalesce.timeout` from the `IOService` DT path.
- It returns `0xe00002c7` unless both low 16-bit values are nonzero.
- On success it writes queue and timeout outputs before returning `0`.
- `fetchAndUpdateRingParameters` initializes core-private `+0x1154` to
  `0x200`; `getDataQueueDepth(OSObject*)` returns that field.
- `IO80211SkywalkInterface::getDataQueueDepth()` calls the bound controller
  vtable slot. IO80211 base would otherwise return `0x400`.
- `IO80211Controller::getActionFramePoolCapacity()` returns `0x100`.

Recovered promiscuous/multicast contracts:

- `setPromiscuousMode(bool)` stores the requested byte at core-private
  `+0x4778` and returns success.
- `setMulticastMode(bool)` and `setMulticastList(...)` share reject gate
  core-private `+0x2891` bit `0x80`, returning `0xe0823804` when set.
- `setMulticastList` rejects caller count above `0x20` with `0xe00002bc`.
- Apple caches multicast count at `+0x234` and 6-byte entries at `+0x238`.
- The firmware IOVAR payload uses a `0xca` byte buffer filled with `0xaa`,
  payload length `4 + count * 6`, and IOVAR name `mcast_list`.

Local corrections in this batch:

- Tahoe controller constants now record queue, capacity, depth, and multicast
  offsets/statuses/payload shape.
- `requestQueueSizeAndTimeout` no longer returns success without writing both
  outputs.
- `getDataQueueDepth` returns the AppleBCMWLANCore default `0x200` owner value.
- `getActionFramePoolCapacity` explicitly returns `0x100`.
- Promiscuous/multicast requested state and caller multicast list are cached in
  Tahoe owner state; counts above `0x20` are rejected with `0xe00002bc`.

The batch intentionally does not issue the Apple `mcast_list` Broadcom IOVAR.
The local active backend remains the Intel HAL multicast path until the
Broadcom commander and virtual-interface multicast owner lifecycle are fully
recovered.

## Hidden Interface Flow, Timestamp, Log-Pipe, And Virtual-Interface Layer

The hidden object at AppleBCMWLANCore private offset `+0x1510` is broader than
the registry/property paths already lifted earlier. The adjacent decompile pass
shows it also owns flow-queue delegation, packet timestamping gates, log-pipe
selection, and parts of virtual-interface lifecycle.

Recovered flow queue contracts:

- `flowIdSupported()` delegates to hidden slot `+0xa68`.
- `requestFlowQueue(...)` tests `+0xa68`, falls back to base slot `+0xd60`
  when unsupported, returns `NULL` while commands are rejected, and otherwise
  calls hidden slot `+0xa70`.
- The hidden flow request receives the original metadata pointer, `metadata+6`,
  dword `metadata+0x0c`, and byte `metadata+0x10`.
- `releaseFlowQueue(...)` delegates to hidden slot `+0xa78` when supported, or
  base slot `+0xd68` when unsupported.

Recovered timestamp/log/virtual-interface contracts:

- Packet timestamp enable uses base slot `+0xd90`, then a command-gate action
  whose gated body delegates to hidden slot `+0xaa8`.
- Packet timestamp disable uses base slot `+0xd98`, then a command-gate action
  whose gated body delegates to hidden slot `+0xab0`.
- `getLogPipes(...)` reads hidden object `+0x88`, then event/log/snapshot pipe
  pointers at `+0x218/+0x220/+0x230`.
- Virtual-interface create/enable/disable delegate through base slots
  `+0xe10/+0xd40/+0xd48`; null enable/disable status is `0xe00002bc`.
- Role `6` paths are special and involve proximity owner `+0x2c28` plus wake
  flag `0x10000`.

Local corrections in this batch:

- Added `TahoeHiddenInterfaceContracts.hpp` with exact offsets/slots/statuses.
- Added hidden-interface owner witnesses to `TahoeOwnerRegistry`.
- `flowIdSupported()` now reads the owner witness, defaulting to false.
- `releaseFlowQueue()` no longer logs on the local no-op path; it records only a
  private owner witness while flow IDs remain disabled.

This batch intentionally does not synthesize flow queue objects, hidden
timestamp owners, or proximity virtual-interface runtime behavior. Those need a
separate recovered local owner backend before the slots can be safely enabled.

## QoS, DynSAR, Congestion-Control, And Feature Flag Layer

The next recovered Q11-C1 helper slice is a compact set of core-private offsets
used by DynSAR, congestion control, AWDL AMPDU policy, feature flags, split-TX,
and TX address resolution counters.

Recovered contracts:

- DynSAR fail-safe start ticks are stored at core-private `+0x74e0`.
- `wasDynSARInFailSafeMode()` computes
  `((nowTicks - startTicks) >> 0x0a) < 0x9502f9`.
- Congestion-control helpers test core-private `+0x7584` bit `0`; set means
  success, clear means `0xe00002c7`.
- AWDL AMPDU force and force-disable flags live at `+0x3768` and `+0x3764`.
- Hardware feature flags live at `+0x458c`.
- Split-TX status returns bit `0` from `+0x00dc`.
- TX address resolution counters live at `+0x2aa4` and `+0x2aa8`.
- The neighboring public status carriers use the same core/private owner
  family: slow-wifi enabled at `+0x7569`, low-latency owner state at `+0x2c28`,
  and tx-blanking bit at `+0x4ce8`.

Local corrections in this batch:

- Added `TahoeQosDynsarContracts.hpp`.
- Added `QosDynsarOwner` witnesses to `TahoeOwnerRegistry`.
- Added pure helper semantics for DynSAR fail-safe window and congestion
  feature gate.
- Routed `getSLOW_WIFI_FEATURE_ENABLED`, `getWCL_LOW_LATENCY_INFO`,
  and `getWCL_GET_TX_BLANKING_STATUS` through the QosDynsar owner state instead
  of separate interface-local caches.

This batch does not execute QoS IOVARs, does not enable DynSAR policy, and does
not force congestion/AMPDU/split-TX/address-resolution state.
## Hidden Association RSN Carrier Contract

The active primary-STA chain still reaches the WCL join/RX-EAPOL boundary
without final RSN/key/data completion. The next restored owner surface is the
hidden association carrier itself:

- hidden selectors: `0x45/0x46`
- exact assoc-candidates payload length: `0x3ad8`
- auth fields: `+0x10/+0x14/+0x18`
- SSID: `+0x20` with length `+0x1c`
- RSN IE: pointer `+0xd6`, length `+0xd4`
- selected candidate: count `+0x218`, first BSSID `+0x220`
- candidate metadata: paired MAC `+0x226`, channel `+0x22c`, stride `0x12`

Apple `setAssocRSNIE(...)` uses pointer+length semantics. Local compatibility
state must therefore store only the bounded IE bytes and clear the previous
override tail, rather than copying a full 257-byte stack buffer. This restores
the carrier/RSN handoff only; it does not claim EAPOL TX/key/RSN completion.

## Skywalk Packet Pool Network Type

The active RX/TX packet-pool layer had one confirmed local divergence from the
Apple Skywalk contract. `AppleBCMWLANSkywalkPacketPool::initWithName(...)`
passes packet type `1` (`kIOSkywalkPacketTypeNetwork`) to the parent
`IOSkywalkPacketBufferPool` init path. The IO80211 downstream consumers at this
boundary are network-packet typed, including
`IO80211InfraInterface::inputPacket(IO80211NetworkPacket*, ...)` and
`IO80211PeerManager::skywalkInputPacket(IO80211NetworkPacket*, ...)`.

Local code previously created both pools with packet type `0`, creating generic
Skywalk packet pools. This matches the observed runtime gap: RX EAPOL enqueue
succeeds, but no `ITLWM_IO80211_INPUT` marker appears afterward.

Local correction:

- TX and RX pool creation now uses `kIOSkywalkPacketTypeNetwork`.
- No manual `inputPacket(...)` callback, forced EAPOL TX, forced key install,
  forced RSN state, retry, delay, or guessed `IO80211NetworkPacket` subclass is
  introduced.

## Skywalk Network Packet And RX Tag ABI

The next adjacent RX/input layer is the ABI shape required before the
producer-side RX completion handoff can be restored safely.

Recovered contracts:

- `IOSkywalkNetworkPacket` is an `IOSkywalkPacket` subclass, not an
  independent `IOService` subclass.
- `AppleBCMWLANPCIeSkywalkRxCompletionQueue::enqueuePackets(...)` calls the
  interface input slot with `(packet, packet_scratch, ether_header, false,
  false)` for normal infrastructure roles.
- The RX completion producer reads scratch/tag offset `+0x18`, maps it to a
  service class, and writes the mapped byte at `+0x29`.
- `IO80211InterfaceMonitor::logRxCompletionPacket(...)` reads tag `+0x18` as
  TID and rejects values above `7`; it also checks tag `+0x14`.
- Apple PCIe packet scratch is reached through packet `+0x78`, has size
  `0x98`, and packet prepare clears the first `0x30` bytes.

Local correction:

- The local `IOSkywalkNetworkPacket` declaration now matches the Tahoe Skywalk
  header shape.
- `packet_info_tag` now has a partial recovered layout for proven offsets
  `+0x14`, `+0x18`, and `+0x29`, plus size `0x98`.
- This does not yet add a manual input callback or synthesize RX delivery. It
  removes the deterministic ABI blocker that would make the next handoff fix
  unsafe.

## Skywalk RX Completion Input Handoff

The active runtime gap after RX enqueue is now tied to the RX completion action
itself. Apple's PCIe RX completion callback is not a passive acknowledgement:
it is the producer-side handoff into IO80211 input.

Recovered handoff:

- RX completion gets packet data virtual address and data offset from the
  packet object.
- It derives the ethernet header pointer as `dataVirtualAddress + dataOffset`.
- It passes packet scratch/tag to the interface input slot.
- It passes a null accepted pointer and final bool `false`.
- `IO80211InfraInterface::inputPacket(...)` then synchronously reaches monitor
  and peer-manager input processing.

Local correction:

- `skywalkRxAction(...)` now performs the handoff from the completion boundary.
- The earlier `skywalkRxInput(...)` remains the mbuf-copy/enqueue producer and
  does not call IO80211 input directly.
- The correction does not force downstream EAPOL TX, key installation, RSN
  completion, DHCP, or data success; those remain runtime-verification
  outcomes.

## Skywalk RX Completion Pending Producer

CR-164 runtime refined the RX completion interpretation. The local registered
RX action was still not invoked after `fRxQueue->enqueuePackets(...)`, proving
that base enqueue is not the producer-action boundary.

Recovered active contract:

- `IOSkywalkRxCompletionQueue::requestEnqueue(...)` calls the registered
  producer action.
- The action fills the Skywalk-provided packet array and returns produced
  count.
- AppleBCMWLAN's recovered action drains an owner-side pending RX list, calls
  the interface input slot, writes produced packet pointers, and returns count.
- Base `IOSkywalkRxCompletionQueue::enqueuePackets(...)` publishes packets to
  the networking path and does not invoke the producer action.

Local correction:

- `skywalkRxInput(...)` stages prepared RX packets in a fixed-capacity local
  pending producer queue.
- `skywalkRxInput(...)` rings `fRxQueue->requestEnqueue(nullptr, 0)`.
- `skywalkRxAction(...)` pops pending packets, performs the IO80211 input
  handoff, fills the provided array, and returns produced count.
- Pending prepared packets are drained before RX pool release.

This does not force IO80211 accepted success, EAPOL TX, key install, RSN, DHCP,
internet, link success, retries, replay, duplicate notify, or deauth masking.

## Skywalk TX Completion Producer

The adjacent TX ownership layer had a confirmed producer mismatch. Local
`skywalkTxAction(...)` consumed `IOSkywalkPacket` objects after copying their
payload into mbufs, but no completion producer returned those packet objects to
Skywalk. This is a packet ownership leak/stall once TX submission starts.

Recovered active contract:

- `AppleBCMWLANPCIeSkywalkTxCompletionQueue::stagePacket(...)` links completed
  packets into an owner-side list.
- `AppleBCMWLANPCIeSkywalkTxCompletionQueue::requestEnqueue(...)` enters the
  IOSkywalk completion producer boundary.
- `AppleBCMWLANPCIeSkywalkTxCompletionQueue::enqueuePackets(...)` drains staged
  packets into the provided packet array and returns produced count.
- IOSkywalk's TX completion enqueue path calls packet
  `completeWithQueue(queue, kIOSkywalkPacketDirectionTx, 0)` for produced
  packets.

Local correction:

- `skywalkTxAction(...)` stages each non-null consumed packet in a local TX
  completion pending ring.
- After the batch it rings `fTxCompQueue->requestEnqueue(nullptr, 0)`.
- `skywalkTxCompletionAction(...)` now produces staged packets into the
  provided array and returns produced count.
- Pending TX completion packets are drained before queue/pool release.

This does not force TX success, EAPOL/key/RSN/DHCP/link state, retries, replay,
polling, packet synthesis, or deauth masking. It only closes the ownership
contract for packets already consumed by the local TX submission action.

## Skywalk RX Producer Tag And Accounting Closure

The next active RX producer audit found two adjacent gaps after the pending
producer restoration.

Recovered reference behavior:

- The Apple RX completion producer passes packet scratch/tag into
  `inputPacket(...)`, maps TID `+0x18` to service class `+0x29`, and produces
  packets into the Skywalk array.
- After the produced batch, it calls
  `IO80211SkywalkInterface::recordInputPacket(int, int)` and then updates the
  RX counter through the interface virtual slot.

Local correction:

- The local RX pending producer now stages `packet_info_tag` metadata and length
  with each RX packet.
- `skywalkRxAction(...)` uses the staged tag and mirrors the post-batch
  `recordInputPacket` / `updateRxCounter` accounting.
- The local code explicitly does not write a synthetic scratch pointer at
  `packet+0x78`, because the generic Tahoe `IOSkywalkNetworkPacket` class is
  size `0x78`; that offset is Apple PCIe subclass storage, not generic packet
  storage.

This does not force accepted success, EAPOL TX, key install, RSN, DHCP, link
state, retries, replay, duplicate notification, or deauth masking.

## Skywalk TX Queue Space And Pending Visibility

The next TX-side audit found that the local interface exposed a live TX queue
through `getTxSubQueue(...)`, but reported zero queue space and zero pending
packets through `packetSpace(...)` and `pendingPackets(...)`.

Recovered reference behavior:

- `AppleBCMWLANSkywalkInterface::getTxSubQueue(...)` maps WME AC through an
  owner queue-index table and returns a TX queue object from the owner vector.
- `AppleBCMWLANIO80211APSTAInterface::getTxSubQueue(...)` uses the same shape
  in the APSTA owner layout.
- `getTxQueueDepth()` reads the first TX queue object's capacity.
- IO80211 `pendingPackets(...)` and `packetSpace(...)` map queue objects and
  call their pending/free-space virtuals.

Local correction:

- The local single-queue mapping now returns `fTxQueue` consistently for local
  TX queue queries.
- `pendingPackets(...)` returns `fTxQueue->getPacketCount()`.
- `packetSpace(...)` returns `fTxQueue->getFreeSpace()`.

This does not fabricate queue capacity or force TX/EAPOL/key/RSN/DHCP/link
success. Values come from the live IOSkywalk TX queue object.

## Skywalk TX Output Accounting Closure

The next TX submission audit found a missing post-dequeue accounting edge.
Apple's TX submission producer does not stop at consuming packets: it records
the accepted packet and byte totals through the IO80211 output-accounting
interface after the batch.

Recovered reference behavior:

- `AppleBCMWLANPCIeSkywalkTxSubmissionQueue::dequeuePackets(...)` accumulates
  packet and byte totals while draining TX packets.
- Near the tail it calls the interface slot matching
  `IO80211SkywalkInterface::recordOutputPacket(apple80211_wme_ac,int,int)`.
- IO80211's implementation delegates to the interface monitor when present and
  otherwise returns without changing link or key state.

Local correction:

- `skywalkTxAction(...)` now accumulates delivered packet and byte counts for
  frames accepted by the existing `outputPacket(...)` path.
- After the batch it calls `recordOutputPacket(...)` for the local single BE
  TX queue mapping.

This does not synthesize Apple packet scratch, call scratch-dependent TX log
methods, force EAPOL/key/RSN/DHCP/link/data success, retry, replay, delay,
poll, or mask deauth.

## IO80211 Network Packet Allocation-Class Closure

The next active EAPOL/RSN audit found that the RX producer now reaches
`IO80211InfraInterface::inputPacket(...)`, but the packet allocation class is
still not reference-equivalent.

Recovered reference behavior:

- `AppleBCMWLANPCIeSkywalkPacketPool::newPacketWithDescriptor(...)` allocates
  `AppleBCMWLANPCIeSkywalkPacket`, whose constructor chain enters the
  `IO80211NetworkPacket` base before installing the Apple packet vtable.
- `AppleBCMWLANPCIeSkywalkPacketPool::allocatePacket(...)` validates the
  parent allocation result against the Apple packet metaclass.
- `IO80211NetworkPacket::getPacketType(...)` parses the Ethernet frame and
  returns packet type `2` for EtherType `0x888e` EAPOL.

Local correction:

- The local Tahoe TX/RX packet pools are now instances of a local
  `IOSkywalkPacketBufferPool` subclass.
- Its `newPacket(...)` creates the system `IO80211NetworkPacket` object through
  `OSMetaClass::allocClassWithName("IO80211NetworkPacket")` and initializes it
  with the same pool descriptor.
- The existing queue topology, RX producer, RX tag carrier, and packet buffers
  are unchanged.

This does not synthesize Apple PCIe packet scratch, write `packet+0x78`, call
scratch-dependent log methods, force EAPOL/key/RSN/DHCP/link/data success,
retry, replay, delay, poll, synthesize packets, or mask deauth.

## IOSkywalk Network Packet Size ABI Closure

The next packet ABI audit found that the base class declaration still consumed
the storage slot that Apple reserves for its PCIe packet subclass.

Recovered reference behavior:

- Tahoe `IOSkywalkNetworkPacket` is registered with instance size `0x78`.
- `IO80211NetworkPacket` enters the same base constructor chain and is
  deallocated with size `0x78`.
- `AppleBCMWLANPCIeSkywalkPacket` is the first observed subclass layer at
  size `0x80`.
- Its scratch pointer is stored at packet offset `+0x78` and points to a
  `0x98` scratch/tag object.

Local correction:

- The tracked local `IOSkywalkNetworkPacket` declaration no longer declares
  `void *mReserved`.
- Compile-time assertions now require
  `sizeof(IOSkywalkNetworkPacket) == 0x78`.

This does not synthesize the Apple packet subclass or scratch pointer. It only
restores the base size required before a scratch-bearing subclass or exact
scratch-dependent datapath methods can be added safely.

## IO80211 Network Packet Virtual Surface Closure

The next packet ABI audit found that the local `IO80211NetworkPacket`
declaration was still too thin for the next Apple packet subclass step.

Recovered reference behavior:

- Tahoe exports `IO80211NetworkPacket` constructors/destructors, metaclass
  state, packet-type parsing, PTM/timestamp/status helpers, `getBufferSize`,
  and two `prepareWithQueue(...)` overloads.
- `getPacketType()` returns EAPOL packet type `2` for EtherType `0x888e`.
- `AppleBCMWLANPCIeSkywalkPacket` constructors call the
  `IO80211NetworkPacket` constructor chain before installing the Apple packet
  vtable.

Local correction:

- The tracked `IO80211NetworkPacket` header now declares the exported method
  surface and opaque `IO80211NetworkTXStatus` ABI type.
- A static assertion keeps `sizeof(IO80211NetworkPacket) == 0x78`.

This does not instantiate a local IO80211 packet object or alter the CR-170
packet pool behavior. It restores the local C++ ABI surface required before
the Apple packet subclass/scratch layer can be implemented without guessing.

## Packet Scratch Field Map Closure

The next packet scratch audit recovered the later Apple scratch fields used by
`AppleBCMWLANPCIeSkywalkPacket` methods.

Recovered reference behavior:

- Apple packet scratch is a `0x98` object referenced from packet offset
  `+0x78`.
- Scratch `+0x48` and `+0x50` are bus/virtual address fields cleared by
  packet completion.
- Scratch `+0x74` stores packet signature `0xdeadbeef`.
- Scratch `+0x80`, `+0x8a`, and `+0x90` store TX status, flow queue index,
  and packed AC/duplicate flags.

Local correction:

- `packet_info_tag` now names the recovered fields and asserts their offsets.
- The total size remains `0x98`.

Rejected path:

- A direct local C++ subclass of `IO80211NetworkPacket` was attempted and
  rejected before CR submission because BootKC verification showed unresolved
  non-exported `IOSkywalkPacket::*` virtual symbols in the generated subclass
  vtable.

This batch therefore does not change packet ownership or allocation. It only
records the field map needed for the next safe construction strategy.


## CR-174 closure

After CR-173 implemented the field map for `+0x48`..`+0x90`, cross-decompile
audit on 2026-04-28 found four additional named-field divergences inside the
`+0x19`..`+0x28` band that CR-173 left as `reserved19[0x10]`:

- `tx_vlan_tag` at `+0x1c` (uint32_t) — written by
  `IO80211InfraInterface::logTxPacket(...)` at `0xffffff80022e3896`.
- `rx_vlan_tag` at `+0x22` (uint16_t) — written by
  `IO80211InfraInterface::inputPacket(...)` at `0xffffff80022e3f20` and by
  `AppleBCMWLANLowLatencyInterface::inputPacket(...)` override; read by
  `IO80211PeerManager::skywalkInputPacket(...)` at `0xffffff80021dd7b4`.
- `rx_drop_marker` at `+0x24` (uint32_t) — cleared by
  `IO80211PeerManager::inputPacket(...)` at `0xffffff80021d1424` and on the
  RxDrop branch of `IO80211PeerManager::skywalkInputPacket(...)`.
- `ac_meta` at `+0x28` (uint8_t) — cleared and conditionally written by
  `IO80211InfraInterface::logTxPacket(...)`.

Cross-decompile evidence also confirms that the IO80211 input chain takes
`packet_info_tag *` as an explicit pointer parameter on every layer:

- `IO80211InfraInterface::inputPacket` (param 3).
- `IO80211PeerManager::inputPacket` (param 3).
- `IO80211PeerManager::skywalkInputPacket` (param 4).
- `AppleBCMWLANLowLatencyInterface::inputPacket` overrides base, enriches
  `tag+0x22` (RX VLAN), forwards to base.

Local `AirportItlwmSkywalkInterface::inputPacket` already passes the explicit
`packet_info_tag *tag` to `IO80211InfraInterface::inputPacket`. The handoff
contract is satisfied without packet-owned scratch — the rejected direct C++
subclass path is no longer required.

CR-174 implements the remaining named-field divergences. Generated binary is
bit-identical to CR-173 (sha256
`c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`,
UUID `BA3D771F-F079-33FF-94E5-C792E66237D8`). CR-174 supersedes CR-173.

## CR-175 closure

After CR-174 sealed the packet-scratch field map, the next adjacent layer is
the `IO80211InfraInterface` public surface. BootKC inspection of
`IO80211Family.kc` on 2026-04-28 confirmed four direct-call (non-vtable)
exported helpers that the local header did not declare:

- `IO80211Peer *IO80211InfraInterface::getInfraPeer()` at
  `0xffffff80022e1148`.
- `ether_addr *IO80211InfraInterface::getCurrentApAddress()` at
  `0xffffff80022e5ef8`.
- `void IO80211InfraInterface::handleKeyDone(bool, bool)` at
  `0xffffff80022e6f9c`.
- `void IO80211InfraInterface::bssidChange(void *, unsigned long)` at
  `0xffffff80022e116e`.

CR-175 adds non-virtual declarations for all four under
`#if __IO80211_TARGET >= __MAC_26_0` in
`include/Airport/IO80211InfraInterface.h`. No caller wiring is added; the
declarations exist so future caller patches can use the documented C++
surface without per-call mangled-symbol shims.

Generated binary is bit-identical to CR-174 (sha256
`c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`, UUID
`BA3D771F-F079-33FF-94E5-C792E66237D8`). CR-175 supersedes CR-174.

## CR-176 closure

After CR-175 sealed the InfraInterface public surface, the next adjacent
layer is the `IO80211PeerManager` public surface — needed by any caller
wiring CR that registers a station peer or queries the peer list. BootKC
inspection of `IO80211Family.kc` on 2026-04-28 confirmed seven direct-call
(non-vtable) exported peer-management helpers that the local include
directory previously did not declare:

- `IO80211PeerManager::addPeer(unsigned char *)` at `0xffffff80021d3f58`.
- `IO80211PeerManager::addPeerOperation()` at `0xffffff80021d7ba0`.
- `IO80211PeerManager::removePeer(IO80211Peer *)` at `0xffffff80021d4452`.
- `IO80211PeerManager::removePeer(unsigned char *)` at `0xffffff80021d4806`.
- `IO80211PeerManager::removePeerOperation()` at `0xffffff80021d7c7e`.
- `IO80211PeerManager::getPeerList()` at `0xffffff80021df2fe`.
- `IO80211PeerManager::getPeerStats(apple80211_peer_stats *)` at
  `0xffffff80021d298e`.

CR-176 adds new `include/Airport/IO80211PeerManager.h` declaring all seven
as non-virtual member functions on an opaque (no vtable / no data layout)
`class IO80211PeerManager`. No caller wiring is added; the declarations
exist so future caller patches can use the documented C++ surface without
per-callsite mangled-symbol shims.

Generated binary is bit-identical to CR-175 (sha256
`c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`, UUID
`BA3D771F-F079-33FF-94E5-C792E66237D8`). CR-176 supersedes CR-175.

## CR-177 closure

After CR-176 sealed the PeerManager public surface, the next adjacent layer
is the `IO80211Peer` public surface — the peer object itself. BootKC
inspection confirmed six direct-call exported helpers selected for CR-177
(out of 230 total IO80211Peer symbols), restricted to those whose C++
signatures contain no kernel-internal enum or class types whose definitions
are not yet recovered:

- `IO80211Peer::withAddressAndManager(unsigned char const *,
   IO80211PeerManager *)` at `0xffffff80021bf64a` — factory.
- `IO80211Peer::init()` at `0xffffff80021bf6c0`.
- `IO80211Peer::getMacAddress()` at `0xffffff80021bff7a`.
- `IO80211Peer::setMacAddress(ether_addr *)` at `0xffffff80021c5df4`.
- `IO80211Peer::getManager()` at `0xffffff80021c3558`.
- `IO80211Peer::getGeneration()` at `0xffffff80021c60dc`.

CR-177 adds new `include/Airport/IO80211Peer.h` declaring all six on an
opaque (no vtable / no data layout) `class IO80211Peer`, with
`withAddressAndManager` as a `static` factory. No caller wiring is added.

Generated binary is bit-identical to CR-176 (sha256
`c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`, UUID
`BA3D771F-F079-33FF-94E5-C792E66237D8`). CR-177 supersedes CR-176.

## CR-178 closure

After CR-177 sealed the IO80211Peer public surface, the next adjacent
layer extends the IO80211PeerManager surface from CR-176 with the
data-path control + peer-lookup cluster. BootKC inspection confirmed
eleven additional direct-call exported helpers selected for CR-178
(out of 220+ total IO80211PeerManager symbols), restricted to
signatures that contain no kernel-internal enum or class types whose
definitions are not yet recovered:

- `IO80211PeerManager::findPeer(unsigned char *)` at `0xffffff80021d1388`.
- `IO80211PeerManager::findCachedPeer(unsigned char *)` at `0xffffff80021d3f0c`.
- `IO80211PeerManager::getUnicastPeer()` at `0xffffff80021df2a8`.
- `IO80211PeerManager::getMulticastPeer()` at `0xffffff80021df296`.
- `IO80211PeerManager::getEnabled()` at `0xffffff80021df672`.
- `IO80211PeerManager::setEnableState(bool)` at `0xffffff80021cc798`.
- `IO80211PeerManager::getDataPathOpen()` at `0xffffff80021df4f8`.
- `IO80211PeerManager::setDataPathOpen(bool)` at `0xffffff80021df50a`.
- `IO80211PeerManager::setDataPathState(bool)` at `0xffffff80021cde60`.
- `IO80211PeerManager::lockDataPath()` at `0xffffff80021cded6`.
- `IO80211PeerManager::unlockDataPath()` at `0xffffff80021cdfca`.

CR-178 extends the existing `include/Airport/IO80211PeerManager.h`
with all eleven declarations on the same opaque `class
IO80211PeerManager` (no vtable / no data layout) introduced in
CR-176. No caller wiring is added.

Generated binary is bit-identical to CR-177 (sha256
`c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`, UUID
`BA3D771F-F079-33FF-94E5-C792E66237D8`). CR-178 supersedes CR-177.

## CR-179 closure

After CR-178 added the data-path + peer-lookup cluster, the next layer
extends the IO80211PeerManager surface with thirteen infra-config
helpers covering BSSID, SSID, channel, TX-state, and RSSI. BootKC
inspection confirmed all thirteen direct-call exported helpers selected
for CR-179, restricted to signatures that contain only already-known
types (`apple80211_channel`, `ether_addr`, `unsigned int`, `int`,
`bool`, `unsigned char *`):

- `IO80211PeerManager::getInfraBssid()` at `0xffffff80021df07a`.
- `IO80211PeerManager::getInfraSsidLen()` at `0xffffff80021df2de`.
- `IO80211PeerManager::setInfraSsidLen(unsigned int)` at `0xffffff80021df2ee`.
- `IO80211PeerManager::getInfraSsidBytes()` at `0xffffff80021df08a`.
- `IO80211PeerManager::setInfraSsidBytes(unsigned char *, unsigned int)` at `0xffffff80021df09a`.
- `IO80211PeerManager::setInfraTxState(bool)` at `0xffffff80021d4e36`.
- `IO80211PeerManager::setInfraChannel(apple80211_channel *)` at `0xffffff80021d4eb0`.
- `IO80211PeerManager::copyInfraChannel(apple80211_channel *)` at `0xffffff80021d4e72`.
- `IO80211PeerManager::resetInfraChannel()` at `0xffffff80021d4e90`.
- `IO80211PeerManager::setInfraChannelInfo(apple80211_channel *)` at `0xffffff80021df04c`.
- `IO80211PeerManager::setInfraChannelFlags(unsigned int)` at `0xffffff80021df06a`.
- `IO80211PeerManager::getInfraRSSI()` at `0xffffff80021df994`.
- `IO80211PeerManager::setInfraRSSI(int)` at `0xffffff80021df984`.

CR-179 extends `include/Airport/IO80211PeerManager.h` with all thirteen
declarations on the same opaque `class IO80211PeerManager` (no vtable /
no data layout) introduced in CR-176 and extended in CR-178. No caller
wiring is added.

Generated binary is bit-identical to CR-178 (sha256
`c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`, UUID
`BA3D771F-F079-33FF-94E5-C792E66237D8`). CR-179 supersedes CR-178.

## CR-180 closure

After CR-179 sealed the IO80211PeerManager infra-config helper surface,
the next adjacent layer is the `IO80211InterfaceMonitor` public surface
— the IORegistry-visible counter / RSSI / link-rate object. BootKC
inspection confirmed nineteen direct-call exported helpers selected for
CR-180 (out of 60+ total IO80211InterfaceMonitor symbols), restricted to
those whose C++ signatures contain only already-known types
(`IO80211Controller`, scalar integers, `bool`):

- controller back-pointer: `getController()`.
- counter accessors: `getInputBytes()`, `getInputPackets()`, the four
  output bytes accessors, and the four output packets accessors.
- RSSI/SNR/NF: `hasInterfaceRSSI()`, `getInterfaceRSSI()`,
  `setInterfaceRSSI(long long)`, `setInterfaceSNR(long long)`,
  `setInterfaceNF(long long)`.
- link-rate / channel: `getLinkRate()`,
  `setLinkRate(unsigned long long)`, `modifyChID(unsigned long long)`.

CR-180 adds new `include/Airport/IO80211InterfaceMonitor.h` declaring
all nineteen on an opaque (no vtable / no data layout) `class
IO80211InterfaceMonitor`, with forward declaration of
`IO80211Controller`. No caller wiring is added.

Generated binary is bit-identical to CR-179 (sha256
`c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`, UUID
`BA3D771F-F079-33FF-94E5-C792E66237D8`). CR-180 supersedes CR-179.

## CR-181 closure

After CR-180 sealed the IO80211InterfaceMonitor public surface, the
next layer extends the IO80211InfraInterface direct-call surface from
CR-175 with eleven additional helpers covering IORegistry property
updates and runtime gating. BootKC inspection confirmed all eleven
helpers selected for CR-181, restricted to signatures that contain
only already-known types (`ether_addr`, `apple80211_channel`, `bool`):

- IORegistry property updaters: `updateSSIDProperty`,
  `updateLocaleProperty`, `updateBSSIDProperty`,
  `updateChannelProperty`, `updateCountryCodeProperty`,
  `updateStaticProperties`, `updateLinkSpeed`.
- Channel-table loaders: `loadHwChannels`, `loadChannelInfo`.
- Runtime helpers: `onDispatchQueue`, `cancelDebounceTimer`.

CR-181 extends `include/Airport/IO80211InfraInterface.h` under the
`__IO80211_TARGET >= __MAC_26_0` block with all eleven declarations.
No caller wiring is added.

Generated binary is bit-identical to CR-180 (sha256
`c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`, UUID
`BA3D771F-F079-33FF-94E5-C792E66237D8`). CR-181 supersedes CR-180.

## CR-182 closure

After CR-181 widened the IO80211InfraInterface property updater
surface, the next layer returns to IO80211InterfaceMonitor and extends
the CR-180 nineteen-helper surface with ten additional public exports
that any future caller-wiring CR will need to drive when the data path
records EAPOL/Ethernet TX/RX events into the InterfaceMonitor.
BootKC inspection confirmed all ten helpers selected for CR-182,
restricted to signatures that contain only already-known types
(`apple80211_ssid`, `ether_addr`, `apple80211_wme_ac`, `int`,
`long long`):

- Leaky-AP helpers: `getLeakyApSsid`, `getLeakyApBssid`,
  `resetLeakyApStats`.
- Per-packet record helpers: `setInputPacketRSSI`,
  `recordInputPacket`, `recordOutputPacket`, `initFrameStats`.
- Reporter-lifecycle helpers: `initHeFrameStats`, `destroyReporters`,
  `updateAllReports`.

CR-182 extends `include/Airport/IO80211InterfaceMonitor.h` `class
IO80211InterfaceMonitor` body with all ten declarations and adds
forward declarations for the three required types. No caller wiring
is added.

Generated binary is bit-identical to CR-181 (sha256
`c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`, UUID
`BA3D771F-F079-33FF-94E5-C792E66237D8`). CR-182 supersedes CR-181.

## CR-183 closure

After CR-182 widened the IO80211InterfaceMonitor leaky-AP/reporter
surface, the next layer returns to IO80211Peer and extends the CR-177
six-helper surface with twenty-five additional public exports covering
per-Peer capability flags, TX-credit accounting, RX/TX sequence
counters, and cache/SoftAP-peer state queries. BootKC inspection
confirmed all twenty-five helpers selected for CR-183, restricted to
signatures that contain only already-known types (`bool`, `unsigned
int`, `unsigned long long`, `unsigned char`):

- Capability getters/setters: `getHtCapable`/`setHtCapable`,
  `getVhtCapable`/`setVhtCapable`, `isHeSupported`/`setHeSupported`,
  `is6ECapable`/`set6ECapable`, `hasHTorVHTCaps`.
- Transmit predicates: `canTransmit`, `canTransmitReason`.
- Credit/counter accessors: `getOpenCredits`, `getCloseCredits`,
  `getNumTxPacket`, `getOutputSuccess`.
- TX-quantum/sequence helpers: `getTxQuantum`, `setTxQuantum`,
  `getNextTxSeq`, `setTransmitOk`.
- RX-sequence helpers: `getRxSequence`, `getRxSequenceMulticast`.
- Cache/SoftAP-peer flag accessors: `isCachedInFw`, `setCachedInFw`,
  `isSoftAPPeer`, `setSoftAPPeer`.

CR-183 extends `include/Airport/IO80211Peer.h` `class IO80211Peer`
body with all twenty-five declarations. No caller wiring is added.

Generated binary is bit-identical to CR-182 (sha256
`c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`, UUID
`BA3D771F-F079-33FF-94E5-C792E66237D8`). CR-183 supersedes CR-182.

## CR-184 closure

After CR-183 added Peer capability/credit/counter accessors, the next
batch returns to IO80211Peer and adds thirty-three more public exports
covering per-band RSSI accounting, packet-stats accessors, cache
state, and queue/lifetime helpers. BootKC inspection confirmed all
thirty-three helpers selected for CR-184, restricted to signatures
that contain only already-known types (`bool`, `int`, `signed char`,
`unsigned int`, `unsigned long long`, `apple80211_channel` as a
forward-declared struct):

- Stats-id helpers: `getStatsID`, `getStatsIDValid`.
- RSSI reporters: `reportRssi`, `reportChainRssi`.
- Per-band RSSI getters/setters: `getAvgRssi24G`, `getAvgRssi5G`,
  `getAvgRssiAcrossBands`, `getAvgChainRssi5G`, `setPeerAvgRssi24G`,
  `setPeerAvgRssi5G`.
- Resource/queue helpers: `simulateDPS`, `freeResources`,
  `unpauseQueues`, `reclaimPackets`, `clearCacheState`.
- RX bit-field/count helpers: `getRxBitField`,
  `getRxBitFieldMulticast`, `incrementRxCount`.
- Packet-stats accessors: `getPacketStats`, `getPacketStatsRealTimeRx`,
  `getPacketStatsRealTimeTx`, `getCumDataStats`.
- Predicates: `hasRealTimeData`, `hasLowLatencyData`,
  `hasQueuedPackets`.
- Queue-state/lifetime helpers: `getDataLinkCount`, `logPeerTxLatency`,
  `updateQueueState`, `setPacketLifetime`.
- Cache-timestamp helpers: `getCacheTimeStamp`, `setCacheTimeStamp`.
- BSS-steering predicates: `isBssSteeringPeer`,
  `isBssSteeringPeerSyncState`.

CR-184 extends `include/Airport/IO80211Peer.h` `class IO80211Peer`
body with all thirty-three declarations and adds a forward
declaration for `apple80211_channel`. No caller wiring is added.

Generated binary is bit-identical to CR-183 (sha256
`c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`, UUID
`BA3D771F-F079-33FF-94E5-C792E66237D8`). CR-184 supersedes CR-183.


## CR-185 closure — IO80211PeerManager parameterless accessor batch (2026-04-28)

`IO80211PeerManager` exports a wide parameterless / single-primitive
accessor surface that the local `include/Airport/IO80211PeerManager.h`
did not yet declare. CR-185 covers thirty-three additional public
direct-call helpers, all confirmed against the BootKC IO80211Family
symbol table (recovered 2026-04-28):

- Provider/controller/identity: `getBSDName`, `GetProvider`,
  `getController`, `getInterfaceId`, `getCommandGate`,
  `interfaceMonitor`.
- Country/timing: `getCountryCode`, `getDTIMPeriod`, `getBeaconPeriod`.
- Enabling state: `getEnabling`, `failToEnable`.
- Capabilities: `getHeCapable`, `getVhtCapable`, `getMyHeCap`,
  `getMyVhtCap`, `getRsdbCap`, `getHtCapabilities`, `isRsdbSupported`.
- Dispatch / cache: `onDispatchQueue`, `isPeerCacheFull`,
  `printHashTable`, `removeAllPeers`, `freeResources`.
- AWDL / mbuf / mDNS: `awdlChipReset`, `flushFreeMbufs`,
  `enablemDNSTx`.
- Reporter lifecycle: `destroyReporters`, `updateAllReports`.
- Scanning: `getScanningState`.
- Per-AC output byte counters: `getOutputBEBytes`, `getOutputBKBytes`,
  `getOutputVIBytes`, `getOutputVOBytes`.

CR-185 extends `include/Airport/IO80211PeerManager.h` `class
IO80211PeerManager` body with all thirty-three declarations. No caller
wiring is added.

Generated binary is bit-identical to CR-184 (sha256
`c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`, UUID
`BA3D771F-F079-33FF-94E5-C792E66237D8`). CR-185 supersedes CR-184.


## CR-186 closure — IO80211InfraInterface LQM/WMM/AVC/BT-coex/SIB/ULLA/AWDL/BPF/leaky-AP/supplicant/P2P helper batch (2026-04-28)

`IO80211InfraInterface` exports a substantial non-virtual helper
surface beyond the CR-175/CR-181 entries. CR-186 covers twenty-one
additional public direct-call helpers, all confirmed against the
BootKC IO80211Family symbol table (recovered 2026-04-28):

- LQM data/gate/static: `getLQMData`, `setLQMGated`, `setLQMStatic`.
- Monitor / WMM: `getMonitorMode`, `getWMMBWReset`, `setWMMBWReset`.
- AVC / BT-coex: `getAVCAdvisory`, `getBtCoexState`.
- Interface reset: `resetInterface`.
- Traffic monitor: `getTrafficMonitor`.
- SIB coex / turn-on metrics: `finishSIBCoexTimer`,
  `resetSIBTurnOnMetrics`.
- CoP / ULLA: `getCoPTxRTSFailCount`, `getULLALiteDuration`,
  `UpdateULLADuration`.
- AWDL: `getAwdlMaxBandWidth`, `notifyAWDLStateChange`.
- BPF: `bpfTapInternal`.
- Leaky-AP: `setLeakyAPStatsMode`.
- Supplicant / P2P: `handleSupplicantEvent`, `routeToP2PInterface`.

CR-186 extends `include/Airport/IO80211InfraInterface.h` `class
IO80211InfraInterface` body with all twenty-one declarations. No
caller wiring is added.

Generated binary is bit-identical to CR-185 (sha256
`c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`, UUID
`BA3D771F-F079-33FF-94E5-C792E66237D8`). CR-186 supersedes CR-185.


## CR-187 closure — IO80211SkywalkInterface non-virtual helper batch (2026-04-28)

`IO80211SkywalkInterface` exports a large non-virtual helper surface
beyond the existing virtual vtable. CR-187 covers twenty additional
public direct-call helpers, all confirmed against the BootKC
IO80211Family symbol table (recovered 2026-04-28):

- pid-lock: `pidLockPid`, `setPidLock`.
- Work-queue: `getWorkQueue`.
- Identity: `getInterfaceId`, `getInterfaceRoleStr`.
- Peer manager / monitor: `getPeerManager`, `getPeerMonitor`.
- MAC: `setInitMacAddress`, `getMacAddressAgent`.
- Interface relationships: `getParentInterface`, `getInterfaceMonitor`.
- Predicates: `isLowLatencyEnabled`, `isCommandAllowed`.
- PostMessage: `postMessageInternal`, `postMessageSync`.
- Ioctl route: `routeIoctlToWcl`.
- Device / medium / power: `getDeviceType`, `setDeviceType`,
  `getMediumType`, `getPowerState`.
- Property table: `getPropertyTable`.

Five exports were intentionally deferred from this batch
(`getBSDName`, `getHardwareAddress`, `setHardwareAddress`,
`stringFromReturn`, `errnoFromReturn`) because their declarations would
implicitly override a parent-class virtual and introduce new vtable
references, breaking the bit-identical kext invariant.

CR-187 extends `include/Airport/IO80211SkywalkInterface.h`
`class IO80211SkywalkInterface` body with all twenty declarations.
No caller wiring is added.

Generated binary is bit-identical to CR-186 (sha256
`c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`, UUID
`BA3D771F-F079-33FF-94E5-C792E66237D8`). CR-187 supersedes CR-186.

## CR-188 closure — IO80211InterfaceMonitor extended primitive helpers (2026-04-28)

CR-188 extends `include/Airport/IO80211InterfaceMonitor.h` with
twenty-seven additional non-virtual primitive-only direct-call
declarations recovered from the IO80211Family BootKC symbol table
(see `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/128_monitor_extension_batch_2026_04_28.yaml`).
No caller wiring is added.

Generated binary is bit-identical to CR-187 (sha256
`c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`, UUID
`BA3D771F-F079-33FF-94E5-C792E66237D8`). CR-188 supersedes CR-187.

## CR-189 closure — IO80211InfraInterface additional primitive helpers (2026-04-28)

CR-189 extends `include/Airport/IO80211InfraInterface.h` with eighteen
additional non-virtual primitive-only direct-call declarations
recovered from the IO80211Family BootKC symbol table (see
`docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/129_infra_additional_primitives_2026_04_28.yaml`).
No caller wiring is added.

Generated binary is bit-identical to CR-188 (sha256
`c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`, UUID
`BA3D771F-F079-33FF-94E5-C792E66237D8`). CR-189 supersedes CR-188.

## CR-190 closure — IO80211SkywalkInterface companion-id / pid-lock / dispatch helpers (2026-04-28)

CR-190 extends `include/Airport/IO80211SkywalkInterface.h` with eight
additional non-virtual primitive-only direct-call declarations
recovered from the IO80211Family BootKC symbol table (see
`docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/130_skywalk_companion_batch_2026_04_28.yaml`).
The remaining BootKC IO80211SkywalkInterface non-virtual exports
recovered on 2026-04-28 are intentionally excluded — they are
already declared as virtuals earlier in the same class body and
resolve through the inherited vtable; redeclaring them as non-virtual
would conflict. No caller wiring is added.

Generated binary is bit-identical to CR-189 (sha256
`c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`, UUID
`BA3D771F-F079-33FF-94E5-C792E66237D8`). CR-190 supersedes CR-189.

## CR-191 closure — IO80211PeerManager primitive-only helper batch (2026-04-28)

CR-191 extends `include/Airport/IO80211PeerManager.h` with twenty
additional non-virtual primitive-only direct-call declarations
recovered from the IO80211Family BootKC symbol table (see
`docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/131_peer_manager_primitive_batch_2026_04_28.yaml`).
None of these names match a parent-class virtual or a virtual
already declared in the same class body. No caller wiring is added.

Generated binary is bit-identical to CR-190 (sha256
`c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`, UUID
`BA3D771F-F079-33FF-94E5-C792E66237D8`). CR-191 supersedes CR-190.

## CR-192 closure — IO80211Peer state-flag and counter accessor batch (2026-04-28)

CR-192 extends `include/Airport/IO80211Peer.h` with thirty additional
non-virtual primitive-only direct-call declarations recovered from
the IO80211Family BootKC symbol table (see
`docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/132_peer_state_flag_batch_2026_04_28.yaml`).
None of these names match a method already declared in the same
class body. No caller wiring is added.

Generated binary is bit-identical to CR-191 (sha256
`c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`, UUID
`BA3D771F-F079-33FF-94E5-C792E66237D8`). CR-192 supersedes CR-191.

## CR-193 closure — IO80211Peer timestamp / link-activity / cache-time batch (2026-04-28)

CR-193 extends `include/Airport/IO80211Peer.h` with twenty-four
additional non-virtual primitive-only direct-call declarations
recovered from the IO80211Family BootKC symbol table (see
`docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/133_peer_timestamp_batch_2026_04_28.yaml`).
None of these names match a method already declared in the same
class body. No caller wiring is added.

Generated binary is bit-identical to CR-192 (sha256
`c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`, UUID
`BA3D771F-F079-33FF-94E5-C792E66237D8`). CR-193 supersedes CR-192.

## CR-194 closure — IO80211Peer caching-state and tx-counter batch (2026-04-28)

CR-194 extends `include/Airport/IO80211Peer.h` with thirty-three
additional non-virtual primitive-only direct-call declarations
recovered from the IO80211Family BootKC symbol table (see
`docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/134_peer_caching_state_batch_2026_04_28.yaml`).
None of these names match a method already declared in the same
class body. No caller wiring is added.

Generated binary is bit-identical to CR-193 (sha256
`c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`, UUID
`BA3D771F-F079-33FF-94E5-C792E66237D8`). CR-194 supersedes CR-193.

## CR-196 closure — IO80211LinkQualityMonitor primitive-only batch (NEW header)

CR-196 introduces `include/Airport/IO80211LinkQualityMonitor.h` —
a new local header carrying 48 primitive-only direct-call exports
recovered from the IO80211Family BootKC symbol table (see
`docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/135_lqm_primitive_batch_2026_04_28.yaml`).
The class is declared with no base class and no data layout; the
local kext does not allocate, subclass, or take `sizeof` of it. No
caller wiring is added.

Generated binary is bit-identical to CR-189..CR-195 (kext sha256
`c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`,
UUID `BA3D771F-F079-33FF-94E5-C792E66237D8`, regdiag sha256
`6915020cdd70a07c4b77b2946dd5605bc378fc0677119506ae691a7968f01fad`).
CR-196 supersedes CR-195.

## CR-199 closure — IO80211BSSBeacon primitive-only batch (NEW header, supersedes CR-198)

CR-199 introduces `include/Airport/IO80211BSSBeacon.h` — a new local
header carrying 102 primitive-only direct-call exports recovered
from the IO80211Family BootKC symbol table (see
`docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/136_bssbeacon_primitive_batch_2026_04_28.yaml`).
The class is declared with no base class and no data layout; the
local kext does not allocate, subclass, or take `sizeof` of it. No
caller wiring is added. CR-199 supersedes CR-198 by deferring the
five exports whose return types are unrecovered kernel-internal
pointers (`getLogger`, `getSSID`, `getOWETransSSID`, `getRnRContext`,
`getQueueChain`); CR-198 had declared those five with `void *`,
flagged as type erasure inside a 1:1 reference-alignment patch.

Generated binary is bit-identical to CR-189..CR-198 (kext sha256
`c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`,
UUID `BA3D771F-F079-33FF-94E5-C792E66237D8`, regdiag sha256
`6915020cdd70a07c4b77b2946dd5605bc378fc0677119506ae691a7968f01fad`).
CR-199 supersedes CR-198.

## CR-200 closure — IO80211BssManager primitive-only batch (REJECTED, superseded by CR-201)

CR-200 proposed forty-one primitive-only direct-call exports of
`IO80211BssManager`. The reviewer rejected the request because each
declared return type was inferred from naming convention rather than
backed by decompile evidence. Superseded by CR-201.

## CR-201 closure — IO80211BssManager primitive-only batch (decomp-evidenced)

CR-201 introduces `include/Airport/IO80211BssManager.h` — a new
local header carrying 14 primitive-only direct-call exports recovered
from the IO80211Family BootKC symbol table and matched against the
verbatim Ghidra 12.2 C decompile of `BootKernelExtensions.kc` (output
captured in `analysis/cr201_bssmgr_decomp.c`; YAML in
`docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/137_bssmanager_primitive_batch_2026_04_28.yaml`).
Each declared return type matches Ghidra's emitted C type at the
function entry (`void` → `void`, `bool` → `bool`, `byte` →
`unsigned char`, `ulong` → `unsigned long`). The class is declared
with no base class and no data layout; the local kext does not
allocate, subclass, or take `sizeof` of it. No caller wiring is added
— pre-existing kext mentions of `IO80211BssManager` are documentation
comments only.

2026-07-07 addendum: later CR-479 work adds live current-BSS writer wiring for
`setMCSIndexSet(apple80211_mcs_index_set_data&)` and
`setRateSet(apple80211_rate_set_data&)`. That addendum is separate from the
CR-201 fourteen-helper primitive-only batch: it is backed by
AppleBCMWLANNetAdapter caller-side disassembly and symbol metadata, and it
does not promote any of the CR-201 deferred opaque-return helpers.

Twenty-seven helpers from the CR-200 forty-one-symbol candidate set
are deferred:
- twelve where Ghidra emitted an opaque placeholder return type
  (`undefined4` / `undefined8`) — promoting these to a concrete C++
  type would be inference, not decomp evidence;
- nine where Ghidra did not recover the mangled symbol name
  (function present but reported as `FUN_<addr>`);
- six where Ghidra reported MISSING at the recorded address (likely
  an analyzer state artifact between import passes).

The previously-noted kernel-internal-typed BssManager exports
(apple80211_*, ether_addr, Bands, IO80211AuthContext, IO80211BSSBeacon,
IO80211ScanCacheStore, CCLogStream, env_bss_info, iOSIESubType,
apple_mlo_context, apple80211_softap_wifi_network_info, IO80211Logger,
out-parameter primitive getters) likewise remain deferred. No `void *`
is substituted for any unrecovered kernel-internal return type.

Generated binary is bit-identical to CR-189..CR-200 (kext sha256
`c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`,
UUID `BA3D771F-F079-33FF-94E5-C792E66237D8`, regdiag sha256
`6915020cdd70a07c4b77b2946dd5605bc378fc0677119506ae691a7968f01fad`).
CR-201 supersedes CR-200.

## 2026-07-11 correction: DRIVER_AVAILABLE lifecycle producers

The earlier boolean-only `postTahoeDriverAvailableBulletin(ready)` model was
incomplete. Current 25C56 decompilation proves that
`AppleBCMWLANCore::signalDriverReady()` only publishes
`CoreWiFiDriverReadyKey`; separate `bootChipImage`, `powerOff`, and `powerOn`
paths call `IO80211Controller::postMessage(..., 0x37, ..., 0xf8, true)`.

Their first six dwords are not interchangeable. Boot-ready uses
`{3, 0x20, 1, 0, 0xe0822803, 0}`, power-off uses
`{3, 0, 0, 0, 0xe0821804, 0}`, and power-on uses
`{3, 0, 1, 0, 0xe0821803, 0}`. Selector `0x37` is a call argument, not payload
dword `+0x00`. The local layer now uses these exact transition builders and
does not reuse a normal carrier for watchdog/fault publication. See
`docs/reference/CR-479-driver-availability-producers-20260711.md`.

## 2026-07-11 correction: driver-owned BssManager lifecycle

The previous Tahoe bridge confused two valid but separately owned framework
objects. `WCLController` creates WCL's BssManager and WCLConfigManager retains
it. Independently, `AppleBCMWLANCore::initAfterIORegUpdated()` creates an
`AppleBCMWLANBssManager` and stores it in core state. Apple driver writers use
the latter and never recover it through WCL private offsets.

The local driver now owns a genuine framework BssManager for its started
lifetime. Only `setWCL_LINK_STATE_UPDATE` creates or clears its current BSS,
using a fresh genuine framework beacon built from the exact `0x844` carrier.
The generic link-state retry burst, private WCL pointer walk, and synthetic LQM
producer are removed. `isAssociated()` is current-pointer presence; it does
not consume the separate feature-gated bool passed to `setCurrentBSS`. See
`docs/reference/CR-479-driver-owned-bssmanager-lifecycle-20260711.md`.

## 2026-07-11 correction: driver-owned LQM statistics producer

Current AppleBCMWLAN decompilation closes the producer below the framework LQM
consumer. Apple owns a dedicated timer, defaults it to 5000 ms, starts it on
association, rearms it only while the driver BssManager remains associated,
and posts a real `0x1dc` event `0x27` through the Infra endpoint.

The local driver now follows that ownership with a separate timer,
command-gated net80211/HAL snapshots, firmware-derived beacon counters, and
exact event validity/generation fields. It does not restore the removed
private-WCL bulletin shortcut. Final UUID
`09663B25-365D-3D90-BE59-D50490351847` delivered 50
`WCLNetManager::handleLqmUpdate` calls over 250 seconds while 240-second ping
plus iperf3 completed with zero loss and no driver/host fault. See
`docs/reference/CR-479-driver-owned-lqm-statistics-producer-20260711.md`.

## 2026-07-13 correction: BSS blacklist async signal chain

The previous `setBSS_BLACKLIST`/`getBSS_BLACKLIST` lift preserved the 43-byte
blob synchronously. That shape was not the Apple signal chain. Current 25C56
shows:

```text
BSD selector 0x174 (exact length 0x2b)
  -> public Infra wrapper
  -> AppleBCMWLANCore requested state
  -> lower MACMODE/MACLIST programming
  -> async GET_MACLIST
  -> successful non-empty callback may publish message 0xa3
     (u32 count + 6*n BSSID + 2 bytes)
```

SET returns success after non-null input even when the lower list rejects a
count of eight or more; requested state changes while applied state remains
the prior valid list. GET never writes the synchronous carrier. Both SET and
GET launch the async query. Publication remains conditional on callback status,
payload, and non-zero callback count; count zero suppresses publication only
when such a callback is actually invoked.

AirportItlwm now routes selector-bearing `0x174` with reference P0 error
precedence (`0x66` before `0x16`), admission before the owner cast, and raw
`0xe082280e` for the proven absent-owner branch. It owns requested/applied
state in the controller lifetime domain, serializes it on the command gate,
and publishes the variable controller-level event. This is not described as a
firmware query callback: lower status/null-payload branches remain open. It
deliberately does not filter scan output or mark a node with an
association-failure bit. WCL candidate code marks and reorders deny-listed
candidates after non-denied candidates, retaining fallback; the separate
lower-selection mapping remains open until its own runtime gate.

See `docs/reference/CR-479-bss-blacklist-async-owner-20260713.md`.
