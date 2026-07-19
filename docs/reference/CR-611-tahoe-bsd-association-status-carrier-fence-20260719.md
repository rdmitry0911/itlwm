# CR-611 — Tahoe BSD association-status carrier fence

## Scope

The Tahoe public `APPLE80211_IOC_ASSOCIATION_STATUS` GET (50) accepts a
non-null four-byte status destination. The recovered route
`getASSOCIATION_STATUS` at `0xffffff80021d6ab2` rejects another shape with
`0x16`, constructs a private eight-byte `{ version, status }` carrier, and
first issues legacy GET `0xc02869c9` for selector `0x32` at length eight. It
then falls back to `apple80211getASSOCIATION_STATUS` at
`0xffffff80021bed93`; that leaf checks interface admission at `+0xcc8` and
tail-calls the interface owner at `+0x428`. Only local `+0x4` (`status`) is
copied to the public four-byte destination. Public SET at
`0xffffff80021c3be4` is a fixed `0xe082280e` leaf.

The local BSD bridge instead passed its nested `req_data` address directly to
`getASSOCIATION_STATUS`, which clears and fills the complete eight-byte legacy
carrier without consulting `req_len`. The GET now delegates to
`IO80211Family` before that local dispatcher, preserving the Tahoe marshalling
and ownership boundary.

## Preserved paths and limits

No association status is synthesized by this fence. The local eight-byte
implementation remains in the source, but this change does not assert a
kernel-owned caller for it. Public SET is not part of this change and retains
its existing family handling.

Selector 50 is absent from `TahoeSkywalkIoctlRoutes`; no card-specific
kernel-carrier route is added. The getter reports association progress/error
for CoreWiFi/CWEAPOL consumers but is not a join trigger, so the fence leaves
the association state machine and its hidden join carriers unchanged.

## Validation boundary

The accompanying static guard fixes selector 50 and the eight-byte legacy
layout, verifies the Tahoe GET fence precedes the local dispatcher without
touching the nested carrier, and keeps the local producer and absence from the
card route explicit. Runtime validation is limited to normal Wi-Fi OFF/ON,
keychain join, AP authorization and protected laboratory traffic; no raw
Apple80211 or BSD probe is issued.
