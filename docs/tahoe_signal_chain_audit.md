# Tahoe Signal Chain Audit

Date: 2026-04-12

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
- payload-less `VIRTUAL_IF_ROLE/PARENT` fell through to raw POSIX `6` instead
  of returning Apple `0xe082280e`.
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
- no clean `setPOWER_BUDGET(...)` producer body has been recovered yet
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
