# CR-479 — WCL_CONFIG_BG_MOTIONPROFILE false-success quarantine

Date: 2026-07-13

## Scope

This correction covers only the public
`AirportItlwmSkywalkInterface::setWCL_CONFIG_BG_MOTIONPROFILE` slot. The
former local implementation checked one byte from a locally invented
0x40-byte layout, copied that layout to `cachedBgMotionProfile`, set a cache
flag, and returned success. Scoped source found the cache and flag only in
their declarations, two reset pairs, and this setter. It found no local
BGScanAdapter, motion-profile mapping, PNO/EPNO owner, Commander path, or
completion consumer.

The direct local null guard remains `kIOReturnBadArgumentTahoe`
(`0xe00002bc`). A non-null request now returns `kIOReturnUnsupported` before
reading the carrier. The dead cache, flag, reset lines, and unused pseudo-layout
are removed.

## Tahoe 25C56 reference recovery

The recovered DEXT is
`/System/Library/DriverExtensions/com.apple.DriverKit-AppleBCMWLAN.dext/com.apple.DriverKit-AppleBCMWLAN`,
SHA-256
`4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab`.

- `AppleBCMWLANInfraProtocol::setWCL_CONFIG_BG_MOTIONPROFILE` at
  `0x10001921c` tail-jumps to Core `0x100142b46`.
- Core returns `0xe00002bc` for null. For non-null it selects the
  BGScanAdapter from Core `+0x1578` and tail-jumps to its setter at
  `0x10000e856`.
- The adapter first calls `configureMotionProfileMapping` at `0x10000e96e`.
  That path consults ConfigManager and builds the `mpf_map` Commander IOVAR,
  submitted through `runIOVarSet` at `0x10017b6e6`.
- The adapter then calls `configureMotionProfilePNO` at `0x10000eb3a` and
  `configureMotionProfileEPNO` at `0x10000ec9a`. The recovered terminals
  submit `pfn_mpfset` through the same Commander transport and propagate their
  status.

The PNO helper has a `data + 1` condition, but that is one subordinate branch
inside a larger mapping, PNO, EPNO, capability, and transport lifecycle. It
does not prove a complete public carrier layout or make a standalone byte gate
safe to preserve.

## Local boundary and non-claims

No guessed PNO/EPNO carrier, `mpf_map`/`pfn_mpfset` IOVAR, private IOCTL,
direct firmware request, or synthetic completion is introduced. No unrelated
background-scan setter is changed.

No direct runtime invocation of this setter is claimed. Runtime validation is
ordinary credentialed association plus bounded traffic and focused fault
filters; radio OFF/ON remains excluded because the independent WCL lifecycle
failure is not a BG motion-profile regression signal.
