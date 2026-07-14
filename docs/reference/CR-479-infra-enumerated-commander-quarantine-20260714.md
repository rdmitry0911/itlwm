# CR-479 — INFRA_ENUMERATED Commander quarantine

Date: 2026-07-14

## Scope

This correction covers only `AirportItlwmSkywalkInterface::setINFRA_ENUMERATED`.
The prior local setter marked an unread `cachedInfraEnumerated` flag and
reported success for every non-null opaque carrier. The flag had no consumer,
and Tahoe’s real non-null path performs Commander-owned timeout work.

No direct setter invocation, private ioctl, Commander timeout change, radio
transition, deployment, association, or traffic is introduced.

## Tahoe 25C56 reference recovery

The live guest x86_64 DriverKit DEXT is
`/System/Library/DriverExtensions/com.apple.DriverKit-AppleBCMWLAN.dext/com.apple.DriverKit-AppleBCMWLAN`,
SHA-256
`4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab`.

- Infra `setINFRA_ENUMERATED` at `0x10001936c` tail-calls
  `AppleBCMWLANCore::setINFRA_ENUMERATED` at `0x100142bf0`.
- `NULL` returns `0xe00002bc`. For non-null input Core reads exactly carrier
  byte `+0`; a zero byte returns success without the terminal call.
- A nonzero byte follows `(Core + 0x48) + 0x1520` to
  `AppleBCMWLANCommander::deviceBootStationaryNotification()` at
  `0x100181e04`. The terminal is void: it first follows Commander `+0x40` to
  internal state, then writes command timeout `0x61a8` at state `+0x10c`, with
  an optional `wlan.factory` adjustment of `0x1388 + state[+0xa8]`.

## Local boundary and non-claims

`apple80211_infra_enumerated` is only a forward declaration in the local
headers. There is no `APPLE80211_IOC_INFRA_ENUMERATED` dispatch route and no
request-length validation, so the local code cannot safely reproduce the
byte-zero branch by dereferencing opaque input. AirportItlwm also lacks the
Commander stationary-boot owner and command-timeout backend.

The local setter retains its existing NULL safety boundary
`kIOReturnBadArgumentTahoe`, then returns `kIOReturnUnsupported` for every
non-null carrier before reading it. The dead `cachedInfraEnumerated` state and
both reset paths are removed. This is a local no-owner quarantine; it does not
claim Apple byte-zero or valid-input return-code parity.

The virtual setter remains for ABI completeness. No getter, BSD dispatcher,
or runtime setter coverage is claimed.

## Deterministic guard

`scripts/infra_enumerated_commander_quarantine_report.py --check` verifies the
canonical reference anchors, retained NULL safety, absence of an opaque-carrier
read and synthetic cache, retained virtual slot, forward-only local ABI,
absent Commander backend, and corrected historical documentation. Runtime
deployment remains independently blocked by the guest's forced-off Wi-Fi
lifecycle state.
