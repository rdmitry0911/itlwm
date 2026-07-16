# CR-575 — Tahoe public BGSCAN cache SET fixed-stub alignment (2026-07-16)

## Scope

This layer changes only the normal, non-null Tahoe BSD `SIOCSA80211` route
for `APPLE80211_IOC_BGSCAN_CACHE_RESULTS` (selector 215).  It does not change
the existing `SIOCGA80211` background-scan cache-result producer.

## 25C56 reference evidence

The public GET wrapper is a 85-byte dynamic wrapper at
`0xffffff80021c0034` (nlist 7900), so its cache-result carrier remains out of
scope and stays routed to `getWCL_BGSCAN_CACHE_RESULT`.

The public SET wrapper is the separate 11-byte unread leaf at
`0xffffff80021c4f0e` (nlist 7929):

```text
55 48 89 e5 b8 0e 28 82 e0 5d c3
```

It returns `0xe082280e` and reads neither public interface nor carrier
argument.  The exact raw symbol and byte records are retained in
`artifacts/skywalk-public-bgscan-cache-set-fixed-stub-bootkc-current/raw.txt`.

## Change and boundary

The Tahoe public SET route now returns that exact fixed status before any
carrier dereference.  GET remains dynamic and untouched.  Pre-26, null, unknown
command, virtual-interface, card-specific, legacy V1, cache-update, firmware,
deployment, association, radio, traffic, and runtime-selector behavior are not
claimed or changed.
