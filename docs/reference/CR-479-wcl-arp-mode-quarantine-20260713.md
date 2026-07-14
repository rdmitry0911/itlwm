# CR-479 — WCL_ARP_MODE false-success quarantine

Date: 2026-07-13

## Scope

This correction covers only the public
`AirportItlwmSkywalkInterface::setWCL_ARP_MODE` slot. The former local
implementation reconstructed an `OFFLOAD_ARP` carrier from unrelated cached
IPv4 values, suppressed that separate quarantine's raw missing-owner status,
cached an inferred 0x14-byte carrier, and returned success. Scoped source
found this ARP-mode cache and flag only in their declarations, two reset pairs,
and this setter. It found no local ARP keepalive, GARP, WNM keepalive, or
matching transport/completion owner.

The direct local null guard remains `kIOReturnBadArgumentTahoe`
(`0xe00002bc`). A non-null request now returns `kIOReturnUnsupported` before
reading the carrier. The dead pseudo-layout, cache, flag, reset lines, and
synthetic direct-OFFLOAD_ARP bridge are removed. The separate direct
`setOFFLOAD_ARP` no-owner quarantine and paired IP-parameter quarantine remain
unchanged.

## Tahoe 25C56 reference recovery

The recovered DEXT is
`/System/Library/DriverExtensions/com.apple.DriverKit-AppleBCMWLAN.dext/com.apple.DriverKit-AppleBCMWLAN`,
SHA-256
`4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab`.

- `AppleBCMWLANInfraProtocol::setWCL_ARP_MODE` at `0x100018cec` tail-jumps to
  Core `0x1001e85e8`.
- Core returns `0xe00002bc` for null and defaults to that same status before
  rejecting an unsupported mode. Its dword at carrier `+0x8` selects either
  ARP keepalive or a KeepAliveOffload GARP path.
- The enabled byte at `+0x10` selects Core `programARPKeepAlive`
  `0x1000d9d6e` / `stopARPKeepAlive` `0x1000d9cba`, or KeepAliveOffload
  `programGARP` `0x10009cdea` / `stopGARP` `0x10009d1e6`.
- If both sideband bytes at `+0x4` and `+0x5` are nonzero, Core selects the
  WnmAdapter at `+0x15b0` and tail-calls `configureWNMKeepAlives`
  `0x1000ad5a0` with the two u16s at `+0` and `+0x2`.

Those branches are owner and transport lifecycle work. They do not prove a
complete public carrier layout, valid mode values, or a safe local
cache-and-direct-OFFLOAD_ARP substitute.

## Local boundary and non-claims

No guessed ARP-mode carrier, private keepalive/GARP/WNM request, private IOCTL,
direct firmware transport, or synthetic completion is introduced. No unrelated
IPv4 producer, direct OFFLOAD_ARP quarantine, or generic networking path is
changed.

No direct runtime invocation of this setter is claimed. Runtime validation is
ordinary credentialed association plus bounded traffic and focused fault
filters; radio OFF/ON remains excluded because the independent WCL lifecycle
failure is not an ARP-mode regression signal.
