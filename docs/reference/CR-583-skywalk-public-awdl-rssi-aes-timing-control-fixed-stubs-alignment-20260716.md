# CR-583 — Tahoe public AWDL RSSI/AES/timing/control fixed-stub alignment (2026-07-16)

## Scope

This layer changes only normal, non-null Tahoe BSD GET and SET routes for
AWDL_RSSI_MEASUREMENT_REQUEST (selector 159), AWDL_AES_KEY (selector 160),
AWDL_SCAN_RESERVED_TIME (selector 161), AWDL_CTL (selector 162), and
AWDL_SOCIAL_TIME_SLOTS (selector 163).

## 25C56 reference evidence

All ten public wrappers are separate 11-byte leaves:

    55 48 89 e5 b8 0e 28 82 e0 5d c3

Each returns 0xe082280e and reads neither public interface nor carrier
argument. Exact symbol records and bytes are retained in
artifacts/skywalk-public-awdl-rssi-aes-timing-control-fixed-stubs-bootkc-current/raw.txt.

## Change and boundary

All five selectors now return the reference status before inspecting a
carrier. This layer does not allocate, inspect, retain, or synthesize AWDL
RSSI-request, AES-key, scan-reservation, control, or social-slot state. No
AWDL_PEER_TRAFFIC_REGISTRATION, AWDL_FORCED_ROAM_CONFIG, AWDL_QUIET,
AWDL_OOB_REQUEST, AWDL_SYNC_FRAME_TEMPLATE, virtual-interface, card-specific,
legacy V1, firmware, deployment, runtime-selector, association, radio, or
traffic claim is made.
