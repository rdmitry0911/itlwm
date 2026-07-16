# CR-580 — Tahoe public AWDL Bluetooth-coexistence fixed-stub alignment (2026-07-16)

## Scope

This layer changes only normal, non-null Tahoe BSD GET and SET routes for
`AWDL_BT_COEX_AW_PROTECTED_PERIOD_LENGTH` (145), `AWDL_BT_COEX_AGREEMENT`
(146), and `AWDL_BT_COEX_AGREEMENT_ENABLED` (147).

## 25C56 reference evidence

All six public wrappers are independent 11-byte leaves:

```text
55 48 89 e5 b8 0e 28 82 e0 5d c3
```

Each returns `0xe082280e` and reads neither public interface nor carrier
argument. Exact records, symbol boundaries, and bytes are retained in
`artifacts/skywalk-public-awdl-btcoex-fixed-stubs-bootkc-current/raw.txt`.

## Change and boundary

The selectors now return the reference status before inspecting a carrier.
The layer does not allocate, inspect, retain, or synthesize AWDL Bluetooth
coexistence agreement, enablement, or protected-period state. No AWDL_STRATEGY,
AWDL_OOB_REQUEST, general BTCOEX selectors, peer-cache, virtual-interface,
card-specific, legacy V1, firmware, deployment, runtime-selector, association,
radio, or traffic claim is made.
