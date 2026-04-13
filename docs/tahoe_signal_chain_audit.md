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
- `setWCL_REAL_TIME_MODE`
- `setWCL_ARP_MODE`
- `setWCL_JOIN_ABORT`
- `setWCL_QOS_PARAMS`
- `setWCL_LINK_UP_DONE`
- `setWCL_CONFIG_BG_MOTIONPROFILE`
- `setWCL_CONFIG_BG_NETWORK`
- `setWCL_CONFIG_BGSCAN`
- `setWCL_CONFIG_BG_PARAMS`

For those slots the Apple producer delegates into roam/net/bgscan/join/power
subsystems. The correct next step is not another ack-only patch, but lifting the
missing adapter-plane behavior from the reference decompiles and wiring it into
our Tahoe path.

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

`getTXPOWER` also leaves the raw-`6` queue in this batch, but its remaining
Apple mismatch is no longer a `Q5` item. The exact reference producer still
uses the config-backed `"qtxpower"` transport, so the source-transport
exactness remains open under `Q13`, not under the old raw-getter queue.

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
