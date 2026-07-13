# CR-479 — WCL_CONFIG_BGSCAN false-success quarantine

Date: 2026-07-13

## Scope

This correction covers only the public
`AirportItlwmSkywalkInterface::setWCL_CONFIG_BGSCAN` slot. The former local
implementation treated an invented eight-byte carrier as generic net80211
background-scan flags: it cached the carrier, cleared generic flags, optionally
called the generic background-scan start callback, and otherwise returned
success. Scoped source found that cache and flag only in their declarations,
two reset pairs, and this setter. It found no local BGScanAdapter, PFN/PNO/EPNO
owner, Commander transport, or matching completion/status consumer.

The direct local null guard remains `kIOReturnBadArgumentTahoe`
(`0xe00002bc`). A non-null request now returns `kIOReturnUnsupported` before
reading the carrier. The dead pseudo-layout, cache, flag, reset lines, and
setter-local generic scan mutations are removed. Generic net80211 scan fields
remain because they have independent live owners.

## Tahoe 25C56 reference recovery

The recovered DEXT is
`/System/Library/DriverExtensions/com.apple.DriverKit-AppleBCMWLAN.dext/com.apple.DriverKit-AppleBCMWLAN`,
SHA-256
`4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab`.

- `AppleBCMWLANInfraProtocol::setWCL_CONFIG_BGSCAN` at `0x10001928c`
  tail-jumps to Core `0x100142b8a`.
- Core returns `0xe00002bc` for null. For non-null it selects the
  BGScanAdapter from Core `+0x1578` and tail-jumps to its setter at
  `0x10000f852`.
- A nonzero carrier byte 0 calls `configurePFN(0)` at `0x10000f516`.
- A nonzero byte 1 calls `configPNO` at `0x10000fa18` with byte 2. That helper
  sends the four-byte `scan_nprobes` request through Commander `runIOVarSet`
  at `0x10017b6e6` before its further PNO/PFN handling.
- A nonzero byte 3 calls `configEPNO` at `0x10000fc20` with byte 4, retaining
  the adapter's EPNO/PFN lifecycle and result handling.

Those conditional branches are part of one adapter-owned PFN/PNO/EPNO and
Commander lifecycle. They do not prove a complete public carrier layout,
branch-valid input values, or a safe local generic-bgscan substitute.

## Local boundary and non-claims

No guessed BGSCAN carrier, `scan_nprobes` or PFN/PNO/EPNO IOVAR, private IOCTL,
direct firmware request, or synthetic completion is introduced. No unrelated
generic net80211 scan path is changed.

No direct runtime invocation of this setter is claimed. Runtime validation is
ordinary credentialed association plus bounded traffic and focused fault
filters; radio OFF/ON remains excluded because the independent WCL lifecycle
failure is not a BGSCAN regression signal.
