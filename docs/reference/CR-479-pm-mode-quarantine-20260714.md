# CR-479 — PM_MODE false-success quarantine

Date: 2026-07-14

## Scope

This correction covers only `AirportItlwmSkywalkInterface::setPM_MODE`. It
preserves the port's local null-safety return and makes every non-null request
return `kIOReturnUnsupported` before it can mutate local state or trigger the
unrelated POWERSAVE path.

The local null guard is an intentional safety divergence. Recovered Apple Core
directly reads carrier `+0x4` without a null guard, so this correction does not
claim Apple null return-code parity. It does not claim Apple valid-input
return-code parity.

## Tahoe 25C56 reference recovery

The live guest x86_64 DriverKit DEXT is
`/System/Library/DriverExtensions/com.apple.DriverKit-AppleBCMWLAN.dext/com.apple.DriverKit-AppleBCMWLAN`,
SHA-256
`4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab`.

- Infra `setPM_MODE` at `0x100018960` reaches Core through virtual `+0x700`.
  `__ZTV16AppleBCMWLANCore` begins at `0x1003a10d8`; the object vptr address
  point is `+0x10`, so cell `0x1003a17e8` resolves to
  `AppleBCMWLANCore::setPM_MODE` at `0x100119c4a`.
- Core takes only the mode dword at carrier `+0x4` and tail-calls
  `AppleBCMWLANNetAdapter::configurePM` at `0x100015e02`. It has no Core-side
  null guard or local cache commit.
- `configurePM` maps zero to request `0` and every nonzero mode to request
  `2`, packages that request in four bytes, and calls Commander
  `sendIOCtlSet` at `0x10017b80c` with IOC `0x56`. It installs
  `setPowerManagementAsyncCallback` at `0x100015fd0` and returns the raw
  enqueue/transport status.

## Local boundary and non-claims

The Intel port has no NetAdapter `configurePM` owner, asynchronous callback,
or IOC `0x56` transport. Before this correction it saved a dead `cachedPmMode`,
called local `setPOWERSAVE`, and reported its result as PM_MODE success. That
was not the Apple asynchronous command lifecycle and could alter normal local
powersave state without applying the requested Apple operation.

The dead PM cache, both reset sites, and synthetic POWERSAVE call are removed.
No guessed IOC payload, command, callback, firmware state, direct runtime
setter call, or synthetic completion is introduced. This does not alter
association, scanning, roaming, radio power, or normal public rejoin paths.

## Deterministic guard

`scripts/pm_mode_quarantine_report.py --check` verifies the reference anchors,
retained local null safety, fail-closed non-null setter, removal of the dead
cache and synthetic POWERSAVE action, absence of scoped PM transport owners,
interface slot, and corrected historical documentation. Runtime deployment
remains independently blocked by the guest's forced-off Wi-Fi lifecycle state.
