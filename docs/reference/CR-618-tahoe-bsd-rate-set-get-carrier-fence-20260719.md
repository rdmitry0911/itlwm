# CR-618: Tahoe BSD RATE_SET GET carrier fence

Date: 2026-07-19

## Scope

This closure covers only the Tahoe raw BSD `GET` ingress for
`APPLE80211_IOC_RATE_SET` (selector 32). It does not change the dynamic local
rate producer, the separate Tahoe SET fixed stub, rate selection, or any
card-specific route.

## Fresh reference contract

The exact Tahoe 25C56 BootKC evidence is pinned in
`artifacts/tahoe-rate-set-get-bsd-carrier-bootkc-current/raw.txt`. The private
BSD wrapper rejects a null interface with `0x66`, requires non-null carrier
plus `req_len == 0xbc` (otherwise `0x16`), and passes that exact caller carrier
directly to WCL. It sends legacy GET `0xc02869c9`, selector `0x20`, and length
`0xbc` to `IO80211Glue::sendIOUCToWcl`; it performs no local allocation or
copyout.

Only a handled WCL result other than `0xe082280f` is returned directly.
Otherwise Apple calls the public leaf, gates selector `0x20` at vtable
`+0xcc8`, safely casts to `IO80211NoneProtocol`, and tail-calls owner slot
`+0x308` (or returns `0xe082280e` when absent). The public carrier remains the
same 0xbc-byte caller buffer throughout that fallback.

## Local divergence

The local raw BSD bridge directly dispatches selector 32 to `getRATE_SET`.
That helper clears and fills its complete `apple80211_rate_set_data` carrier
without a public `req_len` boundary and bypasses Tahoe's WCL-first admission
and public-owner fallback. The source now asserts the recovered `0xbc` ABI,
but only the family owns raw BSD carrier validation and transport.

Selector 32 has no Tahoe card-specific route in
`TahoeSkywalkIoctlRoutes.hpp`; this correction neither creates one nor removes
the local producer used by internal BSS-manager publication.

## Correction

On Tahoe only, raw BSD `GET` selector 32 delegates to
`IO80211InfraProtocol::processBSDCommand` before any local nested-carrier
access. The fence reads only the outer selector, preserves the separate SET
fixed stub and local internal producer, and restores family-owned carrier/WCL
admission.

## Verification boundary

`scripts/test_tahoe_rate_set_get_bsd_carrier_fence.sh` pins the 25C56
wrapper/leaf identities, exact 0xbc public carrier, WCL-first fallback,
GET-only early delegation, retained local producer/SET stub, and absence of a
fabricated card-specific route. Build/load and normal association/traffic
gates establish regression safety. No raw BSD or raw Apple80211 selector is
sent at runtime.
