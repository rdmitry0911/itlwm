# CR-479 — TXPOWER/RATE false-success quarantine

Date: 2026-07-14

## Scope

This correction covers only
`AirportItlwmSkywalkInterface::setTXPOWER` and `setRATE`. It preserves the
port's local null-safety return and makes every non-null request return
`kIOReturnUnsupported` before it can mutate local state.

The local null guard is an intentional safety divergence. Neither recovered
Apple Core body has a null guard, so this correction does not claim Apple null
return-code parity or input-validation parity. It does not claim Apple
valid-input return-code parity.

## Tahoe 25C56 reference recovery

The live guest x86_64 DriverKit DEXT is
`/System/Library/DriverExtensions/com.apple.DriverKit-AppleBCMWLAN.dext/com.apple.DriverKit-AppleBCMWLAN`,
SHA-256
`4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab`.

- Infra `setTXPOWER` at `0x1000180b4` reaches Core through virtual `+0x4e8`.
  `__ZTV16AppleBCMWLANCore` begins at `0x1003a10d8`; the object vptr address
  point is `+0x10`, so cell `0x1003a15d0` resolves to
  `AppleBCMWLANCore::setTXPOWER` at `0x10012099c`.
- The TXPOWER Core body immediately reads the unit at carrier `+0x4` and
  txpower value at `+0x8`; it has no null guard and does not inspect version.
  Unit `1` uses the mW conversion table, other units shift the value by two;
  both forms set bit `0x80000000` in a four-byte `qtxpower` payload. It calls
  `runIOVarSet("qtxpower")` at `0x10017b6e6` and returns the raw SET status.
- Infra `setRATE` at `0x100018100` reaches Core through virtual `+0x500`.
  The same vptr address point makes cell `0x1003a15e8` resolve to
  `AppleBCMWLANCore::setRATE` at `0x100120cba`.
- The RATE Core body also has no null guard. It performs a four-byte
  `bg_rate` GET, overwrites the carrier with request `rate[0]` at `+0x8`, SETs
  `bg_rate`, then performs a final GET and returns that final GET's raw status.
  It does not inspect `version` or `num_radios`; earlier GET/SET failures are
  not the final returned status.

## Local boundary and non-claims

The Intel port has no `qtxpower` or `bg_rate` Commander transaction owner.
Before this correction, `setTXPOWER` encoded a request and wrote
`sc_last_qtxpower_raw`, while `setRATE` wrote two otherwise dead cache fields;
both then returned success. Those updates did not perform either Apple firmware
transition and could corrupt the separate BA-backed getter observation.

The TXPOWER encoder/setter helper and dead RATE fields/resets are removed.
The real `sc_last_qtxpower_raw` producers in the iwm/iwx BA-notification paths
and their getter consumers remain untouched. No guessed IOVAR, carrier,
callback, firmware state, direct runtime setter call, or synthetic completion
is introduced. The change does not alter association, scanning, roaming,
radio power, or normal public rejoin paths.

## Deterministic guard

`scripts/txpower_rate_quarantine_report.py --check` verifies reference anchors,
the retained local null guard, fail-closed non-null setters, removal of only the
synthetic cache mutations, retained BA producers, absence of scoped transport
owners, interface slots, and corrected historical documentation. Runtime
deployment remains independently blocked by the guest's forced-off Wi-Fi
lifecycle state.
