# CR-496 — BTCOEX_2G_CHAIN_DISABLE getter no-producer quarantine

Date: 2026-07-15

## Scope

This correction covers only Tahoe V2/Skywalk slot `[502]`,
`getBTCOEX_2G_CHAIN_DISABLE`. It removes the reset-only 2G chain-disable
getter cache and its fabricated eight-byte successful carrier. It preserves
IOC 260 GET/SET routing, public virtual slots, the opaque carrier declaration
and builder, the existing fail-closed setter, and the separate BTCOEX profile,
active-profile, commander, and owner surfaces.

## Recovered 25C56 reference

The reference x86_64 DriverKit DEXT is
`com.apple.DriverKit-AppleBCMWLAN`:

~~~text
path: /System/Library/DriverExtensions/com.apple.DriverKit-AppleBCMWLAN.dext/com.apple.DriverKit-AppleBCMWLAN
SHA-256: 4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab
UUID: 149C0AD1-A92F-35BC-AA69-5C8815C5421E
~~~

Infra `getBTCOEX_2G_CHAIN_DISABLE` wrapper `0x10001758c` dispatches through
virtual offset `0x3e8` to Core `0x1001e57fc`. The selected Core body returns
`0xe00002c2` for a null caller. For a non-null caller it allocates a commander
RX buffer based on `getMaxCmdRxPayload()`, gets the commander at
`(Core + 0x48) + 0x1520`, and calls
`runIOVarGet("btc_2g_shchain_disable")`. It copies RX bytes `+0x04/+0x05` only
to caller `+0x04/+0x05` when the resulting status is zero or `0xe00002e3`,
then returns that original status. The selected capture does not show a caller
`+0x00` write or prove the complete opaque carrier layout.

The paired reference setter is a separate commander transaction: Infra
`0x1000187ac` dispatches through `+0x690` to Core `0x1001e3a3e`, which sends a
six-byte `0x00060001`-headed payload through
`runIOVarSet("btc_2g_shchain_disable")`. That establishes distinct GET and SET
firmware work; it does not establish SET-to-GET consistency. The selected
static capture is
`docs/reference/artifacts/btcoex-2g-chain-disable-getter-25c56/raw.txt`.

## Local correction

AirportItlwm had no local GET producer. Its chain-disable field was only
zeroed in the two init paths and read by this getter; the paired setter already
fails closed and never writes it. The field and resets are removed. The local
`0xe00002c2` null boundary remains as a safety boundary. Every non-null
request now returns `kIOReturnUnsupported` before caller mutation.

The existing `TahoePayloadBuilders::buildBtcoex2GChainDisable`, IOC route,
setter, BTCOEX profile/active slots, and commander/owner declarations are
deliberately unchanged. They are not evidence of a current local GET result
producer.

This is a no-producer quarantine, **not Apple null-input, valid-input
return-code, value, carrier-layout, special-`0xe00002e3`, firmware, or
runtime-selector parity**. It invokes no private selector, IOVAR, firmware
command, scan, radio transition, deployment, association, or traffic path.

## Deterministic guard

`scripts/btcoex_2g_chain_disable_getter_quarantine_report.py --check` verifies
the reference identity and raw transport anchors, retained public route/ABI
and local null boundary, no-output non-null failure, removal of only the dead
chain-disable cache, preservation of the separate setter/profile/active
surfaces, and supersession of the former cache-backed documentation.
