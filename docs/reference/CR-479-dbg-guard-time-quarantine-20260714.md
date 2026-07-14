# CR-479 — DBG guard-time false-success quarantine

Date: 2026-07-14

## Scope

This correction covers the paired
`getDBG_GUARD_TIME_PARAMS` / `setDBG_GUARD_TIME_PARAMS` selectors. They are
not a local configuration/readback pair: Tahoe sends and receives a private
command-owned `forced_pm` value. The port removes the write-only cache and
rejects non-null use rather than claiming that an unapplied configuration or
fabricated readback succeeded.

## Tahoe 25C56 reference recovery

The recovered DEXT is
`/System/Library/DriverExtensions/com.apple.DriverKit-AppleBCMWLAN.dext/com.apple.DriverKit-AppleBCMWLAN`,
SHA-256
`4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab`.

- Getter Infra wrapper `0x100017210` dispatches Core virtual `+0x2d0`. Core
  vtable `0x1003a10d8` entry `0x1003a13b8` resolves `0x100106ce8`, which
  requests eight bytes through Commander
  `runIOVarGet("forced_pm", kNoTxPayload, ...)` `0x10017b780`.
  On status `0` or `0xe00002e3`, it copies RX `[0..1]` to caller `+0x4`,
  `[4..5]` to `+0x8`, `[6]` to `+0xa`, and `[7]` to `+0xb`; it still returns
  that raw status. Other errors produce no caller copy.
- Setter Infra wrapper `0x100018490` dispatches virtual `+0x4d8`. Core vtable
  `0x1003a10d8` entry `0x1003a15c0` resolves `0x1001203d4`. The terminal
  packs `[in+4..5, 0xaa, 0xaa, in+8..9, in+0xa, in+0xb]` and calls
  `runIOVarSet("forced_pm", ...)` `0x10017b6e6`, returning raw transport
  status without normalization.

The recovered paths do not establish a safe complete public allocation or a
Tahoe null-input contract. This batch does not probe or emulate either.

## Local boundary and non-claims

AirportItlwm has no `forced_pm` command owner, IOVAR transport, or response
path. Its former setter merely cached selected bytes and returned success; its
former getter manufactured a reply from that cache and also returned success.
The cache, reset sites, and fabricated reply are removed. The existing local
getter and setter null guards remain safety-only; non-null requests return
`kIOReturnUnsupported` before any pseudo-state mutation.

No private IOVAR, guessed carrier, synthetic reply, direct selector invocation,
or transport-status parity is introduced. Direct runtime coverage remains
blocked by the separate forced-off guest lifecycle state.
