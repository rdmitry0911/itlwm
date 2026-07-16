# CR-573: Tahoe public control fixed-stub alignment

## Scope

This layer covers only normal non-null Tahoe BSD GET and SET dispatch for five
public selectors:

| Selector | Value | GET nlist | SET nlist |
| --- | ---: | ---: | ---: |
| APPLE80211_IOC_CDD_MODE | 109 | 7139 | 7157 |
| APPLE80211_IOC_LAST_BCAST_SCAN_TIME | 110 | 7903 | 7932 |
| APPLE80211_IOC_THERMAL_THROTTLING | 111 | 7793 | 7830 |
| APPLE80211_IOC_FACTORY_MODE | 112 | 7372 | 7399 |
| APPLE80211_IOC_REASSOCIATE | 113 | 7311 | 7346 |

The 25C56 evidence in
docs/reference/artifacts/skywalk-public-control-fixed-stubs-bootkc-current/raw.txt
shows all ten wrappers are the same unread 11-byte return of 0xe082280e.
None reads a public interface or carrier argument.

## Source contract

AirportItlwmSkywalkInterface::processApple80211Ioctl returns exactly
0xe082280e for normal non-null Tahoe BSD GET and SET requests to those five
selectors.  The group does not allocate, inspect, or synthesize radio,
thermal, factory, scan-time, or reassociation carriers.

Existing outer request/carrier-null behavior remains outside the group.
Unknown commands and pre-Tahoe builds retain their unsupported fallback.  No
RSSI_BOUNDS, ROAM, TX_CHAIN_POWER, legacy V1, virtual-interface,
card-specific, radio-operation, association-operation, firmware, deployment,
runtime-selector, or traffic claim is made by this layer.

## Verification

scripts/skywalk_public_control_fixed_stubs_alignment_report.py verifies raw
identity and boundaries for all ten leaves, the single guarded source group,
both command directions, null-boundary placement, and absence of matching
card-specific routes.  The ordinary static suite and Tahoe build gates cover
the committed source; this layer does not invoke these selectors at runtime.
