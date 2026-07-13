# CR-479: Battery powersave configuration false-success quarantine

Date: 2026-07-13

## Scope

This closure covers only the Tahoe public setter boundary for
`BATTERY_POWERSAVE_CONFIG`. It does not emulate Apple's MIMO-power-save or
MRC-threshold firmware policy on Intel hardware.

## Recovered reference contract

The Tahoe 25C56 reference image is
`/System/Library/DriverExtensions/com.apple.DriverKit-AppleBCMWLAN.dext/com.apple.DriverKit-AppleBCMWLAN`
with SHA-256
`4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab`.

The Infra slot-[613] wrapper at `0x100018f44` reaches
`AppleBCMWLANCore::setBATTERY_POWERSAVE_CONFIG` at `0x100142544`:

- `NULL -> 0xe00002bc`;
- a valid request consumes effective dword `+0` and calls
  `setBatterySaveModeConfiguration` at `0x10013a106`; and
- the outer Core setter returns success after that helper.

The Core helper stores the raw dword at Core `+0x3f8c`, and reaches Power
Manager only when MIMO power-save is enabled. The Power Manager helper at
`0x100172cb8` repeats that gate and, while associated, selects an MRC threshold
for supported input values. `configureMRCThreshold` at `0x100171452` uses a
four-byte signed payload and real Commander `sendIOVarSet` /
`runIOVarSet` work for firmware iovar `mrc_rssi_threshold`. Thus the
reference surface has an actual conditional firmware policy path, not merely a
cache acknowledgement.

The outer valid-input return is success even when internal command failures are
logged. This record proves only the effective dword read, not the public
carrier's complete allocation size.

## Local divergence

Before this correction, the Intel setter rejected null but otherwise wrote only
`cachedBatteryPowerSaveMode` and returned success. The cache has no consumer:
it occurs only in its declaration, two initializers, and this setter. Scoped
source inspection finds no BatterySaveMode owner,
`mrc_rssi_threshold`, `configureMRCThreshold`, or selector-specific Intel
firmware mapping. The generic local Tahoe Commander only completes a
bookkeeping context with status zero; it is not the recovered MRC backend.

## Local correction

Retain the recovered null error. Every non-null request returns
`kIOReturnUnsupported` before cache or commander mutation, then remove the
dead cache and its initializers.

This is a no-backend quarantine, **not Apple valid-input return-code parity**:
the reference may return success after its conditional real firmware work.

## Verification boundary

The deterministic report requires the reference anchors and state-gate
description, retained null guard, non-null fail-closed return, removed
pseudo-state, absence of a local MRC backend token, and the generic
Commander's synthetic-completion shape. Build/load and normal
association/traffic gates establish regression safety. A public route that
stops before the private setter is not presented as direct setter invocation.
Radio OFF/ON remains excluded because the A2DF baseline independently
reproduces its WCL lifecycle panic.
