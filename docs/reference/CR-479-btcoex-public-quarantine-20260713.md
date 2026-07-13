# CR-479: BTCOEX public setter false-success quarantine

Date: 2026-07-13

## Scope

This closure covers only the Tahoe public `BTCOEX_PROFILE`,
`BTCOEX_PROFILE_ACTIVE`, and `BTCOEX_2G_CHAIN_DISABLE` setter boundaries. It
does not emulate Apple's Broadcom coexistence firmware, change the generic
Tahoe commander or owner registry, alter the paired getters, or change MWS,
WCL, radio, power, association, or Intel boot-time coexistence configuration.

## Recovered reference contract

The Tahoe 25C56 reference image is
`/System/Library/DriverExtensions/com.apple.DriverKit-AppleBCMWLAN.dext/com.apple.DriverKit-AppleBCMWLAN`
with SHA-256
`4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab`.

The Infra wrapper `setBTCOEX_PROFILE` at `0x1000186c8` tail-dispatches Core
virtual `+0x670`; recovered Core `setBTCOEX_PROFILE` is at `0x100124656`.
It returns `0xe00002c2` for a null carrier, raw band `+0x03 >= 5`, mode at
raw `+0x00` outside `1..4`, or profile index at raw `+0x04 >= 10`. For a
valid `0x38`-byte record it stores mode-specific state, follows the
mode-specific coexistence path, allocates command payloads, and calls
`AppleBCMWLANCommander::runIOVarSet("btc_profile")`; commander/allocation
status is preserved rather than replaced with synthetic success.

`setBTCOEX_PROFILE_ACTIVE` at Infra `0x100018714` dispatches virtual `+0x698`
to Core `0x1001e393a`. It returns `0xe00002c2` for null and sends the four
bytes at caller `+0x04` through `runIOVarSet("btc_profile_active")`, retaining
the resulting status. `setBTCOEX_2G_CHAIN_DISABLE` at Infra `0x1000187ac`
dispatches virtual `+0x690` to Core `0x1001e3a3e`. It returns `0xe00002c2` for
null, constructs a six-byte payload with fixed header `0x00060001` plus the
two-byte caller value at `+0x04`, and calls
`runIOVarSet("btc_2g_shchain_disable")`, again preserving transport status.

The recovered paths prove concrete firmware work and the visible invalid
branches. They do not establish a complete public allocation for every opaque
carrier or Apple valid-input return-code parity outside the transport result.

## Local divergence

Before this correction, each public setter called the local Tahoe commander.
That commander only changed a local registry and `dispatchTransport` completed
the context with status zero before returning success. The Skywalk setter then
copied the profile record or updated the active/chain getter caches. There is
no Intel implementation of `btc_profile`, `btc_profile_active`,
`btc_2g_shchain_disable`, or an equivalent commander IOVAR. The existing Intel
coexistence initialization is boot-time fixed firmware configuration, not a
consumer of these Tahoe caller carriers.

`cachedBtcoexProfiles` and `cachedBtcoexProfileValidMask` were dead and occur
only in their declarations, two initialization/reset blocks, and the profile
setter. The active and chain-disable cache fields are deliberately retained:
the paired public getters read them and this batch does not alter getter
semantics.

## Local correction

The three setters retain their local null/absent-instance raw
`0xe00002c2` error. The profile setter also retains its builder and
band/mode/index invalid errors. A valid non-null carrier now returns
`kIOReturnUnsupported` before commander, owner, async-completion, or cache
mutation. The two dead profile cache fields and their initialization/reset
sites are removed; paired getter caches remain untouched.

This is a no-backend quarantine, not Apple valid-input return-code parity: the
reference accepts valid carriers only while applying actual coexistence
firmware work.

## Verification boundary

The deterministic report requires all three wrapper/Core/IOVAR anchors,
profile invalid gates, retained active/chain getter-cache scope, fail-closed
valid paths, and removal of only dead profile pseudo-state. Build/load and
normal association/traffic gates establish regression safety. No BTCOEX
carrier, private setter ioctl, or guessed opaque transport is issued, so
runtime results must not be presented as direct setter execution. Radio
OFF/ON remains excluded because the A2DF baseline independently reproduces
its WCL lifecycle panic.
