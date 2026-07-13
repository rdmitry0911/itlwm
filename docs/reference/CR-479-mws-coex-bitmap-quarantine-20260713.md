# CR-479: MWS COEX bitmap false-success quarantine

Date: 2026-07-13

## Scope

This closure covers only the Tahoe public setter boundary for
`MWS_COEX_BITMAP_WIFI_ENH`. It does not emulate Apple's Broadcom MWS
(coexistence) firmware policy on Intel hardware, and does not change another
MWS selector.

## Recovered reference contract

The Tahoe 25C56 reference image is
`/System/Library/DriverExtensions/com.apple.DriverKit-AppleBCMWLAN.dext/com.apple.DriverKit-AppleBCMWLAN`
with SHA-256
`4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab`.

The Infra slot-[650] wrapper at `0x100019624` tail-dispatches to
`AppleBCMWLANCore::setMWS_COEX_BITMAP_WIFI_ENH` at `0x100140f5a`:

- `NULL -> 0xe00002bc`;
- a valid opaque carrier contributes nine effective dwords at `+0` through
  `+0x20`, which the Core setter stores at Core `+0x292c` through `+0x294c`;
  and
- the setter tail-dispatches virtual offset `+0x610`.

The recovered `__ZTV16AppleBCMWLANCore` starts at `0x1003a10d8`; its
virtual-base entry `+0x610` is `0x1003a16f8`, holding
`0x100122074`, `setMWSCoexBitmapsWiFiEnh`. That terminal owner creates a
36-byte (`0x24`) `mws` firmware-iovar packet with command `0x1018000` and
subcommand `2`, packing nine low-16-bit bitmap fields. It uses
`AppleBCMWLANCommander::sendIOVarSet` with
`handleMWSCoexBitmapsWiFiEnhAsyncCallback`, or `runIOVarSet("mws")`; its
return status is preserved. The reference surface therefore has real firmware
work, not merely a cache acknowledgement.

The recovered effective reads establish only the nine dwords described above;
they do not prove the complete public-carrier allocation size.

## Local divergence

Before this correction, the Intel setter rejected null but otherwise copied
nine dwords into `cachedMwsCoexBitmap` and returned success. Scoped local
source inspection finds that cache only in its declaration, two initializers,
and this setter. There is no local `mws` iovar, COEX bitmap owner, matching
callback, `sendIOVarSet`, or `runIOVarSet` backend. Existing generic Intel
coexistence code is not treated as an equivalent of Apple's MWS firmware
contract.

## Local correction

Retain the recovered null error. Every non-null request now returns
`kIOReturnUnsupported` before cache or transport mutation, and the dead cache
plus its initializers are removed.

This is a no-backend quarantine, **not Apple valid-input return-code parity**:
the reference accepts the input and performs its Broadcom firmware operation.

## Verification boundary

The deterministic report requires the reference wrapper/Core/vtable/terminal
anchors, nine-dword/subcommand/payload description, retained null guard,
non-null fail-closed return, removed pseudo-state, and absence of the local
COEX owner or transport tokens. Build/load and normal association/traffic gates
establish regression safety. No guessed opaque carrier or private setter ioctl
is issued, so runtime results must not be presented as direct setter execution.
Radio OFF/ON remains excluded because the A2DF baseline independently
reproduces its WCL lifecycle panic.
