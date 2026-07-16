# CR-562 — Skywalk public STA/RSN GET fixed-stub alignment

## Scope

This change recovers only the current 25C56 public Tahoe BSD GET behavior for
`APPLE80211_IOC_STA_AUTHORIZE` (IOC 74),
`APPLE80211_IOC_STA_DISASSOCIATE` (IOC 75),
`APPLE80211_IOC_STA_DEAUTH` (IOC 76), and
`APPLE80211_IOC_RSN_CONF` (IOC 77).  Each public wrapper is a direct 11-byte
leaf that returns raw `0xe082280e` without reading its public argument.

The recovered bytes are recorded in
`artifacts/skywalk-sta-rsn-get-public-fixed-stubs-bootkc-current/`.  They come
from the read-only current BootKC identity:

- outer SHA-256
  `eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d`;
- outer UUID `F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5`;
- embedded AirportItlwm UUID `8FB4B7F0-D656-3539-B8D6-C1327A50377C`;
- contiguous wrapper range
  `0xffffff80021bf0fa..0xffffff80021bf126` / `0x20bf0fa..0x20bf126`;
- raw fixed status `0xe082280e`.

## Implementation boundary

Each existing public dispatcher case receives a compile-time Tahoe-only GET
branch before its existing `instance` check and SET producer.  That branch
returns `0xe082280e` before carrier access.  Existing non-GET behavior remains
in the original case body: the STA authorization, disassociation, deauth, and
RSN configuration SET producers are preserved byte-for-byte in source.

The four public GET wrappers remain absent from the pre-26 route.  The legacy
V1 `RSN_CONF` SET route and the existing APSTA SET helpers remain separate.
No local carrier contract is inferred for any GET member, and the
card-specific route has no STA/RSN group entry.  No public GET argument is
inspected or used to construct a carrier object.

## Explicit nonclaims

This evidence does not claim outer-null dispatch behavior, a carrier contract,
STA authorization behavior, STA disassociation behavior, STA deauth behavior,
RSN configuration behavior, SET behavior, V1 GET behavior, Virtual IOCTL,
card-specific behavior, firmware, runtime-execution, radio, association,
traffic, or broader Tahoe behavior parity.  No private carrier or selector is
constructed or invoked.

## Validation

`scripts/skywalk_public_sta_rsn_get_fixed_stubs_alignment_report.py` checks
the checked-in raw manifest, selector identities, Tahoe-only GET prefixes,
unmodified SET producer text, preserved V1 RSN SET route, inactive Virtual and
card routes, and generated state report.  Build and runtime evidence are
captured separately; runtime control is passive only.
