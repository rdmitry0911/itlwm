# CR-497 — RSN_XE JoinAdapter no-producer quarantine

Date: 2026-07-15

## Scope

This correction covers only Tahoe V2/Skywalk protocol slots `[531]`
`getRSN_XE` and `[606]` `setRSN_XE`. It removes the local RSNXE cache echo,
its three reset-only fields, and successful GET/SET acknowledgements. It
preserves both public virtual slots, the opaque forward declaration, all
existing RSN_IE/RSN_CONF surfaces, and the independent APSTA association-event
RSNXE parser. The tree has no numeric `APPLE80211_IOC_RSN_XE` selector or
request-switch route; this correction does not add one.

## Recovered 25C56 reference

The reference x86_64 DriverKit DEXT is
`com.apple.DriverKit-AppleBCMWLAN`:

~~~text
path: /System/Library/DriverExtensions/com.apple.DriverKit-AppleBCMWLAN.dext/com.apple.DriverKit-AppleBCMWLAN
SHA-256: 4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab
UUID: 149C0AD1-A92F-35BC-AA69-5C8815C5421E
~~~

Infra `getRSN_XE` wrapper `0x100016f18` dispatches through virtual offset
`0x348` to Core `0x100107da0`; `setRSN_XE` wrapper `0x1000181e4` dispatches
through `0x510` to Core `0x100120e96`. Both Core bodies reach the JoinAdapter
at `(Core + 0x48) + 0x1528`. GET passes caller `+0x06` with capacity `0x101`
to `JoinAdapter::getAssocRSNXE`, then writes the returned low 16-bit length to
caller `+0x04` following the JoinAdapter call. SET reads the low 16-bit public
length at `+0x04`, passes caller `+0x06`, and tail-calls
`JoinAdapter::setAssocRSNXE`. The selected static recovery does not establish
a complete caller layout or a local substitute for the owner's error handling.

The selected JoinAdapter bodies gate both directions on SAE-PK and WPA3-SAE
support (`0xe00002c2` on those observed failures). GET checks caller capacity
against JoinAdapter state length `+0x68` and returns observed `0xe00002db` on
the insufficient-capacity branch; SET rejects lengths above `0x101` with the
same observed status, copies non-empty bytes into state `+0x70`, zeroes the
state for an empty input, and records the length at `+0x68`.

The separate `JoinAdapter::getAssociatedRSNXE` can issue `rsnxe` IOVAR work,
but the selected Core GET directly calls `getAssocRSNXE`; this recovery does
not claim a direct Core GET IOVAR transaction. The selected static capture is
`docs/reference/artifacts/rsn-xe-join-adapter-25c56/raw.txt`.

## Local correction

AirportItlwm had no matching JoinAdapter association owner. Its length, byte
array, and validity fields were only reset in the two init paths and read or
written by the local GET/SET pair; the validity flag had no reader. The fields
and reset blocks are removed. The existing local null guards remain safety
boundaries. Every non-null GET and SET now returns `kIOReturnUnsupported`
before caller mutation or local cache mutation.

`AirportItlwmAPSTAOwner::apsta_copy_rsnxe`, the APSTA `0xf4` IE parser,
RSN_IE/RSN_CONF surfaces, and the public protocol declarations are deliberately
unchanged. They are not a replacement producer for this JoinAdapter-owned
association carrier.

This is a no-producer quarantine, **not Apple null-input, valid-input
return-code, carrier-layout, capability-gate, JoinAdapter-state, firmware, or
runtime-selector parity**. It invokes no private selector, IOVAR, firmware
command, scan, radio transition, deployment, association, or traffic path.

## Deterministic guard

`scripts/rsn_xe_join_adapter_quarantine_report.py --check` verifies reference
identity/raw routing, owner anchors, and decisive JoinAdapter control-flow
branches; retained protocol ABI without a newly invented numeric IOC; local
null boundaries; no-output/no-cache non-null failure for both directions;
removal of the three dead fields; preservation of the independent APSTA parser;
and supersession of the former cache-backed documentation.
