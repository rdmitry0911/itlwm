# CR-577 — Tahoe public AWDL AP-alignment fixed-stub alignment (2026-07-16)

## Scope

This layer changes only the normal, non-null Tahoe BSD GET and SET routes for
`APPLE80211_IOC_AWDL_AVAILABILITY_WINDOW_AP_ALIGNMENT` (selector 127) and
`APPLE80211_IOC_AWDL_SYNC_FRAME_AP_BEACON_ALIGNMENT` (selector 128).

## 25C56 reference evidence

All four public wrappers are independent 11-byte leaves:

```text
55 48 89 e5 b8 0e 28 82 e0 5d c3
```

Each returns `0xe082280e` and reads neither public interface nor carrier
argument:

| Selector | GET nlist / VM | SET nlist / VM |
| --- | --- | --- |
| AWDL_AVAILABILITY_WINDOW_AP_ALIGNMENT | 8238 / `0xffffff80021bf689` | 8240 / `0xffffff80021c450a` |
| AWDL_SYNC_FRAME_AP_BEACON_ALIGNMENT | 8230 / `0xffffff80021bf694` | 8232 / `0xffffff80021c4515` |

Exact raw symbol boundaries and bytes are retained in
`artifacts/skywalk-public-awdl-ap-alignment-fixed-stubs-bootkc-current/raw.txt`.

## Change and boundary

Both selectors now terminally return the reference status before inspecting a
carrier.  The layer does not allocate, inspect, retain, or synthesize AWDL
availability-window or sync-frame AP alignment state.  No
AWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE, AWDL_MASTER_CHANNEL, AWDL_BSSID,
AWDL_ELECTION_ID, virtual-interface, card-specific, legacy V1, firmware,
deployment, runtime-selector, association, radio, or traffic claim is made.
