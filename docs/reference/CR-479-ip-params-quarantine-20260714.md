# CR-479 — IPv4 / IPv6 parameter false-success quarantine

Date: 2026-07-14

## Scope

This correction covers only
`AirportItlwmSkywalkInterface::setIPV4_PARAMS` and
`AirportItlwmSkywalkInterface::setIPV6_PARAMS`. It removes dead local caches
that returned success despite having no Infra, notification, Proximity, or
keepalive lifecycle owner.

## Tahoe 25C56 reference recovery

The live guest x86_64 DriverKit DEXT is
`/System/Library/DriverExtensions/com.apple.DriverKit-AppleBCMWLAN.dext/com.apple.DriverKit-AppleBCMWLAN`,
SHA-256
`4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab`.

- Infra `setIPV4_PARAMS` at `0x1000190cc` tail-jumps to Core
  `0x100142776`. `NULL` returns `0xe00002bc`. A non-null carrier optionally
  invokes `IO80211InfraInterface::setIPv4Params`, without exposing that call's
  status, then stores raw offsets `+0`, `+4`, `+8`, and word `+0xc` in Core
  state. It calls `handleIPv4AddressNotificationGated`, optionally calls
  Proximity `setIPv4Addr`, and calls `handleKeepaliveDataNotificationGated`
  when address and mask are non-zero before returning success.
- Infra `setIPV6_PARAMS` at `0x100019334` tail-jumps to Core
  `0x100142a50`. A non-null carrier optionally invokes
  `IO80211InfraInterface::setIPv6Params`, clamps dword `+0` to ten, reads the
  first dword of each `+4 + 0x10*i` entry, clears a `0xa0`-byte companion
  state region, records the count, seeds `fe80` state, schedules
  `handleIPv6AddressNotificationGated`, and returns success. The raw Core NULL
  branch reaches an immediate carrier dereference, so it does not establish a
  safe Apple NULL return contract.

## Local boundary and non-claims

AirportItlwm has no `setIPv4Params` / `setIPv6Params` owner, IP-address
notification, Proximity update, or keepalive notification backend. The former
IPv4 and IPv6 carrier layouts, cache fields, and reset paths were unread
cache-only acknowledgements, not partial implementations of Apple's lifecycle.

IPV4_PARAMS preserves `NULL -> kIOReturnBadArgumentTahoe` (`0xe00002bc`),
which matches the recovered Apple null return. IPV6_PARAMS keeps the same local
NULL rejection solely as a safety boundary. Both non-null setters return
`kIOReturnUnsupported` before carrier access or mutation.

No IP address configuration, interface notification, Proximity behavior,
keepalive state, direct runtime setter invocation, firmware operation, IPv6
NULL parity, or Apple valid-input return-code parity is claimed.

## Deterministic guard

`scripts/ip_params_quarantine_report.py --check` verifies the reference
anchors, both fail-closed setters, removal of the synthetic layouts/state,
retained interface slots, absent scoped lifecycle tokens, and corrected
historical documentation. Runtime deployment remains independently blocked by
the guest's forced-off Wi-Fi lifecycle state.
