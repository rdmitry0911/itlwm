# CR-479: MWS antenna-selection false-success quarantine

Date: 2026-07-13

## Scope

This closure covers only the Tahoe public setter boundary for
`MWS_ANTENNA_SELECTION_WIFI_ENH`. It does not emulate Apple's Broadcom MWS
(coexistence) firmware policy on Intel hardware, and does not change
Condition-ID, scan frequency/mode, radio state, power management, WCL,
association, or another MWS selector.

## Recovered reference contract

The Tahoe 25C56 reference image is
`/System/Library/DriverExtensions/com.apple.DriverKit-AppleBCMWLAN.dext/com.apple.DriverKit-AppleBCMWLAN`
with SHA-256
`4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab`.

The Infra slot-[657] wrapper at `0x1000197ac` follows the Core pointer and
tail-dispatches to `AppleBCMWLANCore::setMWS_ANTENNA_SELECTION_WIFI_ENH` at
`0x100141cbc`. Its only recovered input validation is `NULL -> 0xe00002bc`.
For a non-null opaque carrier, Core first reads raw `+0x10` as wifiBand and
stores it at Core `+0x28e4`; it then copies the eight u16 selectors at raw `+0x00`
through raw `+0x0e` into Core `+0x28e6` through `+0x28f4`.
It tail-dispatches virtual offset `+0x588` with the resulting Core block.

The recovered `__ZTV16AppleBCMWLANCore` begins at `0x1003a10d8`. The Core
object vptr address point is `+0x10`, so virtual offset `+0x588` selects
raw entry `0x1003a1670`, whose image-local target is `0x10012351c`,
`setANTENNA_SELECTION_V3_WiFiEnh`. The terminal allocates `0x400` bytes
(allocation failure `0xe00002bd`) and constructs an `mws` firmware packet
with command `0x1018000` and subcommand `4`. CommandTxPayload length
`0x74` has embedded body length `0x6c`: it writes wifiBand at payload
`+0x10` and places eight u16 selectors at payload `+0x12` for band 1,
`+0x32` for band 2, or `+0x52` for band 3. Other band values take the
logged common-send path; the trace does not establish a separate invalid-input
return. It uses `AppleBCMWLANCommander::sendIOVarSet` with
`handleMWSAntSelCoexBitmapsWiFiEnhAsyncCallback`, or `runIOVarSet("mws")`;
return status is preserved. The reference surface therefore performs real
firmware work rather than merely acknowledging local pseudo-state.

The recovered effective reads establish the nine u16 fields and their reorder,
but do not prove the complete public-carrier allocation size or Apple
valid-input return-code parity.

## Local divergence

Before this correction, the Intel setter rejected null but otherwise copied
the eight selectors and raw `+0x10` into `cachedMwsAntennaSelection` and
returned success. Scoped local source inspection finds that cache only in its
declaration, two initializers, and this setter. There is no local `mws`
iovar, `setANTENNA_SELECTION_V3_WiFiEnh` owner, matching callback,
`sendIOVarSet`, or `runIOVarSet` backend. Existing generic Intel
coexistence code is not treated as an equivalent of Apple's MWS firmware
contract.

## Local correction

Retain the recovered null error. Every non-null request now returns
`kIOReturnUnsupported` before cache or transport mutation, and the dead
cache plus its initializers are removed.

This is a no-backend quarantine, not Apple valid-input return-code parity: the
reference accepts a valid carrier and performs its Broadcom firmware operation.

## Verification boundary

The deterministic report requires the reference wrapper/Core/vtable/terminal
anchors, field reorder and band-dependent payload description, retained null
guard, non-null fail-closed return, removed pseudo-state, and absence of local
owner or transport tokens. Build/load and normal association/traffic gates
establish regression safety. No guessed opaque carrier or private setter ioctl
is issued, so runtime results must not be presented as direct setter execution.
Radio OFF/ON remains excluded because the A2DF baseline independently
reproduces its WCL lifecycle panic.
