# CR-479 — WCL_CONFIG_BG_PARAMS false-success quarantine

Date: 2026-07-13

## Scope

This correction covers only the public
`AirportItlwmSkywalkInterface::setWCL_CONFIG_BG_PARAMS` slot. The former local
implementation copied a locally invented 0x20-byte layout to `cachedBgParams`,
set a cache flag, and returned success. Scoped source found the cache and flag
only in their declarations, two reset pairs, and this setter. It found no
local BGScanAdapter, dynamic-frequency or unassociated-scan-time owner,
matching Commander path, or completion consumer.

The direct local null guard remains `kIOReturnBadArgumentTahoe`
(`0xe00002bc`). A non-null request now returns `kIOReturnUnsupported` before
reading the carrier. The dead pseudo-layout, cache, flag, and reset lines are
removed.

## Tahoe 25C56 reference recovery

The recovered DEXT is
`/System/Library/DriverExtensions/com.apple.DriverKit-AppleBCMWLAN.dext/com.apple.DriverKit-AppleBCMWLAN`,
SHA-256
`4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab`.

- `AppleBCMWLANInfraProtocol::setWCL_CONFIG_BG_PARAMS` at `0x1000192c4`
  tail-jumps to Core `0x100142bac`.
- Core returns `0xe00002bc` for null. For non-null it selects the
  BGScanAdapter from Core `+0x1578` and tail-jumps to its setter at
  `0x1000102a2`.
- When the first sub-command is enabled, the adapter calls
  `configureDynamicScanFreq` at `0x1000103ec`. That helper builds an
  0x18-byte `pfn_override` request and submits it through Commander
  `sendIOVarSet` with the recovered async callback.
- When the second sub-command is enabled, the adapter calls
  `configureUnAssociatedScanTime` at `0x100010504`. That helper sends a
  four-byte `scan_unassoc_time` request through Commander `runIOVarSet` at
  `0x10017b6e6` and retains the relevant result path.

Those conditional branches are part of a background-scan owner and transport
lifecycle. They do not prove a complete public carrier layout, valid
sub-command values, or a safe local cache-and-success substitute.

## Local boundary and non-claims

No guessed BG params carrier, `pfn_override`/`scan_unassoc_time` IOVAR,
private IOCTL, direct firmware request, or synthetic async completion is
introduced. No unrelated background-scan setter is changed.

No direct runtime invocation of this setter is claimed. Runtime validation is
ordinary credentialed association plus bounded traffic and focused fault
filters; radio OFF/ON remains excluded because the independent WCL lifecycle
failure is not a BG params regression signal.
