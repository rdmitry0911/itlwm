# CR-479 — LQM_CONFIG owner-boundary quarantine

Date: 2026-07-14

## Scope

This correction covers only public `getLQM_CONFIG` and `setLQM_CONFIG` on
`AirportItlwmSkywalkInterface`. It removes the synthetic cached configuration
and the public setter's ability to retune the local statistics timer. It does
not remove the timer, its 5000 ms default, association lifecycle, or its real
message `0x27` telemetry producer.

## Tahoe 25C56 reference recovery

The live guest x86_64 DriverKit DEXT is
`/System/Library/DriverExtensions/com.apple.DriverKit-AppleBCMWLAN.dext/com.apple.DriverKit-AppleBCMWLAN`,
SHA-256
`4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab`.

- Infra `setLQM_CONFIG` at `0x100018844` dispatches through Core virtual
  `+0x710` to `AppleBCMWLANCore::setLQM_CONFIG` at `0x100119d98`.
- `NULL` returns raw `0x16`. For non-null input Core first checks opaque state
  `+0x43f` (`0x2d` when set), then `featureFlagIsBitSet(0x27)`. A disabled
  feature returns `0xe00002bc` before the carrier is read.
- The enabled path requires all interval dwords at `+0x4`, `+0x8`, and `+0xc`
  to be at least `1000`, rejects the seven threshold bytes at `+0x11..+0x17`
  in `[0x0b, 0x9b]`, and rejects any of the eight tail bytes at
  `+0x19..+0x20` that are `>=100`.
- It calls `setEcountersEnableStateSync` at `0x1001de336`, propagates that
  failure, then invokes `AppleBCMWLANLQM::setStatsTimerIntervalMS` at
  `0x10000be36` if the LQM owner exists. It also configures RSSI and channel
  quality through `configureLqmRssiUpdates` (`0x10011a0ea`) and
  `configureLqmChanQUpdates` (`0x10011a308`), whose firmware/event side
  effects occur even though their status is not the final public status.
- `AppleBCMWLANCore::getLQM_CONFIG` at `0x100119800` first checks its LQM
  owner at `(Core + 0x48) + 0x15e8`. If it is absent, it returns
  `0xe00002bc` before dereferencing output. With an owner, it fills the full
  `0x24` carrier from the timer and Core configuration state.

## Local boundary and preserved telemetry

The Intel port has no eCounters synchronizer, Core LQM owner, `rssi_event`,
or `chq_event` configuration backend. Its independent timer is a telemetry
producer, not an equivalent public owner. The public getter now returns
`kIOReturnError` (`0xe00002bc`) without consuming output. The setter preserves
raw `0x16` for `NULL`; all non-null requests return `kIOReturnError` before
carrier access, cache mutation, or timer retune. That setter return is a local
no-backend, feature-off-equivalent quarantine; it is not claimed as Apple
missing-owner or valid-input return-code parity.

This deliberately loses dynamic public interval tuning. The independent
driver-owned timer's default `5000` ms interval, allocation, association
start/stop lifecycle, command-gated statistics publication, and event `0x27`
remain intact. This correction does not claim Apple enabled-path success,
opaque-state `0x2d` parity, valid-input return-code parity, private setter
invocation, firmware IOVAR, synthetic completion, radio transition, or
deployment.

## Deterministic guard

`scripts/lqm_config_owner_quarantine_report.py --check` verifies reference
anchors, removal of the public cache and public timer-retune call, retained
slots/dispatcher, the getter's no-owner boundary and setter's local
no-backend boundary, absence of the missing configuration backends, corrected
historical notes, and the continued local timer/event lifecycle. Runtime
deployment remains independently blocked by the guest's forced-off Wi-Fi
lifecycle state.
