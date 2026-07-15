# CR-495 — BTCOEX_PROFILE_ACTIVE getter no-producer quarantine

Date: 2026-07-15

## Scope

This correction covers only Tahoe V2/Skywalk slot `[498]`,
`getBTCOEX_PROFILE_ACTIVE`. It removes the reset-only active-profile getter
cache and its fabricated eight-byte successful carrier. It preserves the IOC
256 GET/SET route, public virtual slots, the eight-byte opaque carrier
declaration and builder, the existing fail-closed setter, and the separate
BTCOEX owner/commander/chain-disable surfaces.

## Recovered 25C56 reference

The reference x86_64 DriverKit DEXT is
`com.apple.DriverKit-AppleBCMWLAN`:

~~~text
path: /System/Library/DriverExtensions/com.apple.DriverKit-AppleBCMWLAN.dext/com.apple.DriverKit-AppleBCMWLAN
SHA-256: 4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab
UUID: 149C0AD1-A92F-35BC-AA69-5C8815C5421E
~~~

Infra `getBTCOEX_PROFILE_ACTIVE` wrapper `0x100017470` dispatches through
virtual offset `0x3b0` to Core `0x1001e509a`. The selected Core body returns
`0xe00002c2` for a null caller. For a non-null caller it stages one dword,
gets the commander at `(Core + 0x48) + 0x1520`, and calls
`runIOVarGet("btc_profile_active")`. It copies the staged dword only to caller
`+0x04` when the resulting status is zero or `0xe00002e3`, and returns that
original status. The selected capture does not show a caller `+0x00` write.

The paired reference setter is a separate commander transaction:
Infra `0x100018714` dispatches through `+0x698` to Core `0x1001e393a`, which
sends the four caller bytes at `+0x04` through
`runIOVarSet("btc_profile_active")`. That establishes distinct GET and SET
firmware work; it does not establish SET-to-GET consistency or a complete
carrier layout. The selected static capture is
`docs/reference/artifacts/btcoex-profile-active-getter-25c56/raw.txt`.

## Local correction

AirportItlwm had no local GET producer. Its active-profile field was only
zeroed in the two init paths and read by this getter; the paired setter already
fails closed and never writes it. The field and resets are removed. The local
`0xe00002c2` null boundary remains as a safety boundary. Every non-null
request now returns `kIOReturnUnsupported` before caller mutation.

The existing `TahoeOwnerRegistry::btcoex.activeProfile`,
`TahoePayloadBuilders::buildBtcoexProfileActive`, commander/owner declarations,
IOC route, and setter are deliberately unchanged. The then-distinct
chain-disable getter is corrected separately by CR-496. None of these are
evidence of a current local active-profile GET result producer.

This is a no-producer quarantine, **not Apple null-input, valid-input
return-code, value, carrier-layout, special-`0xe00002e3`, firmware, or
runtime-selector parity**. It invokes no private selector, IOVAR, firmware
command, scan, radio transition, deployment, association, or traffic path.

## Deterministic guard

`scripts/btcoex_profile_active_getter_quarantine_report.py --check` verifies
the reference identity and raw transport anchors, retained public route/ABI
and local null boundary, no-output non-null failure, removal of only the dead
active getter cache, preservation of the separate setter/owner/chain surfaces,
and supersession of the former cache-backed documentation.
