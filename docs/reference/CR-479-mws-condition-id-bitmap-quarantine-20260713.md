# CR-479: MWS Condition-ID bitmap false-success quarantine

Date: 2026-07-13

## Scope

This closure covers only the Tahoe public setter boundary for
`MWS_CONDITION_ID_BITMAP_WIFI_ENH`. It does not emulate Apple's Broadcom MWS
(coexistence) firmware policy on Intel hardware, and does not change
scan-frequency, antenna selection, radio state, power management, WCL,
association, or another MWS selector.

## Recovered reference contract

The Tahoe 25C56 reference image is
`/System/Library/DriverExtensions/com.apple.DriverKit-AppleBCMWLAN.dext/com.apple.DriverKit-AppleBCMWLAN`
with SHA-256
`4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab`.

The Infra slot-[656] wrapper at `0x100019774` tail-dispatches to
`AppleBCMWLANCore::setMWS_CONDITION_ID_BITMAP_WIFI_ENH` at `0x100141a2e`.
`NULL` or `data[2] == 0` returns `0xe00002bc`. For a valid carrier, the
Core loops while `i < uint8(data[2])`. Each record reads its condition ID at
raw `+0x28 + 0x28*i`, and nine 32-bit bitmap words from raw `+0x04 + 0x28*i`
through raw `+0x24 + 0x28*i`. It stages the ID at Core
`+0x29c4` and the bitmap words at `+0x292c` through `+0x294c`, calls
virtual offset `+0x638`, and returns immediately on a non-zero terminal
status; only a fully successful loop returns zero.

The recovered `__ZTV16AppleBCMWLANCore` begins at `0x1003a10d8`. The Core
object vptr address point is `+0x10`, so virtual offset `+0x638` selects
raw entry `0x1003a1720`, whose image-local target is `0x100123df8`,
`setWiFiConditionIdBitmapsWiFiEnh`. The terminal allocates `0x400` bytes
(allocation failure `0xe00002bd`) and constructs an `mws` firmware packet
with command `0x1018000` and subcommand `10`. CommandTxPayload length
`0x24` has embedded body length `0x1c`: byte `7` is at payload
`+0x10`, the condition ID is at payload `+0x11`, and nine bitmap low-16-bit
fields occupy payload `+0x12` through `+0x22`. It uses
`AppleBCMWLANCommander::sendIOVarSet` with
`handleMWSWiFiConditionIdCoexBitmapsWiFiEnhAsyncCallback`, or
`runIOVarSet("mws")`; return status is preserved. The reference surface
therefore performs per-record firmware work rather than merely acknowledging
local pseudo-state.

The recovered loop establishes its validation, stride, nine effective bitmap
words, and terminal transport. It does not prove the complete public-carrier
allocation size or Apple valid-input return-code parity.

## Local divergence

Before this correction, the Intel setter retained the same null/count-zero
rejections but otherwise copied only `count * 0x28` bytes starting at raw
`+0x28` into a clamped local cache, set a validity flag, and returned success.
It did not consume the first record's bitmap prefix at raw `+0x04` through
`+0x24`, send each condition record, or propagate a transport result.
Scoped local source inspection finds the three cache members only in their
declarations, two initializers, and this setter. There is no local `mws`
iovar, `setWiFiConditionIdBitmapsWiFiEnh` owner, matching callback,
`sendIOVarSet`, or `runIOVarSet` backend. Existing generic Intel
coexistence code is not treated as an equivalent of Apple's MWS firmware
contract.

## Local correction

Retain both recovered bad-argument checks. Every non-null carrier whose count
is non-zero now returns `kIOReturnUnsupported` before cache or transport
mutation, and the dead cache members plus their initializers are removed.

This is a no-backend quarantine, not Apple valid-input return-code parity: the
reference accepts a valid carrier and performs its Broadcom firmware operation.

## Verification boundary

The deterministic report requires the reference wrapper/Core/vtable/terminal
anchors, count loop and record geometry, payload description, retained
bad-argument checks, non-null fail-closed return, removed pseudo-state, and
absence of the local owner or transport tokens. Build/load and normal
association/traffic gates establish regression safety. No guessed opaque
carrier or private setter ioctl is issued, so runtime results must not be
presented as direct setter execution. Radio OFF/ON remains excluded because
the A2DF baseline independently reproduces its WCL lifecycle panic.
