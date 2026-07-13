# CR-479 — EAP_FILTER_CONFIG false-success quarantine

Date: 2026-07-13

## Scope

This correction covers only the public
`AirportItlwmSkywalkInterface::setEAP_FILTER_CONFIG` slot.  The former local
implementation accepted a non-null `apple80211_eap_filter_config`, copied its
first dword into `cachedEapFilterConfig`, and returned success.  Scoped source
found that cache only in its declaration, two initialization resets, and that
setter.  It found no local EAPOL packet-filter owner, filter lifecycle,
Commander request path, or completion consumer.

The existing local null guard remains `kIOReturnBadArgumentTahoe`
(`0xe00002bc`).  A non-null request now returns `kIOReturnUnsupported` before
mutation, and only the dead cache and its two reset sites are removed.

## Tahoe 25C56 reference recovery

The recovered DEXT is
`/System/Library/DriverExtensions/com.apple.DriverKit-AppleBCMWLAN.dext/com.apple.DriverKit-AppleBCMWLAN`,
SHA-256
`4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab`.

- `AppleBCMWLANInfraProtocol::setEAP_FILTER_CONFIG` at `0x1000191ac` directly
  dispatches to `AppleBCMWLANCore::setEAP_FILTER_CONFIG` at `0x10014294e`.
- Core returns `0xe00002bc` for null.  For a non-null carrier it reads the
  first dword only and writes Core `+0x4d48`, then returns success.
- The deferred packet-filter owner is `configurePktFilters` at `0x10012f310`.
  It calls `deleteEapolFilter` at callsite `0x10012f780` and
  `configureEapolFilter` at callsite `0x10012f788`.
- `configureEapolFilter` at `0x100135022` reads Core `+0x4d48`.  Its enabled
  path reaches `pkt_filter_add` and Commander `runIOVarSet` at `0x10017b6e6`;
  the separate delete owner is `deleteEapolFilter` at `0x100135d70`.

This establishes a real deferred packet-filter lifecycle.  It does not justify
recreating that firmware path from a cache write.  In particular, the raw
recovery demonstrates only the first effective dword: it does not provide a
complete public-carrier allocation, valid-input return-code parity, or a
packet-filter payload ABI.

## Local boundary and non-claims

No guessed `pkt_filter_*` IOVAR, private IOCTL, direct firmware request, or
synthetic completion is introduced.  Generic local EAPOL RX/TX handling is a
data path rather than the Tahoe EAPOL firmware-filter owner and is deliberately
unchanged.

No direct runtime invocation of this setter is claimed.  Runtime validation is
ordinary credentialed association plus bounded traffic and focused fault
filters; radio OFF/ON remains excluded because the independent WCL lifecycle
failure is not an EAP-filter regression signal.
