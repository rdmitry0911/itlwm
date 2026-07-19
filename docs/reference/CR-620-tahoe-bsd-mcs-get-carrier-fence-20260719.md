# CR-620: Tahoe BSD MCS GET carrier fence

Date: 2026-07-19

## Scope

This closure covers only raw BSD `GET` ingress for `APPLE80211_IOC_MCS`
(selector 57) on Tahoe. It does not alter MCS decoding, cached-rate state, the
local MCS producer, or a card-specific route.

## Fresh reference contract

The exact Tahoe 25C56 BootKC evidence is pinned in
`artifacts/tahoe-mcs-get-bsd-carrier-bootkc-current/raw.txt`. The raw GET
handler table maps selector `0x39` to the recovered private wrapper. It rejects
a null interface with `0x66`, requires a non-null carrier and `req_len == 0x04`
(otherwise `0x16`), then creates a zeroed eight-byte stack carrier for WCL.

Apple sends legacy GET `0xc02869c9`, selector `0x39`, and length `0x08` through
`IO80211Glue::sendIOUCToWcl`. When WCL is unhandled or returns `0xe082280f`,
the same internal stack carrier reaches the public `apple80211getMCS` wrapper.
That wrapper gates selector `0x39` at vtable `+0xcc8`, safely casts to
`IO80211InfraProtocol`, and tail-calls owner slot `+0xee8` (or returns
`0xe082280e` when absent). Only the four-byte index at internal offset `+4` is
copied to the public caller carrier.

## Local divergence

The local raw BSD bridge directly dispatches selector 57 to `getMCS`. That
helper initializes the complete eight-byte version-and-index carrier without
the public four-byte boundary and bypasses Tahoe's WCL-first admission,
internal copyout, and public-owner fallback. The source pins the local
eight-byte layout, but family code owns public carrier validation and transport.

Selector 57 has no Tahoe card-specific route in
`TahoeSkywalkIoctlRoutes.hpp`; this correction neither creates one nor removes
the helper used by kernel-owned MCS consumers.

## Correction

On Tahoe only, raw BSD `GET` selector 57 delegates to
`IO80211InfraProtocol::processBSDCommand` before any local nested-carrier
access. The fence reads only the outer selector. It preserves the internal MCS
producer and restores the exact family-owned public-carrier boundary.

## Verification boundary

`scripts/test_tahoe_mcs_get_bsd_carrier_fence.sh` pins the current 25C56 table,
private and public wrapper identities, the external four-byte/internal
eight-byte split, WCL-first fallback, GET-only delegation, retained local
producer, and absence of a fabricated card route. Build/load and normal
association/traffic gates establish regression safety. No raw BSD or raw
Apple80211 selector is sent at runtime.
