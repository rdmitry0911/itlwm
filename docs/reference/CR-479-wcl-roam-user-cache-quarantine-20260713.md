# CR-479 — WCL_ROAM_USER_CACHE false-success quarantine

Date: 2026-07-13

## Scope

This correction covers only the public
`AirportItlwmSkywalkInterface::setWCL_ROAM_USER_CACHE` slot. The former local
implementation copied an inferred 0x7c-byte carrier to a cache, set a flag,
and returned success. Scoped source found that cache and flag only in their
declarations, two reset pairs, and this setter. It found no local RoamAdapter,
adaptive-roam channel-state owner, validation/mutation helpers, Commander
transport, or matching completion/status consumer.

The direct local null guard remains `kIOReturnBadArgumentTahoe`
(`0xe00002bc`). A non-null request now returns `kIOReturnUnsupported` before
reading the carrier. The dead pseudo-layout, cache, flag, and reset lines are
removed. Reassociation, roam-lock, roam-profile, and the separate generic
adaptive-roaming platform-property path remain unchanged.

## Tahoe 25C56 reference recovery

The recovered DEXT is
`/System/Library/DriverExtensions/com.apple.DriverKit-AppleBCMWLAN.dext/com.apple.DriverKit-AppleBCMWLAN`,
SHA-256
`4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab`.

- `AppleBCMWLANInfraProtocol::setWCL_ROAM_USER_CACHE` at `0x100018ca0`
  dispatches through virtual `+0x6e0` to Core `0x100141e52`.
- Core selects RoamAdapter at `+0x15c0` and tail-jumps to
  `cmdROAM_USER_CACHE` at `0x10001c916`.
- The adapter allocates 0x78 bytes of backend state, handles a null request
  through its error path, validates non-empty channel input with
  `isAdaptiveRoamRequestValid` `0x10001ca9a`, and retains error status.
- It clears channels through `clearChannelsFromUserRoamCache` `0x10001cb3a`,
  adds channels through `addChannelsToUserRoamCache` `0x10001cc16`, and
  conditionally changes override state through
  `setOverrideStateFromUserRoamCache` `0x10001cd78`.
- Recovered reads include channel entries from `+0x0` in 0x0c strides, channel
  count at `+0x78`, and override-related bytes including `+0x7a`; they do not
  establish a complete public carrier layout.

Those branches are an adaptive-roam state and transport lifecycle. They do not
justify a local opaque cache-and-success substitute.

## Local boundary and non-claims

No guessed user-cache carrier, channel mutation, private IOCTL, direct firmware
request, Commander IOVAR, or synthetic completion is introduced. No unrelated
roam, reassoc, lock, profile, scan, or generic adaptive-roaming path is
changed.

No direct runtime invocation of this setter is claimed. Runtime validation is
ordinary credentialed association plus bounded traffic and focused fault
filters; radio OFF/ON remains excluded because the independent WCL lifecycle
failure is not a user-cache regression signal.
