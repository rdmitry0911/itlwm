# CR-578 — Tahoe public AWDL OUI/top-master fixed-stub alignment (2026-07-16)

## Scope

This layer changes only normal, non-null Tahoe BSD GET and SET routes for
`APPLE80211_IOC_AWDL_OUI` (selector 131) and
`APPLE80211_IOC_AWDL_TOP_MASTER` (selector 133).

## 25C56 reference evidence

All four public wrappers are separate 11-byte leaves:

```text
55 48 89 e5 b8 0e 28 82 e0 5d c3
```

Each returns `0xe082280e` and reads neither public interface nor carrier
argument:

| Selector | GET nlist / VM | SET nlist / VM |
| --- | --- | --- |
| AWDL_OUI | 7137 / `0xffffff80021bf764` | 7155 / `0xffffff80021c4580` |
| AWDL_TOP_MASTER | 7571 / `0xffffff80021bf7c4` | 7608 / `0xffffff80021c45e0` |

Raw symbol boundaries and bytes are retained in
`artifacts/skywalk-public-awdl-oui-top-master-fixed-stubs-bootkc-current/raw.txt`.

## Change and boundary

Both selectors now return the reference status before inspecting a carrier.
The layer does not allocate, inspect, retain, or synthesize AWDL OUI identity
or top-master state.  No AWDL_MASTER_CHANNEL, AWDL_SYNC_STATE,
AWDL_RSDB_CAPS, AWDL_BSSID, virtual-interface, card-specific, legacy V1,
firmware, deployment, runtime-selector, association, radio, or traffic claim
is made.
