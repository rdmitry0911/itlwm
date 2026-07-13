# CR-479: WCL WNM Offload false-success quarantine

Date: 2026-07-13

## Reference contract

The Tahoe 25C56 `com.apple.DriverKit-AppleBCMWLAN` image has SHA-256
`4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab`.
Infra wrapper `0x100019af6` tail-jumps to
`AppleBCMWLANCore::setWCL_WNM_OFFLOAD` at `0x1001429d2`.  Core returns
`0xe00002bc` for a null pointer; otherwise it selects the WnmAdapter at Core
`+0x15b0` and tail-jumps to `configureWnmOffloadFeatures` at `0x1000a99e0`.

The adapter reads control bits at carrier `+0x00` and `+0x04` and reaches
`unconfigureOffloads` `0x1000a9c80` and `configureOffloads` `0x1000a9f60`.
Its recovered descendants include `configureDMS` `0x1000ae2e0`, which sends
`tclas_add` and `wnm_dms_set`, and `configureWNMDMSDependency`
`0x1000ae160`, which sends `wnm_dms_dependency`, through
`AppleBCMWLANCommander::runIOVarSet`.  This is real adapter/commander work,
not an inert Core cache.

The observed reads do not establish a complete public carrier allocation.
The local correction below also does not claim Apple null-input status,
valid-input return, feature-gate, or transport-status parity.

## Local divergence

`AirportItlwmSkywalkInterface::setWCL_WNM_OFFLOAD` previously accepted a
non-null pointer, copied its local `0x30` byte storage and set
`hasCachedWnmOffload`, then returned success.  Scoped local source has no
matching WNM-offload configurator or `tclas_add`/`wnm_dms_set`/
`wnm_dms_dependency` path.

## Local correction

The existing local null guard remains.  A non-null request now returns
`kIOReturnUnsupported` before cache or configurator mutation, and the dead
cache member, flag, and their initialization/reset sites are removed.  This is
a local no-backend quarantine, not Apple valid-input return-code parity.

## Deterministic guard

`scripts/wcl_wnm_offload_quarantine_report.py --check` requires the 25C56
wrapper/Core/WnmAdapter/IOVAR anchors, preserved local null guard, a non-null
unsupported result, removal of pseudo-state, absence of matching local
WNM-offload anchor literals, and corrected signal-chain wording.
