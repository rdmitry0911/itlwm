# CR-479: WCL WNM OPS false-success quarantine

Date: 2026-07-13

## Reference contract

The Tahoe 25C56 `com.apple.DriverKit-AppleBCMWLAN` image has SHA-256
`4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab`.
Infra wrapper `0x100019abe` tail-jumps to
`AppleBCMWLANCore::setWCL_WNM_OPS` at `0x1001429b0`. Core returns
`0xe00002bc` for a null pointer; otherwise it selects the WnmAdapter at Core
`+0x15b0` and tail-jumps to `configureWnmFeatures` at `0x1000a7ff0`.

The adapter branches into `configureEnterpriseFeatures` `0x1000a8280`,
`configureProductInfoReporting` `0x1000a8480`, and
`configureBeaconReporting` `0x1000a9180`. Its enterprise path reaches
`configureWNM` `0x1000aa9e0`, which checks WNM support and calls
`AppleBCMWLANCommander::runIOVarSet` `0x10017b6e6` for the `wnm` IOVAR. The
recovered command path constructs a four-byte transmit payload, calls
`issueCommand`, and exposes error handling for the command result. This is
real adapter/commander work, not an inert Core cache.

The observed branches do not establish a complete public carrier allocation.
The local correction below also does not claim Apple null-input status,
valid-input return, feature-gate, or transport-status parity.

## Local divergence

`AirportItlwmSkywalkInterface::setWCL_WNM_OPS` previously accepted a non-null
pointer, copied a local `0x338` byte cache, set `hasCachedWnmConfig`, then
returned success. The cache and flag had no consumer beyond their two reset
sites. Scoped local source has no matching WNM configuration adapter/commander
implementation path; this is not a claim that every local occurrence of the
generic `wnm` term is absent.

## Local correction

The existing local null guard remains. A non-null request now returns
`kIOReturnUnsupported` before cache or configuration mutation, and the dead
cache member, flag, and their initialization/reset sites are removed. This is
a local no-backend quarantine, not Apple valid-input return-code parity.

## Deterministic guard

`scripts/wcl_wnm_ops_quarantine_report.py --check` requires the 25C56
wrapper/Core/WnmAdapter/Commander anchors, preserved local null guard, a
non-null unsupported result, removal of pseudo-state, absence of matching
local WNM configuration anchor literals, and corrected signal-chain wording.
