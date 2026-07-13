# CR-479 — WCL_CONFIG_BG_NETWORK false-success quarantine

Date: 2026-07-13

## Scope

This correction covers only the public
`AirportItlwmSkywalkInterface::setWCL_CONFIG_BG_NETWORK` slot. The former
local implementation copied a locally invented 0x12c0-byte layout to
`cachedBgNetwork`, set a cache flag, reset two scan-result iterator fields, and
returned success. Scoped source found the cache and flag only in their
declarations, two reset pairs, and this setter. It found no local
BGScanAdapter, PFN owner, matching Commander path, or completion consumer.

The direct local null guard remains `kIOReturnBadArgumentTahoe`
(`0xe00002bc`). A non-null request now returns `kIOReturnUnsupported` before
reading the carrier. The dead pseudo-layout, cache, flag, reset lines, and
setter-local iterator mutations are removed. The live scan-result fields and
their other owners are not changed.

## Tahoe 25C56 reference recovery

The recovered DEXT is
`/System/Library/DriverExtensions/com.apple.DriverKit-AppleBCMWLAN.dext/com.apple.DriverKit-AppleBCMWLAN`,
SHA-256
`4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab`.

- `AppleBCMWLANInfraProtocol::setWCL_CONFIG_BG_NETWORK` at `0x100019254`
  tail-jumps to Core `0x100142b68`.
- Core returns `0xe00002bc` for null. For non-null it selects the
  BGScanAdapter from Core `+0x1578` and tail-jumps to its setter at
  `0x10000ee46`.
- The adapter clears state, calls `configurePFN(0)` at `0x10000f516`, then
  sends `pfnclear` through Commander `runIOVarSet` at `0x10017b6e6`. It also
  calls `configurePFNSuspend(0)`, retains adapter-owned state, and submits
  `pfn_set`, `pfn_add`, and `pfn_add_bssid` through the same Commander path,
  propagating the resulting status.

The recovered 0x12c0 adapter copy is only one part of that PFN lifecycle. It
does not prove a complete public carrier layout or make a standalone local
copy-and-success substitute safe.

## Local boundary and non-claims

No guessed PFN carrier, `pfnclear`/`pfn_set`/`pfn_add`/`pfn_add_bssid` IOVAR,
private IOCTL, direct firmware request, or synthetic completion is introduced.
No unrelated background-scan setter is changed.

No direct runtime invocation of this setter is claimed. Runtime validation is
ordinary credentialed association plus bounded traffic and focused fault
filters; radio OFF/ON remains excluded because the independent WCL lifecycle
failure is not a BG network regression signal.
