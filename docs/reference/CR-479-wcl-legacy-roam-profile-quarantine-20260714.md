# CR-479 — WCL_LEGACY_ROAM_PROFILE_CONFIG false-success quarantine

Date: 2026-07-14

## Scope

This correction covers only public
`AirportItlwmSkywalkInterface::setWCL_LEGACY_ROAM_PROFILE_CONFIG`. The former
local implementation copied an inferred 0x60-byte opaque carrier to a cache,
set a flag, and returned success. Scoped source found that pseudo type/cache/
flag only in the declaration, two reset pairs, and this setter. It found no
local RoamAdapter, legacy V4/V2 policy owner, `roam_prof`/multi-AP transport,
callback, or matching completion/status consumer.

The direct local null guard remains `kIOReturnBadArgumentTahoe`
(`0xe00002bc`). A non-null request now returns `kIOReturnUnsupported` before
reading the carrier. The dead pseudo-layout/cache/flag/reset lines are removed.
The modern WCL profile, generic STA `ROAM_PROFILE`, reassociation, roam lock,
user cache, scan, key, link, WCL event, and generic adaptive-roaming
platform-property paths remain unchanged.

## Tahoe 25C56 reference recovery

The recovered DEXT is
`/System/Library/DriverExtensions/com.apple.DriverKit-AppleBCMWLAN.dext/com.apple.DriverKit-AppleBCMWLAN`,
SHA-256
`4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab`.

- `AppleBCMWLANInfraProtocol::setWCL_LEGACY_ROAM_PROFILE_CONFIG` at
  `0x100018b28` dispatches through virtual `+0x6d0` to Core `0x100141de4`.
- Core selects RoamAdapter at `+0x15c0` and tail-jumps to
  `setLEGACY_ROAM_PROFILE_CONFIG` at `0x10001c272`.
- The null route reaches `0x10019ffce`, which stores `0xe00002bc`.
- A non-null request calls `setRoamingProfile` `0x10001a17e`, which selects
  V4 `0x10001a782` or V2 `0x10001b3c4` policy. Those paths build `roam_prof`
  Commander work through `sendIOVarSet` `0x10017b900` with
  `handleRoamProfileAsyncCallBack` `0x10001bd9a`; enqueue/transport status
  feeds the top-level return paths.
- The same lifecycle calls `configureMultiAPBit` `0x10001c322`, which sends
  `roam_multi_ap_env` through Commander and installs
  `handleMultiAPBitAsyncCallBack` `0x10001e809`.

Those branches are a multi-owner policy and transport lifecycle. They do not
justify a local opaque cache-and-success substitute. Recovered reads do not
establish a complete public carrier layout.

## Local boundary and non-claims

No guessed profile ABI, `roam_prof`, multi-AP IOVAR, direct firmware request,
private IOCTL, synthetic callback/completion, or policy mutation is introduced.
No modern profile, generic STA `ROAM_PROFILE`, reassociation, scan, key, link,
WCL event, or unrelated adaptive-roaming path is changed.

No direct runtime invocation of this setter is claimed. Runtime validation is
ordinary credentialed association plus bounded traffic and focused fault
filters; radio OFF/ON remains excluded because the independent WCL lifecycle
failure is not a legacy-profile regression signal.
