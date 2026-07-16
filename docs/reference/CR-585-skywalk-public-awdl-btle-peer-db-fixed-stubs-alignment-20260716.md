# CR-585 — Tahoe public AWDL BTLE/peer-database fixed-stub alignment (2026-07-16)

## Scope

This layer changes only normal, non-null Tahoe BSD GET and SET routes for
AWDL_BTLE_PEER_INDICATION (selector 201), AWDL_BTLE_STATE_PARAMS (selector
202), AWDL_PEER_DATABASE (selector 203), and
AWDL_BTLE_ENABLE_SYNC_WITH_PARAMS (selector 204).

## 25C56 reference evidence

All eight public wrappers are separate 11-byte leaves:

    55 48 89 e5 b8 0e 28 82 e0 5d c3

Each returns 0xe082280e and reads neither public interface nor carrier
argument. Exact symbol records and bytes are retained in
artifacts/skywalk-public-awdl-btle-peer-db-fixed-stubs-bootkc-current/raw.txt.

## Change and boundary

All four selectors now return the reference status before inspecting a
carrier. This layer does not allocate, inspect, retain, or synthesize AWDL
BTLE indication, state, peer-database, or sync-with-parameters state. No
AWDL_PIGGYBACK_SCAN_REQ, AWDL_PRIVATE_ELECTION_ID, AWDL_QUIET,
AWDL_PEER_TRAFFIC_REGISTRATION, virtual-interface, card-specific, legacy V1,
firmware, deployment, runtime-selector, association, radio, or traffic claim
is made.
