# CR-479 — SET_PROPERTY callback quarantine

Date: 2026-07-14

## Scope

This correction covers only `AirportItlwmSkywalkInterface::setSET_PROPERTY`.
The previous local body marked an unread `cachedSetPropertyIoctlSeen` flag and
returned success for every non-null opaque carrier. Tahoe instead routes the
request through a broad gated Core property callback.

This is intentionally a standalone control-plane quarantine. No direct setter
invocation, property callback, private ioctl, radio transition, deployment,
association, or traffic is introduced.

## Tahoe 25C56 reference recovery

The live guest x86_64 DriverKit DEXT is
`/System/Library/DriverExtensions/com.apple.DriverKit-AppleBCMWLAN.dext/com.apple.DriverKit-AppleBCMWLAN`,
SHA-256
`4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab`.

- Infra `setSET_PROPERTY` at `0x100018914` dispatches through Core virtual
  `+0x728` (entry `0x1003a1810`) to
  `AppleBCMWLANCore::setSET_PROPERTY` at `0x1000da8de`.
- The recovered Core body has no NULL gate. It performs an owner/gate query,
  through `(Core + 0x48) + 0x7950` virtual `+0x90`, then reads opaque carrier
  pointer `+0x8`, marks in-flight state at `(Core + 0x48) + 0x794b`, invokes
  Core virtual callback `+0x790`, clears the state bit, and returns the
  callback status. Vtable entry `0x1003a1878` resolves that callback to
  `AppleBCMWLANCore::setProperties(OSObject*)` at `0x1000da982`.
- If the direct gate is unavailable, Core obtains its command gate and runs
  virtual `+0x68`, then command-gate virtual `+0x38` invokes
  `setPropertyIoctlGated` at `0x1000da8aa`. That gated helper reads the same
  carrier `+0x8`, marks and clears the same state bit, invokes callback
  `+0x790`, and returns its status.

## Local boundary and non-claims

`apple80211_set_property_unserialized_data` is only a forward declaration in
the local headers. There is no `APPLE80211_IOC_SET_PROPERTY` route, no local
carrier ABI or length validation, and no matching gated property callback.
The port therefore cannot safely inspect carrier `+0x8` or acknowledge a
generic property operation.

The local setter retains its existing `kIOReturnBadArgumentTahoe` NULL safety
boundary and returns `kIOReturnUnsupported` for every non-null carrier before
reading it. The dead cache and both reset paths are removed. This is a local
no-owner quarantine; it does not claim Apple NULL, callback, or valid-input
return-code parity. The virtual slot remains for ABI completeness.

## Deterministic guard

`scripts/set_property_callback_quarantine_report.py --check` verifies the
canonical reference anchors, no carrier dereference or synthetic cache, the
retained virtual slot and local NULL safety guard, forward-only/no-IOC local
scope, absent callback backend, and corrected historical documentation.
Runtime deployment remains independently blocked by the guest's forced-off
Wi-Fi lifecycle state.
