# CR-479: MWS scan-frequency false-success quarantine

Date: 2026-07-13

## Scope

This closure covers only the Tahoe public setter boundary for
`MWS_SCAN_FREQ_WIFI_ENH`. It does not emulate Apple's Broadcom MWS
(coexistence) firmware policy on Intel hardware, and does not change scan mode,
Condition-ID, antenna, or another MWS selector.

## Recovered reference contract

The Tahoe 25C56 reference image is
`/System/Library/DriverExtensions/com.apple.DriverKit-AppleBCMWLAN.dext/com.apple.DriverKit-AppleBCMWLAN`
with SHA-256
`4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab`.

The Infra slot-[654] wrapper at `0x100019704` tail-dispatches to
`AppleBCMWLANCore::setMWS_SCAN_FREQ_WIFI_ENH` at `0x100141708`:

- `NULL -> 0xe00002bc`;
- a valid opaque carrier contributes ten effective dwords in reference order:
  raw `+0x24` is stored at Core `+0x299c`, then raw `+0` through `+0x20` are
  stored at Core `+0x29a0` through `+0x29c0`; and
- the setter tail-dispatches virtual offset `+0x628`.

The recovered `__ZTV16AppleBCMWLANCore` starts at `0x1003a10d8`. The Core
object vptr address point is `+0x10`, so its virtual offset `+0x628` selects
raw entry `0x1003a1710`, whose image-local target is `0x100122806`,
`setWiFiType4BlankingBitmapsWiFiEnh`. That terminal allocates its transfer
area and describes a 40-byte (`0x28`) `mws` firmware-iovar packet with command `0x1018000`
and subcommand `7`. It packs ten low-16-bit bitmap fields, uses
`AppleBCMWLANCommander::sendIOVarSet` with
`handleMWSWiFiType4BlankCoexBitmapsWiFiEnhAsyncCallback`, or
`runIOVarSet("mws")`; its return status is preserved. The reference surface
therefore has real firmware work, not merely a cache acknowledgement.

The recovered effective reads establish only the ten dwords and their reorder;
they do not prove the complete public-carrier allocation size or Apple
valid-input return-code parity.

## Local divergence

Before this correction, the Intel setter rejected null but otherwise copied ten
raw dwords into `cachedMwsScanFreq` and returned success. Scoped local source
inspection finds that cache only in its declaration, two initializers, and this
setter. There is no local `mws` iovar, `setWiFiType4BlankingBitmapsWiFiEnh`
owner, matching callback, `sendIOVarSet`, or `runIOVarSet` backend. Existing
generic Intel coexistence code is not treated as an equivalent of Apple's MWS
firmware contract.

## Local correction

Retain the recovered null error. Every non-null request now returns
`kIOReturnUnsupported` before cache or transport mutation, and the dead cache
plus its initializers are removed.

This is a no-backend quarantine, **not Apple valid-input return-code parity**:
the reference accepts the input and performs its Broadcom firmware operation.

## Verification boundary

The deterministic report requires the reference wrapper/Core/vtable/terminal
anchors, reordered-ten-dword/subcommand/payload description, retained null
guard, non-null fail-closed return, removed pseudo-state, and absence of the
local owner or transport tokens. Build/load and normal association/traffic
gates establish regression safety. No guessed opaque carrier or private setter
ioctl is issued, so runtime results must not be presented as direct setter
execution. Radio OFF/ON remains excluded because the A2DF baseline
independently reproduces its WCL lifecycle panic.
