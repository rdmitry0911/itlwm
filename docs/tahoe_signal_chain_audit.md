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
