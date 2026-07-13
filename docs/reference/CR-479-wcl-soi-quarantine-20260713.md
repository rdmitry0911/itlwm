# CR-479: WCL SOI false-success quarantine

Date: 2026-07-13

## Reference contract

The Tahoe 25C56 `com.apple.DriverKit-AppleBCMWLAN` image has SHA-256
`4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab`.
Its Infra wrapper at `0x1000194e4` dispatches virtual slot `+0x780` to
Core.  The Core terminal is `AppleBCMWLANCore::setWCL_SOI_CONFIG` at
`0x100143182`.

The terminal passes the base carrier through the `PowerStateAdapter` at Core
`+0x8c88` to `configureBeaconSOI` at `0x100041352`; it then passes carrier
`+0x1c` to `configureDataSOI` at `0x100041a0c` and returns success after the
configuration calls.  The observed beacon path reads multiple caller words,
chooses a listen-interval path, and reaches commander `runIOVarSet` for
`bcn_li_bcn`.  The observed data path can reach `pm2_sleep_ret` through its
DFRTS/FRTS configuration branch.  These are real adapter/commander effects,
not an inert Core cache.

The terminal forwards its carrier directly to those configurators; this
recovery makes no Apple null-input status claim.  Nor do the observed offsets
or the local former `0x40` cache establish a complete public carrier allocation.

## Local divergence

`AirportItlwmSkywalkInterface::setWCL_SOI_CONFIG` previously accepted a
non-null pointer, copied a local `0x40`-byte cache, set an unconsumed cache
flag, and returned success.  The Intel port has no matching sleep adapter,
SOI configurators, or recovered `bcn_li_bcn`/`pm2_sleep_ret` backend.

## Local correction

The existing local null guard remains.  A non-null request now returns
`kIOReturnUnsupported` before cache or backend mutation, and the two dead
cache members plus their initialization/reset sites are removed.  This is a
local no-backend quarantine, not Apple valid-input return-code parity.

## Deterministic guard

`scripts/wcl_soi_quarantine_report.py --check` requires the 25C56
wrapper/Core/adapter/IOVAR anchors, preserved local null guard, a non-null
unsupported result, removal of pseudo-state, absence of the local backend,
and the corrected signal-chain wording.
