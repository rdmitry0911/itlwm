# CR-574: Tahoe public AWDL service fixed-stub alignment

## Scope

This layer changes only normal non-null Tahoe BSD GET and SET dispatch for
three public selectors:

| Selector | Value | GET nlist | SET nlist |
| --- | ---: | ---: | ---: |
| APPLE80211_IOC_AWDL_SERVICE_PARAMS | 119 | 7844 | 7871 |
| APPLE80211_IOC_AWDL_PEER_SERVICE_REQUEST | 120 | 8104 | 8114 |
| APPLE80211_IOC_AWDL_ELECTION_ALGORITHM_ENABLED | 121 | 8210 | 8214 |

The 25C56 evidence in
docs/reference/artifacts/skywalk-public-awdl-service-fixed-stubs-bootkc-current/raw.txt
shows all six wrappers are the same unread 11-byte return of 0xe082280e.
None reads a public interface or carrier argument.

## Source contract

AirportItlwmSkywalkInterface::processApple80211Ioctl returns exactly
0xe082280e for normal non-null Tahoe BSD GET and SET requests to this narrow
AWDL selector group.  It does not allocate, inspect, or synthesize an AWDL
service, peer-request, or election-algorithm carrier.

Existing outer request/carrier-null behavior remains outside the group.
Unknown commands and pre-Tahoe builds retain their unsupported fallback.  No
AWDL_ELECTION_ID, AWDL_SYNC_PARAMS, AWDL_SYNC_ENABLED, legacy V1,
virtual-interface, card-specific, firmware, deployment, runtime-selector,
association, radio, or traffic claim is made by this layer.

## Verification

scripts/skywalk_public_awdl_service_fixed_stubs_alignment_report.py verifies
raw identity and boundaries for all six leaves, the guarded source group, both
command directions, null-boundary placement, and absence of matching
card-specific routes.  The ordinary static suite and Tahoe build gates cover
the committed source; this layer does not invoke these selectors at runtime.
