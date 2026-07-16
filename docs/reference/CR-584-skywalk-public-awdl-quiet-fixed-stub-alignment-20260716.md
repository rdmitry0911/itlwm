# CR-584 — Tahoe public AWDL quiet fixed-stub alignment (2026-07-16)

## Scope

This layer changes only normal, non-null Tahoe BSD GET and SET routes for
AWDL_QUIET (selector 168).

## 25C56 reference evidence

The two public wrappers are separate 11-byte leaves:

    55 48 89 e5 b8 0e 28 82 e0 5d c3

Each returns 0xe082280e and reads neither public interface nor carrier
argument. Exact symbol records and bytes are retained in
artifacts/skywalk-public-awdl-quiet-fixed-stub-bootkc-current/raw.txt.

## Change and boundary

AWDL_QUIET now returns the reference status before inspecting a carrier. This
layer does not allocate, inspect, retain, or synthesize AWDL quiet state. No
AWDL_PEER_TRAFFIC_REGISTRATION, AWDL_FORCED_ROAM_CONFIG, AWDL RSSI, AES-key,
scan-reservation, control, social-slot, virtual-interface, card-specific,
legacy V1, firmware, deployment, runtime-selector, association, radio, or
traffic claim is made.
