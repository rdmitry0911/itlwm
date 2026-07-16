# CR-576 — Tahoe public AWDL tree/guard fixed-stub alignment (2026-07-16)

## Scope

This layer changes only the normal, non-null Tahoe BSD GET and SET routes for
`APPLE80211_IOC_AWDL_MAX_TREE_DEPTH` (selector 123) and
`APPLE80211_IOC_AWDL_GUARD_TIME` (selector 124).

## 25C56 reference evidence

All four public wrappers are separate 11-byte leaves with the exact body:

```text
55 48 89 e5 b8 0e 28 82 e0 5d c3
```

Each returns `0xe082280e` and reads neither public interface nor carrier
argument:

| Selector | GET nlist / VM | SET nlist / VM |
| --- | --- | --- |
| AWDL_MAX_TREE_DEPTH | 7843 / `0xffffff80021bf613` | 7870 / `0xffffff80021c444a` |
| AWDL_GUARD_TIME | 7567 / `0xffffff80021bf61e` | 7604 / `0xffffff80021c4455` |

The raw records retain exact symbol boundaries and bytes in
`artifacts/skywalk-public-awdl-tree-guard-fixed-stubs-bootkc-current/raw.txt`.

## Change and boundary

Both selectors now terminally return the reference status before inspecting a
carrier.  This layer does not allocate, inspect, retain, or synthesize AWDL
tree topology or guard-time state.  No AWDL_ELECTION_ID, AWDL_BSSID, AWDL
SYNC_PARAMS, virtual-interface, card-specific, legacy V1, firmware,
deployment, runtime-selector, association, radio, or traffic claim is made.
