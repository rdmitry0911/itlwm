# CR-479 — POWER_BUDGET false-success quarantine

Date: 2026-07-14

## Scope

This correction covers only
`AirportItlwmSkywalkInterface::setPOWER_BUDGET`. It preserves the port's
existing null guard and feature-bit gate, corrects the recovered valid budget
range to `1..100`, and returns `kIOReturnUnsupported` for a valid non-null
request before any local cache update.

The local null guard remains a safety boundary. The recovered Core body has no
explicit null check after its feature gate and reads carrier `+0x4` when the
feature is enabled, so this change does not claim exact enabled-Core null
return-code parity.

## Tahoe 25C56 reference recovery

The live guest x86_64 DriverKit DEXT is
`/System/Library/DriverExtensions/com.apple.DriverKit-AppleBCMWLAN.dext/com.apple.DriverKit-AppleBCMWLAN`,
SHA-256
`4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab`.

- Infra `setPOWER_BUDGET` at `0x1000187f8` reaches Core through virtual
  `+0x4f8`. `__ZTV16AppleBCMWLANCore` begins at `0x1003a10d8`; the object
  vptr address point is `+0x10`, so cell `0x1003a15e0` resolves to
  `AppleBCMWLANCore::setPOWER_BUDGET` at `0x100120790`.
- Core calls `featureFlagIsBitSet(0x3b)` at `0x1000c846e`. A disabled feature
  returns `0xe00002bc`; otherwise Core reads only the public budget dword at
  carrier `+0x4`.
- The unsigned range sequence accepts exactly `1..100`. `0` and `>=101` take
  the `0xe00002bc` failure path. No complete setter carrier ABI beyond the
  observed budget dword is claimed.
- A valid request allocates a 12-byte payload, writes the budget at payload
  `+0x8`, and calls Commander `runIOVarSet("tvpm")` at `0x10017b6e6`.
  Core writes its state at `*(Core + 0x48) + 0x4` only when transport status is
  zero or special status `0xe3ff8117`; it returns the raw transport status.

## Local boundary and non-claims

The Intel port has no `tvpm` owner, Commander `runIOVarSet` path, matching
payload builder, or transport status lifecycle. Before this correction it
rejected the Apple-valid range, accepted the invalid complement, updated
`cachedPowerBudget`, and returned success. That was both an inverted public
validation defect and a false acknowledgement of a firmware action.

The getter and its default-only cache remain outside this narrow setter change.
No guessed payload, IOVAR, callback, firmware state, direct runtime setter
call, or synthetic completion is introduced. This does not claim Apple
valid-input return-code parity or alter association, scanning, roaming, radio
power, or normal public rejoin paths.

## Deterministic guard

`scripts/power_budget_quarantine_report.py --check` verifies the corrected
range, the retained local safety gates, absence of the setter cache write and
scoped backend, reference anchors, and corrected historical documentation.
Runtime deployment remains independently blocked by the guest's forced-off
Wi-Fi lifecycle state.
