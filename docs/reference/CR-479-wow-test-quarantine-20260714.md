# CR-479 — WOW_TEST false-success quarantine

Date: 2026-07-14

## Scope

This correction covers only `AirportItlwmSkywalkInterface::setWOW_TEST`. It
preserves the port's null and invalid-mode return `0xe00002c2` and its observed
valid mode range `1..600`. A valid non-null request now returns
`kIOReturnUnsupported` without caching a mode or enabling local WoW state.

The recovered Core body directly reads carrier `+0x4`; neither it nor the Infra
wrapper establishes a safe null return. Keeping the existing local null guard
is a safety boundary, not a claim of exact Apple null parity.

## Tahoe 25C56 reference recovery

The live guest x86_64 DriverKit DEXT is
`/System/Library/DriverExtensions/com.apple.DriverKit-AppleBCMWLAN.dext/com.apple.DriverKit-AppleBCMWLAN`,
SHA-256
`4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab`.

- Infra `setWOW_TEST` at `0x10001827c` reaches Core through virtual `+0x518`.
  `__ZTV16AppleBCMWLANCore` begins at `0x1003a10d8`; with its `+0x10` object
  vptr address point, cell `0x1003a1600` resolves to
  `AppleBCMWLANCore::setWOW_TEST` at `0x100120ed2`.
- Core accepts only mode `1..600` from carrier `+0x4`; invalid modes return
  `0xe00002c2`. A valid mode calls `configureWoWTestModeEntry` at
  `0x100120f20` up to five times and returns the successful result or its final
  backend failure.
- The helper adds event bit `0x4c` at `0x1001e25ae`, calls
  `writeEventBitField` at `0x10011ef7e`, and sends the four-byte mode through
  Commander `runIOVarSet("wake_event")` at `0x10017b6e6`.
  It sets Core state `+0x2508` only after a successful transport result.

Those owner transitions and result semantics are real work, not permission to
emit a local successful acknowledgement or fabricate a complete carrier ABI.

## Local boundary and non-claims

The Intel port has no `wake_event` owner, event-bit mutation,
`configureWoWTestModeEntry`, matching Commander transport, or completion
lifecycle. The former implementation returned success on its first synthetic
loop iteration, copied the mode to a write-only cache, and called
`setWoWEnabled(true)` without a completed wake-test operation.

The cache fields and both initialization resets are removed. No guessed
firmware request, event bit, callback, direct runtime setter call, or synthetic
completion is introduced. This is the isolated `APPLE80211_IOC_WOW_TEST`
surface; it does not alter association, scanning, roaming, radio power, or
normal public rejoin paths. It does not claim Apple valid-input return-code
parity.

## Deterministic guard

`scripts/wow_test_quarantine_report.py --check` verifies the retained local
null/range boundary, removed fake state, absence of the scoped backend, exact
reference anchors, and corrected historical documentation. Runtime deployment
remains independently gated by the guest's forced-off Wi-Fi lifecycle state.
