# CR-619: Tahoe BSD MCS_INDEX_SET GET carrier fence

Date: 2026-07-19

## Scope

This closure covers only the Tahoe raw BSD `GET` ingress for
`APPLE80211_IOC_MCS_INDEX_SET` (selector 66). It does not alter MCS selection,
cached BSS state, the local MCS producer, or any card-specific route.

## Fresh reference contract

The exact Tahoe 25C56 BootKC evidence is pinned in
`artifacts/tahoe-mcs-index-get-bsd-carrier-bootkc-current/raw.txt`. The raw
GET handler table maps selector `0x42` to the recovered private wrapper. That
wrapper rejects a null interface with `0x66`, requires non-null carrier plus
`req_len == 0x10` (otherwise `0x16`), and passes that exact caller carrier
directly to WCL. It sends legacy GET `0xc02869c9`, selector `0x42`, and length
`0x10` to `IO80211Glue::sendIOUCToWcl`; it performs no local allocation or
copyout.

Only a handled WCL result other than `0xe082280f` is returned directly.
Otherwise Apple calls the public leaf, gates selector `0x42` at vtable
`+0xcc8`, safely casts to `IO80211NoneProtocol`, and tail-calls owner slot
`+0x528` (or returns `0xe082280e` when absent). The public carrier remains the
same 0x10-byte caller buffer throughout that fallback.

## Local divergence

The local raw BSD bridge directly dispatches selector 66 to
`getMCS_INDEX_SET`. That helper clears and fills its complete
`apple80211_mcs_index_set_data` carrier without a public `req_len` boundary and
bypasses Tahoe's WCL-first admission and public-owner fallback. The source now
asserts the recovered 0x10 ABI, but only the family owns raw BSD carrier
validation and transport.

Selector 66 has no Tahoe card-specific route in
`TahoeSkywalkIoctlRoutes.hpp`; this correction neither creates one nor removes
the local producer used by internal BSS-manager publication.

## Correction

On Tahoe only, raw BSD `GET` selector 66 delegates to
`IO80211InfraProtocol::processBSDCommand` before any local nested-carrier
access. The fence reads only the outer selector, leaves the local internal MCS
producer untouched, and restores family-owned carrier/WCL admission.

## Verification boundary

`scripts/test_tahoe_mcs_index_get_bsd_carrier_fence.sh` pins the 25C56 raw
handler-table entry and wrapper/leaf identities, exact 0x10 public carrier,
WCL-first fallback, GET-only early delegation, retained local producer, and
absence of a fabricated card-specific route. Build/load and normal
association/traffic gates establish regression safety. No raw BSD or raw
Apple80211 selector is sent at runtime.
