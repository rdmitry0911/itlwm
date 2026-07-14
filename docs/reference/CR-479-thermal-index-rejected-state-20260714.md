# CR-479 — THERMAL_INDEX rejected-state correction

Date: 2026-07-14

## Scope

This correction covers the local interaction between
`AirportItlwmSkywalkInterface::setTHERMAL_INDEX` and
`getTHERMAL_INDEX`. It removes a cache write that occurred even though the
setter returned the fixed failure `kIOReturnBadArgumentTahoe`
(`0xe00002bc`). The getter retains its established version-plus-u32 ABI
carrier with a zero local baseline, but a rejected direct vtable request can
no longer manufacture that value.

No direct setter invocation, IOVAR, private ioctl, synthetic firmware state,
radio transition, deployment, or runtime traffic is introduced.

## Tahoe 25C56 reference recovery

The live guest x86_64 DriverKit DEXT is
`/System/Library/DriverExtensions/com.apple.DriverKit-AppleBCMWLAN.dext/com.apple.DriverKit-AppleBCMWLAN`,
SHA-256
`4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab`.

- Infra `setTHERMAL_INDEX` at `0x100018760` dispatches through Core virtual
  `+0x4f0` to `AppleBCMWLANCore::setTHERMAL_INDEX` at `0x100120586`.
- Core calls `featureFlagIsBitSet(0x3b)` at `0x1000c846e`. If that feature is
  absent, Core returns `0xe00002bc` before consuming the carrier. The enabled
  path reads the requested scalar at carrier `+0x4` and accepts exactly
  `1..100`; this recovery does not claim a complete enabled-path NULL contract.
- A valid request allocates a 12-byte payload, writes a `tvpm` command header
  and the requested scalar at payload `+0x8`, then calls Commander
  `runIOVarSet("tvpm")` at `0x10017b6e6`.
- Core writes the scalar to `(Core + 0x48) + 0x0` only when the transport
  result is zero or special status `0xe3ff8117`; it returns the raw transport
  status. Allocation failure is `0x0c`.
- `AppleBCMWLANCore::getTHERMAL_INDEX` at `0x100106eda` copies that core-state
  scalar to caller offset `+0x4` and returns success.

## Local boundary and non-claims

The Intel port has no `tvpm` payload builder, Commander owner, or firmware
transport lifecycle. Its local fixed-fail boundary therefore remains
`kIOReturnBadArgumentTahoe` for the public setter carrier, but it now rejects
before reading the carrier and has no synthetic cached state. This preserves a
safe local boundary and does not claim Apple valid-input return-code parity or
an enabled-Core NULL return-code contract.

The normal BSD dispatch only exposes `getTHERMAL_INDEX`; it routes non-get
commands to `kIOReturnUnsupported`. The virtual setter remains present for ABI
completeness, and this correction prevents its rejected input from affecting a
subsequent getter. The getter's zero is a local no-accepted-owner-action
baseline, not a statement about a live Tahoe thermal index.

## Deterministic guard

`scripts/thermal_index_rejected_state_report.py --check` verifies the
reference anchors, removal of the rejected-state cache, the retained fixed
failure without carrier consumption, the default-only getter, retained
interface slots and dispatch boundary, scoped owner absence, and corrected
historical documentation. Runtime deployment remains independently blocked by
the guest's forced-off Wi-Fi lifecycle state.
