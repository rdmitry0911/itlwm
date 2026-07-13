# CR-479: OS eligibility false-success quarantine

Date: 2026-07-13

## Reference contract

The Tahoe 25C56 `com.apple.DriverKit-AppleBCMWLAN` image has SHA-256
`4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab`.
Its Infra wrapper at `0x100019830` dispatches virtual slot `+0x7d0` to
Core.  The Core terminal is `AppleBCMWLANCore::setOS_ELIGIBILITY` at
`0x100143ed6`.

The terminal compares carrier bit 0 with Core `+0x8cec`.  When that bit
changes and the commander is not sleeping, it calls the NetAdapter at Core
`+0x15e0`, `configureAggressiveEDCA` at `0x100014cc8`, with the new boolean;
it then stores the full carrier word at `+0x8cec` and returns success.  The
recovered NetAdapter path gates its policy by platform/country state, sends
the four-byte `wme_ac_sta` commander IOVAR, and updates the short retry limit.
That is real adapter/commander work, not an inert Core cache.

The terminal reads its carrier before any visible null branch, so this
recovery makes no Apple null-input status claim.  Nor do the observed reads
and dword store establish a complete public carrier allocation.

## Local divergence

`AirportItlwmSkywalkInterface::setOS_ELIGIBILITY` previously accepted a
non-null pointer, copied a dword into unconsumed local `cachedOsEligibility`,
and returned success.  The Intel port has no matching aggressive-EDCA
configuration, `wme_ac_sta` commander path, or retry-limit backend.

## Local correction

The existing local null guard remains.  A non-null request now returns
`kIOReturnUnsupported` before cache or backend mutation, and the dead cache
member plus its initialization/reset sites are removed.  This is a local
no-backend quarantine, not Apple valid-input return-code parity.

## Deterministic guard

`scripts/os_eligibility_quarantine_report.py --check` requires the 25C56
wrapper/Core/NetAdapter/IOVAR anchors, preserved local null guard, a non-null
unsupported result, removal of pseudo-state, absence of the local backend,
and the corrected signal-chain wording.
