# CR-606 — Tahoe BSD large nested-carrier fence

## Scope

The Tahoe BSD bridge marshals the outer `apple80211req`, not the nested
`req_data` address.  Two GET producers wrote large fixed ranges directly
through that nested caller-owned address without consulting `req_len`:

- `APPLE80211_IOC_COUNTRY_CHANNELS` (237): `0x12d8` bytes from the carrier
  base;
- `APPLE80211_IOC_TRAP_CRASHTRACER_MINI_DUMP` (339): `0x19000` bytes starting
  at carrier offset four.

Both selectors now take the Tahoe BSD-only family-transport fence before the
local dispatcher.  The local producers remain available to non-BSD callers;
no local copy-out ABI or large allocation is fabricated.

## Evidence and limits

The direct local writes are statically visible and have no nested-carrier
marshaling at the shared BSD ingress.  This is the same ownership boundary
that caused the quarantined `SCAN_RESULT` SMAP hazard.

`COUNTRY_CHANNELS` SET is intentionally unchanged: its public 25C56 wrapper
is independently recovered as the unread fixed `0xe082280e` leaf and is
terminalized before carrier access.  No corresponding assertion is made for
either GET's family-transport result.  The fence removes unsafe local
zero-fill from raw BSD ingress; it does not claim return-code or payload
equivalence to the former local producer.

## Laboratory validation

The candidate was built, signed, and loaded through an explicit AuxKC only in
the laboratory guest.  It completed four normal Wi-Fi OFF/ON cycles on the
5 GHz channel-153/VHT80 AP.  Each cycle reached AP authorization and delivered
five ICMP packets from `10.77.0.47` to laboratory-only `10.77.0.1`, with the
default route retained on `en0` and the laboratory route on `en1`.  No raw BSD
or raw Apple80211 probe was executed, and the external validation host was not
contacted or changed.
