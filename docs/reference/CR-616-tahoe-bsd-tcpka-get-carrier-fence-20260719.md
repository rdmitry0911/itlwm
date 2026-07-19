# CR-616: Tahoe BSD TCP keepalive GET carrier fence

Date: 2026-07-19

## Scope

This closure covers only the Tahoe raw BSD `GET` ingress for
`APPLE80211_IOC_OFFLOAD_TCPKA_ENABLE` (selector 265). It does not advertise
TCP keepalive support, alter the cached enable value, emulate a keepalive
owner, issue a keepalive request, or change the selector's `SET` path.

## Fresh reference contract

The exact Tahoe 25C56 BootKC evidence is pinned in
`artifacts/tahoe-tcpka-get-bsd-carrier-bootkc-current/raw.txt`. The private
BSD wrapper rejects a null interface with `0x66`, requires non-null carrier
plus `req_len == 0x4` (otherwise `0x16`), and zeroes an internal eight-byte
legacy result. It sends legacy GET `0xc02869c9`, selector `0x109`, and length
`0x8` to `IO80211Glue::sendIOUCToWcl`.

Only a handled WCL result other than `0xe082280f` is returned directly.
Otherwise Apple calls the public leaf, gates selector `0x109` at vtable
`+0xcc8`, safely casts to `IO80211InfraProtocol`, and tail-calls owner slot
`+0xfb0` (or returns `0xe082280e` when absent). On success the wrapper copies
only the internal dword at `+4` into the public four-byte carrier.

## Local divergence

The local raw BSD bridge directly dispatches selector 265 to
`getOFFLOAD_TCPKA_ENABLE`. When its local cache admits the helper, it clears
the complete eight-byte versioned carrier, writes the version at offset zero,
and writes the enable dword at offset four. It has no public `req_len`
boundary. The Apple public carrier is only four bytes, so that helper cannot
be invoked through this BSD callback, which has marshalled only the outer
request.

Selector 265 has no Tahoe card-specific route in
`TahoeSkywalkIoctlRoutes.hpp`; this correction neither creates one nor changes
the local GET helper or the independent SET path.

## Correction

On Tahoe only, raw BSD `GET` selector 265 delegates to
`IO80211InfraProtocol::processBSDCommand` before any local nested-carrier
access. The fence reads only the outer selector, leaves SET semantics and the
local helper untouched, and restores the family-owned public carrier boundary.

## Verification boundary

`scripts/test_tahoe_tcpka_get_bsd_carrier_fence.sh` pins the 25C56
wrapper/leaf identities, four-byte public versus eight-byte internal carrier
split, GET-only early delegation, unchanged local GET/SET dispatch, and
absence of a fabricated card-specific route. Build/load and normal
association/traffic gates establish regression safety. No raw BSD or raw
Apple80211 selector is sent at runtime.
