# AppleBCMWLAN QoS / DynSAR / congestion-control offset contracts

Date: 2026-04-27

Scope: small Tahoe AppleBCMWLANCore helpers adjacent to the hidden-interface and
controller layers. This note records core-private offsets and simple return
contracts only. It does not enable runtime QoS IOVARs or DynSAR policy locally.

## DynSAR fail-safe window

Function:

- `AppleBCMWLANCore::wasDynSARInFailSafeMode()`
- Address: `0xffffff8001632052`

Observed behavior:

- Reads a start-tick value from core-private `+0x74e0`.
- Computes elapsed ticks from a time source.
- Shifts elapsed right by `0x0a`.
- Returns true while the shifted elapsed value is below `0x9502f9`.
- Debug logging, when enabled, uses line `0xdea9`.

## Congestion-control feature gate

Functions:

- `AppleBCMWLANCore::configureCongestionControlMechanisms(unsigned int)`
- Address: `0xffffff8001632144`
- `AppleBCMWLANCore::configureAggregationCongestionControlMechanism(unsigned int)`
- Address: `0xffffff8001632162`

Observed behavior:

- Both functions test core-private byte `+0x7584` bit `0`.
- If the bit is set, return `0`.
- If the bit is clear, return `0xe00002c7`.
- `AppleBCMWLANCore::setCONGESTION_CTRL_IND(...)` stores the caller bool at
  core-private `+0x79d2`.

## AWDL AMPDU and feature flag accessors

Functions:

- `AppleBCMWLANCore::forceAwdlAmpdu()`
- Address: `0xffffff800163428c`
- `AppleBCMWLANCore::setForceAwdlAmpdu(unsigned int)`
- Address: `0xffffff80016342a0`
- `AppleBCMWLANCore::forceDisableAwdlAmpdu()`
- Address: `0xffffff80016342b4`
- `AppleBCMWLANCore::setForceDisableAwdlAmpdu(unsigned int)`
- Address: `0xffffff80016342c8`
- `AppleBCMWLANCore::getHwFeatureFlags() const`
- Address: `0xffffff80016342dc`

Observed behavior:

- `forceAwdlAmpdu()` reads core-private `+0x3768`.
- `setForceAwdlAmpdu(...)` writes core-private `+0x3768`.
- `forceDisableAwdlAmpdu()` reads core-private `+0x3764`.
- `setForceDisableAwdlAmpdu(...)` writes core-private `+0x3764`.
- `getHwFeatureFlags()` reads core-private `+0x458c`.

## Split TX status and address resolution counters

Functions:

- `AppleBCMWLANCore::isSplitTxStatusEnabled()`
- Address: `0xffffff800163434a`
- `AppleBCMWLANCore::getTxAddrResolveReqV4()`
- Address: `0xffffff8001634360`
- `AppleBCMWLANCore::getTxAddrResolveReqV6()`
- Address: `0xffffff8001634374`

Observed behavior:

- `isSplitTxStatusEnabled()` returns bit 0 of core-private `+0x00dc`.
- `getTxAddrResolveReqV4()` reads core-private `+0x2aa4`.
- `getTxAddrResolveReqV6()` reads core-private `+0x2aa8`.

## Slow-wifi / low-latency / tx-blanking carriers

Functions:

- `AppleBCMWLANCore::getSLOW_WIFI_FEATURE_ENABLED(...)`
- `AppleBCMWLANCore::getWCL_LOW_LATENCY_INFO(...)`
- `AppleBCMWLANCore::getWCL_GET_TX_BLANKING_STATUS(...)`

Observed behavior:

- slow-wifi enabled reads core-private `+0x7569`.
- low-latency info is sourced from the owner at `+0x2c28`.
- tx-blanking status returns bit 0 of core-private `+0x4ce8`.

## Local alignment

- Adds `TahoeQosDynsarContracts.hpp` with recovered offsets, bit masks, status,
  threshold, and helper semantics.
- Adds `TahoeOwnerRegistry::QosDynsarOwner` as a local owner-state witness for
  the recovered fields.
- Moves slow-wifi, low-latency, tx-blanking, and congestion-indication
  carriers onto `TahoeOwnerRegistry::QosDynsarOwner` so the interface surface
  reads and writes the same core/private owner state family.
- Does not call QoS IOVARs locally.
- Does not enable DynSAR policy or congestion-control runtime paths.

## Non-claims

- This batch does not claim final primary STA association or data success.
- This batch does not enable AP/SoftAP runtime.
- This batch does not implement QoS IOVAR dispatch.
- This batch does not force DynSAR, congestion, AMPDU, split-TX, or address
  resolution state.

## Runtime validation

2026-07-10 follow-up:

- guest Tahoe build passed `scripts/test_payload_builders.sh`.
- guest Tahoe build passed BootKC symbol gate:
  `OK: all 949 undefined symbols resolve against BootKC`.
- installed kext cdhash: `b29629c243493fcb05ed639b5e9826eb4590e7f1`.
- lab association used `AIAMlab6235` / `aa00bb0900`; en1 received
  `10.77.0.47`.
- 240-second stress: ping to `10.77.0.1` returned `240/240` with `0.0%`
  packet loss while iperf3 ran for the same window at `20.0 Mbits/sec`
  (`572 MBytes`).
- post-stress `wdutil` reported no Wi-Fi faults, recoveries, or link tests.
