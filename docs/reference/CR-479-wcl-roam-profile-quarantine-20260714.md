# CR-479 — WCL_ROAM_PROFILE_CONFIG false-success quarantine

Date: 2026-07-14

## Scope

This correction covers only modern public
`AirportItlwmSkywalkInterface::setWCL_ROAM_PROFILE_CONFIG`. The former local
implementation copied an inferred 0x23c-byte opaque carrier to a cache, set a
flag, and returned success. Scoped source found that pseudo type/cache/flag
only in the declaration, two reset pairs, and this setter. It found no local
RoamAdapter, modern per-band policy owner, `roam_prof`/`join_pref`/multi-AP
transport, callback, or matching completion/status consumer.

The direct local null guard remains `kIOReturnBadArgumentTahoe`
(`0xe00002bc`). A non-null request now returns `kIOReturnUnsupported` before
reading the carrier. The dead pseudo-layout/cache/flag/reset lines are removed.
The separate legacy WCL profile, generic STA `ROAM_PROFILE`, reassociation,
roam lock, user cache, scan, key, link, WCL event, and generic adaptive-roaming
platform-property paths remain unchanged.

## Tahoe 25C56 reference recovery

The recovered DEXT is
`/System/Library/DriverExtensions/com.apple.DriverKit-AppleBCMWLAN.dext/com.apple.DriverKit-AppleBCMWLAN`,
SHA-256
`4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab`.

- `AppleBCMWLANInfraProtocol::setWCL_ROAM_PROFILE_CONFIG` at `0x100018b74`
  dispatches through virtual `+0x6d8` to Core `0x100141e10`.
- The null route reaches `0x1001a01a0`, which stores `0xe00002bc`.
- A non-null request selects RoamAdapter at `+0x15c0` and tail-jumps to
  `setROAM_PROFILE_CONFIG` at `0x10001c3f8`.
- The owner conditionally reads three per-band records beginning at `+0x0`,
  `+0xb8`, and `+0x170`, then calls `setRoamingProfileV6` `0x10001bfca` with
  band identities 2, 1, and 4. That helper builds the `roam_prof` Commander
  request, uses `sendIOVarSet` `0x10017b900`, and installs
  `handleRoamProfileAsyncCallBack` `0x10001bd9a`; enqueue/transport status
  flows through the top-level error paths.
- Bit 1 at `+0x238` drives `disable6GForRoamScans` `0x10001c5b0`, including
  its `join_pref` Commander route and
  `disable6GForRoamScansCallback` `0x10001de02` or a synchronous
  `runIOVarSet` `0x10017b6e6` path according to owner capability.
- The same lifecycle calls `applyRoamingCandidateBoost` `0x10001c6ba` and
  `configureMultiAPBit` `0x10001c322` for multi-AP state.

Those branches are a multi-owner policy and transport lifecycle. They do not
justify a local opaque cache-and-success substitute. Recovered reads do not
establish a complete public carrier layout.

## Local boundary and non-claims

No guessed profile ABI, `roam_prof`, `join_pref`, multi-AP IOVAR, direct
firmware request, private IOCTL, synthetic callback/completion, or policy
mutation is introduced. No legacy profile, generic STA `ROAM_PROFILE`,
reassociation, scan, key, link, WCL event, or unrelated adaptive-roaming path
is changed.

No direct runtime invocation of this setter is claimed. Runtime validation is
ordinary credentialed association plus bounded traffic and focused fault
filters; radio OFF/ON remains excluded because the independent WCL lifecycle
failure is not a modern-profile regression signal.
