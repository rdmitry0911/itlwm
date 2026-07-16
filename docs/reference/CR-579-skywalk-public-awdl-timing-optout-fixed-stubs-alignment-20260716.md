# CR-579 — Tahoe public AWDL timing/opt-out fixed-stub alignment (2026-07-16)

## Scope

This layer changes only normal, non-null Tahoe BSD GET and SET routes for
selectors 138–141: `AWDL_PERIODIC_SYNC_FRAME_PACKET_LIFETIME`,
`AWDL_MASTER_MODE_SYNC_FRAME_PERIOD`,
`AWDL_NON_ELECTION_MASTER_MODE_SYNC_FRAME_PERIOD`, and
`AWDL_EXPLICIT_AVAILABILITY_WINDOW_EXTENSION_OPT_OUT`.

## 25C56 reference evidence

All eight public wrappers are independent 11-byte leaves:

```text
55 48 89 e5 b8 0e 28 82 e0 5d c3
```

Each returns `0xe082280e` and reads neither public interface nor carrier
argument.  The exact records, symbol boundaries, and bytes are retained in
`artifacts/skywalk-public-awdl-timing-optout-fixed-stubs-bootkc-current/raw.txt`.

## Change and boundary

The four selectors now return the reference status before inspecting a carrier.
This layer does not allocate, inspect, retain, or synthesize AWDL frame-period,
lifetime, or availability-window opt-out state.  No AWDL_GET_AWDL_MASTER_DATABASE,
AWDL_CIPHER_KEY, AWDL synchronization-channel, virtual-interface, card-specific,
legacy V1, firmware, deployment, runtime-selector, association, radio, or traffic
claim is made.
