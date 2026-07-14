# CR-479 — FACETIME_WIFICALLING_PARAMS false-success quarantine

Date: 2026-07-14

## Scope

This correction covers only
`AirportItlwmSkywalkInterface::setFACETIME_WIFICALLING_PARAMS`. It preserves
the local null return and rejects every non-null request before a dead status
cache can advertise success for a missing WiFi-call policy operation.

## Tahoe 25C56 reference recovery

The live guest x86_64 DriverKit DEXT is
`/System/Library/DriverExtensions/com.apple.DriverKit-AppleBCMWLAN.dext/com.apple.DriverKit-AppleBCMWLAN`,
SHA-256
`4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab`.

- Infra `setFACETIME_WIFICALLING_PARAMS` at `0x100019094` loads Core and
  directly jumps to Core `0x100142714`.
- Core returns `0xe00002bc` for `NULL`. For non-null input it reads one status
  dword at carrier `+0x0`, calls `AppleBCMWLANCore::setWiFiCallPolicies` at
  `0x100139fbc`, then returns success without exposing the helper's internal
  outcome.
- The helper gates policy work on feature flag `0x2c`. When enabled, it takes
  `AppleBCMWLANPowerManager` from Core's `+0x48` state block at `+0x1590` and
  invokes `setWiFiCallPowerPolicy` at `0x100174780` with the status.

## Local boundary and non-claims

AirportItlwm has no WiFi-call policy helper, PowerManager, feature gate, or
equivalent power-policy backend. The former 4-byte status struct, cache field,
and reset lines were therefore a cache-only success substitute, not a partial
implementation of the Apple operation.

No status validation, feature-state behavior, PowerManager action, firmware
state, direct runtime setter invocation, or Apple valid-input return-code
parity is claimed. The retained local `NULL -> kIOReturnBadArgumentTahoe`
boundary matches the recovered Apple null return.

## Deterministic guard

`scripts/facetime_wificalling_quarantine_report.py --check` verifies the
reference anchors, matching null guard, fail-closed non-null setter, removal
of the synthetic status cache and 4-byte local carrier, absent scoped policy
backend, interface slot, and corrected historical documentation. Runtime
deployment remains independently blocked by the guest's forced-off Wi-Fi
lifecycle state.
