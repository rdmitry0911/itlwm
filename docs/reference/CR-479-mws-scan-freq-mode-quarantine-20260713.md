# CR-479: MWS scan-frequency-mode false-success quarantine

Date: 2026-07-13

## Scope

This closure covers only the Tahoe public setter boundary for
`MWS_SCAN_FREQ_MODE_WIFI_ENH`. It does not emulate Apple's Broadcom MWS
(coexistence) firmware policy on Intel hardware, and does not change scan
frequency, Condition-ID, antenna, radio state, power management, or another
MWS selector.

## Recovered reference contract

The Tahoe 25C56 reference image is
`/System/Library/DriverExtensions/com.apple.DriverKit-AppleBCMWLAN.dext/com.apple.DriverKit-AppleBCMWLAN`
with SHA-256
`4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab`.

The Infra slot-[655] wrapper at `0x10001973c` tail-dispatches to
`AppleBCMWLANCore::setMWS_SCAN_FREQ_MODE_WIFI_ENH` at `0x100141910`.
`NULL -> 0xe00002bc`. For a non-null opaque carrier, the only effective
reads are raw `+0x24` into Core `+0x299c`, then raw `+0x04`, raw `+0x08`,
and raw `+0x0c` into Core `+0x29a0`, `+0x29a4`, and
`+0x29a8`. Raw `+0` is not read. The Core setter then tail-dispatches
virtual offset `+0x630`.

The recovered `__ZTV16AppleBCMWLANCore` begins at `0x1003a10d8`. The Core
object vptr address point is `+0x10`, so virtual offset `+0x630` selects
raw entry `0x1003a1718`, whose image-local target is `0x100122a9c`,
`setWiFiType4BlankingModeBitmapsWiFiEnh`. The terminal allocates its
transfer area and describes a 40-byte (`0x28`) `mws` firmware-iovar packet
with command `0x1018000` and subcommand `12`. It packs the freqIndex low-16-bit
field and three bitmap low-16-bit fields, uses
`AppleBCMWLANCommander::sendIOVarSet` with
`handleMWSWiFiType4BlankModeCoexBitmapsWiFiEnhAsyncCallback`, or
`runIOVarSet("mws")`; return status is preserved. The reference surface
therefore performs real firmware work rather than merely acknowledging a
cache update.

The recovered effective reads establish only those four fields and their
reorder. They do not prove the full public-carrier allocation size or Apple
valid-input return-code parity.

## Local divergence

Before this correction, the Intel setter rejected null but otherwise copied
four dwords into `cachedMwsScanFreqMode` and returned success. Scoped local
source inspection finds that cache only in its declaration, two initializers,
and this setter. There is no local `mws` iovar,
`setWiFiType4BlankingModeBitmapsWiFiEnh` owner, matching callback,
`sendIOVarSet`, or `runIOVarSet` backend. Existing generic Intel
coexistence code is not treated as an equivalent of Apple's MWS firmware
contract.

## Local correction

Retain the recovered null error. Every non-null request now returns
`kIOReturnUnsupported` before cache or transport mutation, and the dead
cache plus its initializers are removed.

This is a no-backend quarantine, **not Apple valid-input return-code parity**:
the reference accepts the input and performs its Broadcom firmware operation.

## Verification boundary

The deterministic report requires the reference wrapper/Core/vtable/terminal
anchors, reordered-field/subcommand/payload description, retained null guard,
non-null fail-closed return, removed pseudo-state, and absence of local owner
or transport tokens. Build/load and normal association/traffic gates establish
regression safety. No guessed opaque carrier or private setter ioctl is issued,
so runtime results must not be presented as direct setter execution. Radio
OFF/ON remains excluded because the A2DF baseline independently reproduces its
WCL lifecycle panic.
