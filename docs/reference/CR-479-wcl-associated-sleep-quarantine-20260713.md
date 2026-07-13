# CR-479: WCL associated-sleep false-success quarantine

Date: 2026-07-13

## Reference contract

The Tahoe 25C56 `com.apple.DriverKit-AppleBCMWLAN` image has SHA-256
`4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab`.
Its Infra wrapper at `0x100019498` dispatches virtual slot `+0x778` to
Core.  The Core terminal is
`AppleBCMWLANCore::setWCL_ASSOCIATED_SLEEP` at `0x100142fce`.

The terminal reads the carrier immediately, updates Core state, copies the
carrier byte at `+0x36` into Core `+0x4d48`, and uses the
`PowerStateAdapter` at Core `+0x8c88` for four real operations:

- `configureBeaconSOI` at `0x100041352` with carrier `+0x4`;
- `configureDataSOI` at `0x100041a0c` with carrier `+0x20`;
- `configureExcessPMAlert` at `0x100041ce6` with carrier `+0x54`;
- `configureRoamScanForAssociatedSleep` at `0x100041eb6` with carrier
  `+0x48` and the derived Core flag.

The Core also derives a flag from the first carrier word and updates Core
`+0x227c` and `+0x1a48` before those calls.  It returns success after that
backend work.  Because the recovered terminal dereferences its carrier before
any visible null branch, this recovery makes no Apple null-input status claim;
nor do these observed offsets establish a complete public carrier allocation.

## Local divergence

`AirportItlwmSkywalkInterface::setWCL_ASSOCIATED_SLEEP` previously accepted a
non-null pointer, copied a local `0x58`-byte cache, set an unconsumed cache
flag, and returned success.  The Intel port has no `PowerStateAdapter` or any
of the recovered configuration operations.  The local cache layout is not
evidence of a complete Apple carrier ABI and cannot replace the reference
backend effects.

## Local correction

The existing local null guard remains.  A non-null request now returns
`kIOReturnUnsupported` before cache or backend mutation, and the two dead
cache members plus their initialization/reset sites are removed.  This is a
local no-backend quarantine, not Apple valid-input return-code parity.

## Deterministic guard

`scripts/wcl_associated_sleep_quarantine_report.py --check` requires the
25C56 wrapper/Core/adapter anchors, the preserved local null guard, a
non-null unsupported result, removal of pseudo-state, absence of the four
local adapter operations, and the corrected signal-chain wording.
