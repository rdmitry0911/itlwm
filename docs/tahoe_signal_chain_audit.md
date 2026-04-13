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

## Root Cause After Live `08aa5ec`

The next architectural mismatch was earlier than any individual IOC handler.
The Tahoe port was still constructing the primary interface through the old
V16-style `IO80211InfraInterface::init()` path instead of the real 26.x attach
contract.

Recovered attach chain from
`docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/85_bsd_attach_chain_xref_checked.yaml`
is explicit:

- step 1 on Tahoe is `IO80211InfraInterface::init(IOService*, ether_addr*)`
- that path allocates the Skywalk/Infra internal state block and prepares the
  later BSD attach chain before `registerEthernetInterface`, `start()`,
  `deferBSDAttach(false)`, and the BSDClient callbacks

Our local source still did the opposite:

- `AirportItlwmSkywalkInterface::init(IOService*, ether_addr*)`
- intentionally bypassed the 2-argument Tahoe init
- called plain `IO80211InfraInterface::init()`
- kept an old comment claiming the Tahoe overload was wrong and that V16 was
  the "working" architecture

That source comment is no longer defensible against the recovered xref-level
attach chain. It means the port was constructing the interface with the wrong
producer-side initialization sequence before any of the later readiness, role,
or scan consumers even ran.

Why this matters for the live failure:

- the Apple core-side object at `+0x1510` is used immediately for producer-side
  state and property publication (`CoreWiFiDriverReadyKey`, platform/ring
  property acquisition, init failure reporting, boot checkpoints)
- on our Tahoe path, `CoreWiFiDriverReadyKey="true"` became visible in `ioreg`
  only after we manually pushed it there, yet family-side
  `isDriverAvailable` still remained `0`
- that is consistent with building the interface via a non-equivalent init path:
  we reproduced a surface property, but not the same internal interface-side
  state carrier chain that Apple establishes during `init(provider, addr)`

So the next strict-parity fix is not another IOC special-case. The fix is to
restore the Tahoe init path itself:

- call `IO80211InfraInterface::init(provider, addr)` on 26.x
- stop treating the old no-arg init as authoritative for Tahoe
- keep the reasoning in source comments so the port does not regress back to
  the V16 shortcut

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
