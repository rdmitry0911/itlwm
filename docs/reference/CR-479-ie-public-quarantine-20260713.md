# CR-479: IE public setter and carrier-ABI false-success quarantine

Date: 2026-07-13

## Scope

This closure covers only the public `APPLE80211_IOC_IE` / selector-552
boundaries and the shared `apple80211_ie_data` carrier declaration. It does
not implement Broadcom WAPI or vendor-IE firmware work, change the generic
Tahoe commander/owner registry, alter association, radio, WCL, MWS, power, or
Intel firmware behavior, or issue an IE carrier at runtime.

## Recovered reference contract

The Tahoe 25C56 reference image is
`/System/Library/DriverExtensions/com.apple.DriverKit-AppleBCMWLAN.dext/com.apple.DriverKit-AppleBCMWLAN`
with SHA-256
`4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab`.

Infra `setIE` at `0x100018230` tail-dispatches Core virtual `+0x528` to
`AppleBCMWLANCore::setIE` at `0x100121826`. The Core first rejects a null
carrier with raw `0x16`. It then loads `ie_len` from `+0x10`, computes
`ie_len - 0x801` modulo 32 bits, and reaches the real branches only when that
unsigned value is greater than `0xfffff7ff`. The exact accepted range is
therefore `1..0x800`; zero and every value above `0x800` take the same raw
`0x16` return. The associated cold path identifies this as invalid association
data parameters and logs the IE length.

The accepted carrier is `0x814` bytes. Its first IE byte is at `+0x14`, not
`+0x18`: the Core compares `+0x14` with `0x44` and passes `data + 0x14` to
the custom path. With frame type `+0x04 == 4`, nonzero `add` at `+0x08`, and
first byte `0x44`, it invokes `AppleBCMWLANJoinAdapter::setCustomAssocIE` at
`0x10003eeac`, which performs real `wapiie` commander work. Otherwise it
calls `AppleBCMWLANCore::setVendorIE` at `0x10012109c`; that path uses
`runVirtualIOVarGet` and `runVirtualIOVarSet` for `vndr_ie` and preserves
allocation/transport status.

The recovered paths prove real backend work and the exact invalid range and
carrier offsets. They do not establish a complete public allocation beyond
the observed `0x814` carrier or Apple valid-input return-code parity outside
the backend result.

## Local divergence

Before this correction, the Skywalk setter accepted zero-length carriers,
called a local Tahoe commander, updated owner state and seven dead Skywalk IE
caches, then returned synthetic success. The legacy controller setter in
`AirportAWDL.cpp` also returned success for every input. Neither local path
routes into a live equivalent of `wapiie` or `vndr_ie`; the only local
`vndr_ie` occurrence is APSTA layout scaffolding whose reference note records
that it does not issue the command at runtime. There is no
`AppleBCMWLANCommander` or equivalent Intel IE backend. Separately, the checked-in carrier declaration inserted a
`pad1` dword after `ie_len`, making it `0x818` bytes and shifting IE data to
`+0x18` despite the existing APSTA reference record already documenting the
`0x814` / `+0x14` layout.

## Local correction

The shared declaration now has the recovered `0x814`-byte layout with IE
bytes at `+0x14`, backed by size and offset assertions. The shared payload
builder and both public setters retain raw `0x16` for null, zero-length, or
over-capacity carriers. Valid non-null carriers return `kIOReturnUnsupported`
before commander, owner, async-completion, transport, or cache mutation. The
seven dead Skywalk IE cache fields and their two reset sites are removed.

This is a no-backend quarantine, not Apple valid-input return-code parity: the
reference accepts valid carriers only while applying real WAPI or `vndr_ie`
work.

## Verification boundary

The deterministic report requires the wrapper/Core/IOVAR and ABI anchors,
the `1..0x800` validation, both public fail-closed setters, removal of only
dead Skywalk IE pseudo-state, and absence of a local IE backend execution
path. Build/load and normal association/traffic gates establish regression safety.
No IE carrier, private setter ioctl, or guessed opaque transport is issued, so
runtime results must not be presented as direct setter execution. Radio
OFF/ON remains excluded because the A2DF baseline independently reproduces
its WCL lifecycle panic.
