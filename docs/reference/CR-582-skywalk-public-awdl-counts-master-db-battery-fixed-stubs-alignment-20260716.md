# CR-582 — Tahoe public AWDL counts/master-database/battery fixed-stub alignment (2026-07-16)

## Scope

This layer changes only normal, non-null Tahoe BSD GET and SET routes for
AWDL_ELECTION_MASTER_COUNTS (selector 137),
AWDL_GET_AWDL_MASTER_DATABASE (selector 142), and
AWDL_BATTERY_LEVEL (selector 144).

## 25C56 reference evidence

All six public wrappers are separate 11-byte leaves:

    55 48 89 e5 b8 0e 28 82 e0 5d c3

Each returns 0xe082280e and reads neither public interface nor carrier
argument. Exact symbol records and bytes are retained in
artifacts/skywalk-public-awdl-counts-master-db-battery-fixed-stubs-bootkc-current/raw.txt.

## Change and boundary

All three selectors now return the reference status before inspecting a
carrier. This layer does not allocate, inspect, retain, or synthesize AWDL
election-count, master-database, or battery-level state. No
PEER_CACHE_CONTROL, AWDL timing, AWDL Bluetooth coexistence,
AWDL_OOB_REQUEST, AWDL_SYNC_FRAME_TEMPLATE, VHT, virtual-interface,
card-specific, legacy V1, firmware, deployment, runtime-selector,
association, radio, or traffic claim is made.
