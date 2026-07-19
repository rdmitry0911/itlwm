# CR-613: Tahoe BSD BTCOEX profile carrier fence

Date: 2026-07-19

## Scope

This closure covers only the Tahoe raw BSD `SIOCSA80211` ingress for
`APPLE80211_IOC_BTCOEX_PROFILE` (selector 255). It does not issue a BTCOEX
request, emulate Broadcom coexistence firmware, alter the existing Intel
boot-time coexistence configuration, or change the card-specific,
kernel-owned Skywalk route.

## Reference contract

The fresh 25C56 BootKC evidence is pinned in
`artifacts/tahoe-btcoex-profile-bsd-set-carrier-bootkc-current/raw.txt`.
Its private BSD wrapper first rejects a null interface with `0x66`, then
requires a non-null nested carrier and `req_len == 0x38` (otherwise `0x16`).
It sends legacy SET `0x802869c8`, selector `0xff`, and length `0x38` through
`IO80211Glue::sendIOUCToWcl`. It returns the WCL result only when its handled
bit is set and the result is not `0xe082280f`; otherwise it falls through to
the public leaf. That leaf gates selector `0xff` at vtable `+0xcc8`, safely casts to
`IO80211InfraProtocol`, and tail-calls its `+0x11c8` slot (or returns
`0xe082280e` when no protocol owner exists).

`CR-479-btcoex-public-quarantine-20260713.md` independently pins the matching
AppleBCMWLAN dext owner: SHA-256
`4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab`; Infra
`setBTCOEX_PROFILE` at `0x1000186c8`; and Core implementation at
`0x100124656`. The Core owner rejects null, raw band `+0x03 >= 5`, mode
`+0x00` outside `1..4`, and profile index `+0x04 >= 10`; a valid carrier is a
`0x38`-byte record consumed by the coexistence protocol owner. That separate
owner recovery identifies `btc_profile` as its transport, but this BSD
boundary correction depends only on the pinned wrapper/owner boundary.

Thus the Apple-visible public setter is not a generic direct nested-pointer
write. Its public carrier is owned and validated by the family/Infra path
before the hardware-specific owner consumes it.

## Local divergence

`AirportItlwmSkywalkInterface::processBSDCommand` receives the outer
`apple80211req` from a BSD ioctl and previously sent selector 255 directly to
the local dispatcher. `setBTCOEX_PROFILE` immediately calls
`TahoePayloadBuilders::buildBtcoexProfile`, which reads raw offsets `+0`,
`+3`, and `+4` and copies the entire local profile entry before its value
checks. The BSD bridge has no nested-carrier marshalling or length proof.

The controller `handleCardSpecific` path is different: it constructs a
kernel-owned `apple80211req` and calls `routeTahoeSkywalkIoctl`, which invokes
`processApple80211Ioctl` directly. Its selector-255 route remains necessary
for the internal carrier and is intentionally not fenced.

## Correction

On Tahoe only, raw BSD `SET` selector 255 now delegates to
`IO80211InfraProtocol::processBSDCommand` before the local dispatcher reads
the nested carrier. The fence inspects only the outer request selector, is
`SET`-only, leaves GET semantics unchanged, and retains the local helper for
the kernel-owned card-specific route.

This is a transport/ownership boundary correction. It makes no claim that a
valid public BTCOEX profile can be executed by the Intel backend.

## Verification boundary

`scripts/test_tahoe_btcoex_profile_bsd_set_carrier_fence.sh` pins the selector,
reference owner/IOVAR anchors, unsafe local reads, Tahoe SET-only early
delegation, and the retained card-specific route. Build/load plus normal
association and traffic gates test regression safety. No raw BTCOEX ioctl or
guessed opaque carrier is sent at runtime.
