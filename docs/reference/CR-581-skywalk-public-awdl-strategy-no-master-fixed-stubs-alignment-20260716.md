# CR-581 — Tahoe public AWDL strategy/no-master fixed-stub alignment (2026-07-16)

## Scope

This layer changes only normal, non-null Tahoe BSD GET and SET routes for
`AWDL_STRATEGY` (selector 148) and `AWDL_MAX_NO_MASTER_PERIODS` (selector 150).

## 25C56 reference evidence

All four public wrappers are separate 11-byte leaves:

```text
55 48 89 e5 b8 0e 28 82 e0 5d c3
```

Each returns `0xe082280e` and reads neither public interface nor carrier
argument. Exact symbol records and bytes are retained in
`artifacts/skywalk-public-awdl-strategy-no-master-fixed-stubs-bootkc-current/raw.txt`.

## Change and boundary

Both selectors now return the reference status before inspecting a carrier.
This layer does not allocate, inspect, retain, or synthesize AWDL strategy or
max-no-master-period state. No AWDL_OOB_REQUEST, AWDL_SYNC_FRAME_TEMPLATE,
AWDL master database, VHT, virtual-interface, card-specific, legacy V1,
firmware, deployment, runtime-selector, association, radio, or traffic claim is
made.
