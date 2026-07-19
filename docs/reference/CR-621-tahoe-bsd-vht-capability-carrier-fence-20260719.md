# CR-621: Tahoe BSD VHT_CAPABILITY carrier fence

Date: 2026-07-19

## Scope

This closure covers only raw BSD `GET` and `SET` ingress for
`APPLE80211_IOC_VHT_CAPABILITY` (selector 214) on Tahoe. It does not change the
local VHT IE producer, VHT association fields, AWDL virtual ioctl, or any
card-specific route.

## Fresh reference contract

The exact Tahoe 25C56 BootKC evidence is pinned in
`artifacts/tahoe-vht-capability-bsd-carrier-bootkc-current/raw.txt`. The
canonical raw commands (`0xc02869c9` GET and `0x802869c8` SET) route selector
`0xd6` through distinct external tables. Both private handlers require
non-null `req_data` and `req_len == 0x14` (otherwise `0x16`), require an
interface/owner chain (otherwise `0x2d` or `0x13`), and gate the owner as
`IO80211AWDLPeerManager`.

Raw GET reads the peer-manager cache only. It copies precisely the 14-byte VHT
IE body into caller offsets `+4..+0x11`; it leaves the version and tail bytes
`+0x12..+0x13` untouched. It does not call a public leaf or WCL.

Raw SET reaches `IO80211PeerManager::setVHtCapabilityIE`, which enters the
canonical internal SET table. The public leaf gates the InfraProtocol owner and
tail-calls vtable slot `+0x1170`; on this class that is the local
`setVHT_CAPABILITY` override. The peer-manager commits its 14-byte body only
when that route returns zero.

`IO80211InfraInterface::processBSDCommand` is the real system-family super
path and reaches the recovered external router, so delegation does not recurse
into the local BSD override. There is no Tahoe card-specific selector-214
route.

## Local divergence

The local raw BSD bridge dispatches both directions directly to compact local
helpers. `apple80211_vht_capability` represents the local 18-byte
`version + VHT IE` prefix; its helpers do not validate the 20-byte raw
ABI and read or write the nested caller pointer without peer-manager ownership.

Adding two padding bytes would not repair this. Reference code deliberately
preserves/ignores the raw tail, whereas a size-based local memset or cache copy
would write or retain it. The raw owner and admission boundary must remain with
IO80211Family.

## Correction

On Tahoe only, both raw BSD directions for selector 214 delegate to
`IO80211InfraProtocol::processBSDCommand` before any local nested-carrier
access. The fence reads only the outer selector and leaves all local virtual
helpers available for the system SET route and kernel-owned callers.

The source also recognizes standard `c030/8030` ioctl aliases. The fence sends
those aliases to the family for the same nested-pointer safety boundary, but
does not claim they select the same recovered private-table path as canonical
`c028/8028` commands.

## Verification boundary

`scripts/test_tahoe_vht_capability_bsd_carrier_fence.sh` pins the 25C56
external table entries, ABI and owner gates, exact body/tail behavior, system
super route, SET's retained local virtual route, GET/SET-only Tahoe delegation,
and absence of a fabricated card route. Build/load and normal
association/traffic gates establish regression safety. No raw BSD or raw
Apple80211 selector is sent at runtime.
