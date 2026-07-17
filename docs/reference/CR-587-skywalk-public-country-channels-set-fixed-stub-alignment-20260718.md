# CR-587 — Tahoe public COUNTRY_CHANNELS BSD SET fixed-stub alignment (2026-07-18)

## Scope

This layer changes only the canonical normal, non-null Tahoe BSD `SIOCSA80211`
route for `APPLE80211_IOC_COUNTRY_CHANNELS` (selector 237). It returns the
exact reference fixed status before inspecting the caller carrier. The existing
zero-fill `getCOUNTRY_CHANNELS` producer is preserved.

## 25C56 reference evidence

The pinned guest BootKC has SHA-256
`eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d`.
Within its `com.apple.iokit.IO80211Family` fileset image, the public setter
`apple80211setCOUNTRY_CHANNELS(IO80211SkywalkInterface*, apple80211_country_channel_data*)`
is nlist 7679 at VM `0xffffff80021c529a` (file offset `0x20c529a`). Its whole
11-byte body is:

```text
55 48 89 e5 b8 0e 28 82 e0 5d c3
```

It returns `0xe082280e` and reads neither the public interface nor the carrier.
The direct raw record and its digest are retained in
`artifacts/skywalk-public-country-channels-set-fixed-stub-bootkc-25c56/raw.txt`.

## Change and boundary

The Tahoe BSD dispatcher now terminates the canonical normal, non-null SET
with that fixed raw status after its existing outer carrier-null guard. The
established BSD bridge reaches the normalized `SIOCSA80211` route before this
point; this layer neither proves nor changes a separate legacy ioctl contract.
GET remains zero-fill and untouched; `COUNTRY_CHANNELS_INFO`,
`SUPPORTED_CHANNELS`, COUNTRY/HW channel paths, controller/card-specific
ingress, virtual interfaces, pre-26 behavior, null behavior, regulatory
state, scan inventory, firmware, association, radio, traffic, and runtime
invocation are not claimed or changed.
