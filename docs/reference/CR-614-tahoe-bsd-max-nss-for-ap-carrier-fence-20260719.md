# CR-614: Tahoe BSD MAX_NSS_FOR_AP carrier fence

Date: 2026-07-19

## Scope

This closure covers only the Tahoe raw BSD `GET` ingress for
`APPLE80211_IOC_MAX_NSS_FOR_AP` (selector 259). It does not change the Intel
NSS value, emulate a Broadcom coexistence owner, issue an NSS request, or
change the separate kernel-owned card-specific GET route.

## Fresh reference contract

The exact Tahoe 25C56 BootKC evidence is pinned in
`artifacts/tahoe-max-nss-for-ap-bsd-carrier-bootkc-current/raw.txt`. The
private BSD wrapper rejects a null interface with `0x66`, requires non-null
carrier plus `req_len == 0x4` (otherwise `0x16`), and zeroes an internal
eight-byte legacy result. It sends legacy GET `0xc02869c9`, selector `0x103`,
and length `0x8` to `IO80211Glue::sendIOUCToWcl`.

Only a handled WCL result other than `0xe082280f` is returned directly.
Otherwise Apple calls the public leaf, gates selector `0x103` at vtable
`+0xcc8`, safely casts to `IO80211InfraProtocol`, and tail-calls owner slot
`+0xf98` (or returns `0xe082280e` when absent). On success the wrapper copies
only the internal dword at `+4` into the public four-byte carrier.

## Local divergence

The local raw BSD bridge directly dispatches selector 259 to
`getMAX_NSS_FOR_AP`. That helper clears eight bytes through its incoming
pointer and writes the local Tx NSS dword at offset `+4`, with no `req_len`
boundary. The Apple public carrier is only four bytes; the local helper is
valid only for the separate kernel-owned route that owns its larger carrier.

`TahoeSkywalkIoctlRoutes::kIocMaxNssForAp` remains a retained GET route.
`handleCardSpecific` constructs its own `apple80211req` and calls
`routeTahoeSkywalkIoctl`, which invokes the local dispatcher directly rather
than traversing raw BSD `processBSDCommand`.

## Correction

On Tahoe only, raw BSD `GET` selector 259 delegates to
`IO80211InfraProtocol::processBSDCommand` before any local nested-carrier
access. The fence reads only the outer selector, leaves SET semantics and the
local helper untouched, and preserves the kernel-owned card-specific path.

## Verification boundary

`scripts/test_tahoe_max_nss_for_ap_bsd_carrier_fence.sh` pins the 25C56
wrapper/leaf identities, 4-byte public versus 8-byte internal carrier split,
GET-only early delegation, retained local helper, and retained card route.
Build/load and normal association/traffic gates establish regression safety.
No raw BSD or raw Apple80211 selector is sent at runtime.
