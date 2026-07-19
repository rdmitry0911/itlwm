# CR-609 — Tahoe BSD link-changed-event carrier fence

## Scope

The public `APPLE80211_IOC_LINK_CHANGED_EVENT_DATA` GET (156) carries the
fixed 32-byte `apple80211_link_changed_event_data` response.  The local BSD
bridge previously passed its nested `req_data` address straight to
`getLINK_CHANGED_EVENT_DATA`, which clears and fills all 32 bytes without
consulting `req_len`.

The recovered Tahoe WCL wrapper owns this response carrier and accepts the
snapshot only after a `0x20` length gate.  The BSD bridge now delegates only
the GET direction to `IO80211Family` before the local dispatcher, retaining
that raw outer-ioctl ownership boundary.

## Preserved paths and limits

This change does not synthesize a status or payload.  The local snapshot
producer remains available to kernel-owned callers.

Selector 156 is not admitted by `TahoeSkywalkIoctlRoutes`, so the fence does
not change the card-specific route.  It also does not alter the separate
32-byte `APPLE80211_M_LINK_CHANGED` publication from
`setLinkStateInternal`; that association transition publisher retains its
kernel-owned inline payload.

`CHIP_COUNTER_STATS` and `TKO_DUMP` were deliberately not grouped into this
change: their absent-owner Tahoe paths return their recovered fixed failures
without dereferencing their output carriers.  Redirecting them would change
their contract without removing a write risk.

## Validation boundary

The accompanying static guard asserts the selector and 32-byte ABI, verifies
that the BSD GET fence precedes the local dispatcher without inspecting the
nested carrier, and keeps the internal event publisher and absence from the
card route explicit.  Runtime validation uses only normal Wi-Fi OFF/ON,
keychain join, AP authorization, and protected laboratory traffic; no raw
Apple80211 or BSD selector probe is used.
