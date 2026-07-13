# CR-479: LMTPC configuration false-success quarantine

Date: 2026-07-13

## Scope

This closure covers only the Tahoe public setter boundary for
`LMTPC_CONFIG`.  It does not emulate Apple's firmware LMTPC owner on Intel
hardware.

## Recovered reference contract

The Tahoe 25C56 reference image is
`/System/Library/DriverExtensions/com.apple.DriverKit-AppleBCMWLAN.dext/com.apple.DriverKit-AppleBCMWLAN`
with SHA-256
`4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab`.

The Infra public wrapper at `0x1000193a4` tail-jumps to
`AppleBCMWLANCore::setLMTPC_CONFIG` at `0x100142c22`, which:

- returns `0xe00002bc` for `NULL`;
- consumes and zero-extends effective input byte `+0` into Core offset
  `+0x4594`; and
- calls the Core LMTPC owner.

The owner at `0x1000fe4c0` performs real Commander `sendIOVarSet` /
`runIOVarSet` work for firmware iovar `lpc`, using a four-byte payload under a
firmware-version gate of at least `0x1123`.  A valid reference request is
therefore firmware-owned policy work, not a caller-visible cache update.
The outer Core setter itself returns success after the owner call; this record
does not claim Apple valid-input error propagation.  The recovered effective
byte is not a claim about the public carrier's complete allocation size.

## Local divergence

Before this correction, non-null local input was reduced through a local
one-byte effective-carrier model, copied only into `cachedLmtpcValue`, and
returned success.
The cache has no consumer: it occurs only in its declaration, two
initializers, and this setter.  Scoped source inspection finds no LMTPC owner,
`lpc` iovar, `runSetLMTPC`, or Intel firmware transport.  The generic local
Tahoe Commander only completes a bookkeeping context with status zero; it is
not the recovered LMTPC firmware backend.

## Local correction

Retain the recovered null error.  Every non-null request returns
`kIOReturnUnsupported` before cache or commander mutation.  Remove the dead
cache, its initializers, and the now-unused one-byte carrier declaration.

This is a no-backend quarantine, **not Apple valid-input return-code parity**:
the reference performs real `lpc` firmware work for a valid carrier.

## Verification boundary

The deterministic report requires the reference anchors, retained null guard,
non-null fail-closed return, removed pseudo-state, absence of a local LMTPC
backend token, and the generic Commander's synthetic-completion shape.
Build/load and normal association/traffic gates establish regression safety.
If a public ioctl stops before the private setter, it is not represented as
direct setter invocation.  Radio OFF/ON remains excluded because the A2DF
baseline independently reproduces its WCL lifecycle panic.
